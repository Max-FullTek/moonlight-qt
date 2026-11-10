//! One control runtime. Context temporarily comes from the current profile
//! owner; host polling, display queries and pairing are owned solely by Rust.
use crate::core::Event;
use moonlight_host_api::{CancellationToken, HostContext};
use std::{future::Future, pin::Pin, sync::mpsc};
use tokio::sync::mpsc::{UnboundedSender, unbounded_channel};

enum Command {
    Monitors(Vec<crate::monitor::Spec>),
    ResetPairWait,
    BeginPair {
        host: String,
        id: u32,
    },
    PairContext {
        host: String,
        id: u32,
        context: moonlight_host_api::pairing::Context,
    },
    Begin {
        host: String,
        id: u32,
    },
    Context {
        host: String,
        id: u32,
        context: HostContext,
    },
    Stop,
}
#[derive(Clone)]
pub struct Client(UnboundedSender<Command>);
impl Client {
    pub fn spawn(
        events: mpsc::Sender<Event>,
        commands: mpsc::Sender<serde_json::Value>,
    ) -> anyhow::Result<Self> {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()?;
        let (sender, mut receiver) = unbounded_channel();
        std::thread::Builder::new().name("host-control".into()).spawn(move || runtime.block_on(async move {
            type Job = Pin<Box<dyn Future<Output = Result<Vec<moonlight_host_api::Display>, moonlight_host_api::ApiError>> + Send>>;
            let mut pending: Option<(String, u32)> = None;
            let mut job: Option<Job> = None;
            let mut cancellation = CancellationToken::new();
            type PairJob = Pin<Box<dyn Future<Output = Result<moonlight_host_api::pairing::Outcome, moonlight_host_api::pairing::Error>> + Send>>;
            let mut pair_pending: Option<(String, u32)> = None;
            let mut pair_job: Option<PairJob> = None;
            let pair_cancellation = CancellationToken::new();
            let mut monitors: std::collections::HashMap<String, (u32, CancellationToken, tokio::task::JoinHandle<()>)> = std::collections::HashMap::new();
            loop {
                tokio::select! {
                    biased;
                    command = receiver.recv() => match command {
                        Some(Command::Monitors(specs)) => {
                            monitors.retain(|host, (_, token, job)| {
                                if specs.iter().any(|spec| &spec.host == host) { true }
                                else { token.cancel(); job.abort(); false }
                            });
                            for spec in specs {
                                if monitors.get(&spec.host).is_some_and(|(id, _, _)| *id == spec.generation) { continue; }
                                if let Some((_, token, job)) = monitors.remove(&spec.host) { token.cancel(); job.abort(); }
                                let token = CancellationToken::new();
                                let host = spec.host.clone();
                                let id = spec.generation;
                                let job = tokio::spawn(crate::monitor::run(spec, commands.clone(), token.clone()));
                                monitors.insert(host, (id, token, job));
                            }
                        }
                        Some(Command::ResetPairWait) => {
                            if pair_job.is_none() { pair_pending = None; }
                        }
                        Some(Command::BeginPair {host, id}) => {
                            if pair_pending.is_none() { pair_pending = Some((host, id)); }
                        }
                        Some(Command::PairContext {host, id, context}) => {
                            if pair_job.is_none() && pair_pending.as_ref() == Some(&(host.clone(), id)) {
                                let commands = commands.clone();
                                pair_job = Some(Box::pin(moonlight_host_api::pairing::pair(context, pair_cancellation.clone(), move |name| {
                                    let _ = commands.send(serde_json::json!({"command":"pair_name", "host":host, "request_id":id, "name":name}));
                                })));
                            }
                        }
                        Some(Command::Begin {host, id}) => {
                            cancellation.cancel();
                            job = None;
                            cancellation = CancellationToken::new();
                            pending = Some((host, id));
                        }
                        Some(Command::Context {host, id, context}) => {
                            if job.is_none() && pending.as_ref() == Some(&(host, id)) {
                                job = Some(Box::pin(moonlight_host_api::displays(context, cancellation.clone())));
                            }
                        }
                        Some(Command::Stop) | None => {
                            cancellation.cancel(); pair_cancellation.cancel();
                            for (_, (_, token, job)) in monitors.drain() { token.cancel(); job.abort(); }
                            break;
                        }
                    },
                    result = async { match job.as_mut() {
                        Some(job) => job.await,
                        None => std::future::pending().await,
                    }} => {
                        job = None;
                        if let Some((host, id)) = pending.take() {
                            let displays = result.unwrap_or_else(|error| {
                                // Retain NvHTTP::getDisplays empty-on-failure UI
                                // behavior. Never print replies, URLs or keys.
                                eprintln!("Host display query failed: {error:?}");
                                Vec::new()
                            });
                            if events.send(Event::Displays {host, displays, request_id: Some(id)}).is_err() { break; }
                        }
                    }
                    result = async { match pair_job.as_mut() {
                        Some(job) => job.await,
                        None => std::future::pending().await,
                    }} => {
                        pair_job = None;
                        if let Some((host, id)) = pair_pending.take() {
                            let (state, certificate, error) = match result {
                                Ok(outcome) => (outcome.state as u8, outcome.certificate.unwrap_or_default(), ""),
                                Err(moonlight_host_api::pairing::Error::Transport(moonlight_host_api::transport::Error::Timeout)) => (2, String::new(), "timeout"),
                                Err(_) => (2, String::new(), "failed"),
                            };
                            let _ = commands.send(serde_json::json!({"command":"pair_complete","host":host,"request_id":id,"state":state,"certificate":certificate,"error":error}));
                        }
                    }
                }
            }
        }))?;
        Ok(Self(sender))
    }
    pub fn begin(&self, host: String, id: u32) -> anyhow::Result<()> {
        self.0
            .send(Command::Begin { host, id })
            .map_err(|_| anyhow::anyhow!("主機控制服務已結束"))
    }
    pub fn begin_pair(&self, host: String, id: u32) -> anyhow::Result<()> {
        self.0
            .send(Command::BeginPair { host, id })
            .map_err(|_| anyhow::anyhow!("主機控制服務已結束"))
    }
    pub fn worker_reply(&self) {
        let _ = self.0.send(Command::ResetPairWait);
    }
    pub fn pair_context(
        &self,
        host: String,
        id: u32,
        context: moonlight_host_api::pairing::Context,
    ) {
        let _ = self.0.send(Command::PairContext { host, id, context });
    }
    pub fn context(&self, host: String, id: u32, context: HostContext) {
        let _ = self.0.send(Command::Context { host, id, context });
    }
    pub fn stop(&self) {
        let _ = self.0.send(Command::Stop);
    }
    pub fn monitors(&self, specs: Vec<crate::monitor::Spec>) {
        let _ = self.0.send(Command::Monitors(specs));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;
    fn context() -> HostContext {
        serde_json::from_value(serde_json::json!({"base_url":"invalid", "server_certificate":"", "client_certificate":"",
            "client_key":"", "unique_id":"", "client_name":"", "nonce":""})).unwrap()
    }
    #[test]
    fn monitor_generation_replacement_cancels_old_tasks_and_stops() {
        let (events, _) = mpsc::channel();
        let (commands, replies) = mpsc::channel();
        let client = Client::spawn(events, commands).unwrap();
        let spec = |generation| crate::monitor::Spec {
            host: "host".into(),
            generation,
            context: context(),
            addresses: vec!["invalid".into()],
            online: false,
            has_apps: false,
        };
        client.monitors(vec![spec(1)]);
        client.monitors(vec![spec(2)]);
        // A result already queued before replacement may retain generation 1;
        // the worker gate must ignore it. The replacement must still run.
        loop {
            let reply = replies.recv_timeout(Duration::from_secs(2)).unwrap();
            assert_eq!(reply["command"], "monitor_update");
            if reply["generation"] == 2 {
                break;
            }
            assert_eq!(reply["generation"], 1);
        }
        client.monitors(Vec::new());
        client.stop();
        assert!(matches!(
            replies.recv_timeout(Duration::from_secs(2)),
            Err(mpsc::RecvTimeoutError::Disconnected)
        ));
    }
    #[test]
    fn pairing_rejects_stale_context_and_retries_after_worker_rejection() {
        let (events, _) = mpsc::channel();
        let (commands, replies) = mpsc::channel();
        let client = Client::spawn(events, commands).unwrap();
        let context = || moonlight_host_api::pairing::Context {
            host: context(),
            http_url: "invalid".into(),
            generation: 7,
            pin: "1234".into(),
        };
        client.begin_pair("old".into(), 1).unwrap();
        client.worker_reply(); // Worker declined before supplying credentials.
        client.begin_pair("new".into(), 2).unwrap();
        client.pair_context("old".into(), 1, context());
        client.pair_context("new".into(), 2, context());
        let result = replies.recv_timeout(Duration::from_secs(2)).unwrap();
        assert_eq!(result["host"], "new");
        assert_eq!(result["request_id"], 2);
        assert_eq!(result["state"], 2);
        assert_eq!(result["certificate"], "");
        client.begin_pair("again".into(), 3).unwrap();
        client.pair_context("new".into(), 2, context());
        client.pair_context("again".into(), 3, context());
        assert_eq!(
            replies.recv_timeout(Duration::from_secs(2)).unwrap()["request_id"],
            3
        );
        client.stop();
        assert!(matches!(
            replies.recv_timeout(Duration::from_secs(2)),
            Err(mpsc::RecvTimeoutError::Disconnected)
        ));
    }
    #[test]
    fn ignores_obsolete_context_and_stops_without_polling() {
        let (tx, rx) = mpsc::channel();
        let (commands, _) = mpsc::channel();
        let client = Client::spawn(tx, commands).unwrap();
        client.begin("old".into(), 1).unwrap();
        client.begin("new".into(), 2).unwrap();
        client.context("old".into(), 1, context());
        client.context("new".into(), 2, context());
        assert!(
            matches!(rx.recv_timeout(Duration::from_secs(2)).unwrap(), Event::Displays {host, displays, request_id:Some(2)} if host == "new" && displays.is_empty())
        );
        client.context("old".into(), 1, context());
        client.begin("waiting".into(), 3).unwrap();
        client.stop();
        assert!(matches!(
            rx.recv_timeout(Duration::from_secs(2)),
            Err(mpsc::RecvTimeoutError::Disconnected)
        ));
    }
}
