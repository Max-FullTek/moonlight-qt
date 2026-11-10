//! One control runtime. Context temporarily comes from the current profile
//! owner; polling, displays, pairing and streaming control requests use Rust.
use crate::core::Event;
use moonlight_host_api::{CancellationToken, HostContext};
use std::{future::Future, pin::Pin, sync::mpsc};
use tokio::sync::mpsc::{UnboundedSender, unbounded_channel};

enum Command {
    Session(SessionRequest),
    BeginQuit { host: String, id: u32 },
    QuitContext { host: String, id: u32, context: HostContext, http_url: String },
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
#[derive(serde::Deserialize)]
pub struct SessionRequest {
    pub request_id: u64,
    pub context: HostContext,
    pub request: moonlight_host_api::session::Operation,
}
impl std::fmt::Debug for SessionRequest {
    fn fmt(&self,f:&mut std::fmt::Formatter<'_>)->std::fmt::Result {f.debug_struct("SessionRequest").field("request_id",&self.request_id).finish_non_exhaustive()}
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
            type QuitJob = Pin<Box<dyn Future<Output = Result<(), moonlight_host_api::hosts::Error>> + Send>>;
            let mut quit_pending: Option<(String, u32)> = None;
            let mut quit_job: Option<QuitJob> = None;
            let quit_cancellation = CancellationToken::new();
            let session_cancellation=CancellationToken::new();
            let mut session_jobs=tokio::task::JoinSet::new();
            let mut monitors: std::collections::HashMap<String, (u32, CancellationToken, tokio::task::JoinHandle<()>)> = std::collections::HashMap::new();
            loop {
                tokio::select! {
                    biased;
                    command = receiver.recv() => match command {
                        Some(Command::Session(request)) => {
                            let token=session_cancellation.clone();
                            session_jobs.spawn(async move {
                                let result=moonlight_host_api::session::execute(request.context,request.request,token).await;
                                use moonlight_host_api::hosts::Error;
                                let response=match result {
                                    Ok(value)=>serde_json::json!({"ok":true,"value":value}),
                                    Err(Error::Rejected {code,message})=>serde_json::json!({"ok":false,"kind":"host","code":code,"message":message}),
                                    Err(Error::Xml(moonlight_host_api::xml::Error::Status(code)))=>serde_json::json!({"ok":false,"kind":"host","code":code,"message":""}),
                                    Err(Error::Http(code))=>serde_json::json!({"ok":false,"kind":"http","code":code}),
                                    Err(Error::Transport(moonlight_host_api::transport::Error::Timeout))=>serde_json::json!({"ok":false,"kind":"timeout"}),
                                    Err(_)=>serde_json::json!({"ok":false,"kind":"failed"}),
                                };
                                (request.request_id,response)
                            });
                        }
                        Some(Command::BeginQuit {host, id}) => {
                            if quit_pending.is_none() { quit_pending = Some((host, id)); }
                        }
                        Some(Command::QuitContext {host, id, context, http_url}) => {
                            if quit_job.is_none() && quit_pending.as_ref() == Some(&(host, id)) {
                                let token = quit_cancellation.clone();
                                quit_job = Some(Box::pin(async move { moonlight_host_api::hosts::quit(context, &http_url, token).await }));
                            }
                        }
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
                            if quit_job.is_none() { quit_pending = None; }
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
                            quit_cancellation.cancel();
                            session_cancellation.cancel();session_jobs.abort_all();
                            for (_, (_, token, job)) in monitors.drain() { token.cancel(); job.abort(); }
                            break;
                        }
                    },
                    Some(result)=session_jobs.join_next(), if !session_jobs.is_empty() => {
                        if let Ok((id,result))=result {
                            let _=commands.send(serde_json::json!({"command":"session_api_result","request_id":id,"result":result}));
                        }
                    },
                    result = async { match quit_job.as_mut() {
                        Some(job) => job.await,
                        None => std::future::pending().await,
                    }} => {
                        quit_job = None;
                        if let Some((host, id)) = quit_pending.take() {
                            let (error, status, message) = match result {
                                Ok(()) => ("", 0, String::new()),
                                Err(moonlight_host_api::hosts::Error::Xml(moonlight_host_api::xml::Error::Status(599))) |
                                Err(moonlight_host_api::hosts::Error::Rejected {code:599,..}) => ("not_owner", 599, String::new()),
                                Err(moonlight_host_api::hosts::Error::Rejected {code, message}) => ("host", code, message),
                                Err(moonlight_host_api::hosts::Error::Http(code)) => ("http", code as i32, String::new()),
                                Err(_) => ("failed", 0, String::new()),
                            };
                            let _ = commands.send(serde_json::json!({"command":"quit_complete", "host":host,
                                "request_id":id, "error":error, "status":status, "message":message}));
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
    pub fn session(&self,request:SessionRequest) {let _=self.0.send(Command::Session(request));}
    pub fn begin_quit(&self, host: String, id: u32) -> anyhow::Result<()> {
        self.0.send(Command::BeginQuit {host, id})
            .map_err(|_| anyhow::anyhow!("主機控制服務已結束"))
    }
    pub fn quit_context(&self, host: String, id: u32, context: HostContext, http_url: String) {
        let _ = self.0.send(Command::QuitContext {host, id, context, http_url});
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
    #[test]
    fn session_requests_keep_ids_and_hide_private_context() {
        let (events,_)=mpsc::channel();let (commands,replies)=mpsc::channel();
        let api=Client::spawn(events,commands).unwrap();
        for id in [42,7] {
            let mut context=context();context.client_key="private-secret".into();
            let request=SessionRequest {request_id:id,context,request:moonlight_host_api::session::Operation::AbrCapabilities};
            assert!(!format!("{request:?}").contains("private-secret"));
            api.session(request);
        }
        let mut ids=Vec::new();
        for _ in 0..2 {
            let reply=replies.recv_timeout(Duration::from_secs(2)).unwrap();
            assert_eq!(reply["command"],"session_api_result");
            assert_eq!(reply["result"]["ok"],false);
            ids.push(reply["request_id"].as_u64().unwrap());
        }
        ids.sort_unstable();assert_eq!(ids,[7,42]);
        api.stop();
        assert!(matches!(replies.recv_timeout(Duration::from_secs(2)),Err(mpsc::RecvTimeoutError::Disconnected)));
    }
    #[test]
    #[ignore = "requires native NvHTTP peer and pinned loopback fixture; run check_session.py"]
    fn session_api_through_native_callers() -> anyhow::Result<()> {
        use std::io::{BufRead,BufReader,Write};
        use std::process::{Command,Stdio};
        let args:Vec<String>=serde_json::from_str(&std::env::var("SESSION_FIXTURE_ARGS")?)?;
        let mut child=Command::new(std::env::var("SESSION_FIXTURE_PEER")?).args(args)
            .env("SESSION_FIXTURE_RPC","1").stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn()?;
        let mut stdin=child.stdin.take().unwrap();
        let stdout=child.stdout.take().unwrap();
        let stderr=child.stderr.take().unwrap();
        let diagnostics=std::thread::spawn(move || {let mut reader=BufReader::new(stderr);let mut bytes=Vec::new();std::io::Read::read_to_end(&mut reader,&mut bytes).unwrap();bytes});
        let (events,_)=mpsc::channel();let (commands,replies)=mpsc::channel();
        let api=Client::spawn(events,commands)?;
        let writer=std::thread::spawn(move || {for reply in replies {serde_json::to_writer(&mut stdin,&reply).unwrap();writeln!(stdin).unwrap();stdin.flush().unwrap();}});
        let outcome=(||->anyhow::Result<()> {
            for line in BufReader::new(stdout).lines() {
                let value:serde_json::Value=serde_json::from_str(&line?)?;
                if value["done"]==true {
                    std::fs::write(std::env::var("SESSION_FIXTURE_RESULT")?,serde_json::to_vec(&value)?)?;return Ok(());
                }
                let event:crate::core::Event=serde_json::from_value(value)?;
                if let crate::core::Event::SessionApi {request}=event {
                    api.session(request);
                    if std::env::var("SESSION_FIXTURE_STOP").as_deref()==Ok("1") {
                        std::thread::sleep(Duration::from_millis(150)); api.stop();
                    }
                } else {anyhow::bail!("Unexpected native event");}
            }
            anyhow::bail!("No native result")
        })();
        api.stop();drop(api);writer.join().unwrap();
        anyhow::ensure!(child.wait()?.success(),"Native caller failed");
        let bytes=diagnostics.join().unwrap();
        let log=String::from_utf8_lossy(&bytes);
        anyhow::ensure!(!log.contains("PRIVATE KEY") && !log.contains("BEGIN CERTIFICATE") && !log.contains("Launch response:") && !log.contains("Executing native request:"),"Native caller leaked data or performed network I/O");
        outcome
    }
    fn context() -> HostContext {
        serde_json::from_value(serde_json::json!({"base_url":"invalid", "server_certificate":"", "client_certificate":"",
            "client_key":"", "unique_id":"", "client_name":"", "nonce":""})).unwrap()
    }
    #[test]
    fn quit_rejects_stale_context_and_retries_after_worker_rejection() {
        let (events, _) = mpsc::channel();
        let (commands, replies) = mpsc::channel();
        let client = Client::spawn(events, commands).unwrap();
        client.begin_quit("old".into(), 1).unwrap();
        client.worker_reply(); // Not ready, or already free: no context follows.
        client.begin_quit("new".into(), 2).unwrap();
        client.quit_context("old".into(), 1, context(), "invalid".into());
        client.quit_context("new".into(), 2, context(), "invalid".into());
        let result = replies.recv_timeout(Duration::from_secs(2)).unwrap();
        assert_eq!(result["command"], "quit_complete");
        assert_eq!(result["host"], "new");
        assert_eq!(result["request_id"], 2);
        assert_eq!(result["error"], "failed");
        client.begin_quit("again".into(), 3).unwrap();
        client.quit_context("new".into(), 2, context(), "invalid".into());
        client.quit_context("again".into(), 3, context(), "invalid".into());
        assert_eq!(replies.recv_timeout(Duration::from_secs(2)).unwrap()["request_id"], 3);
        client.stop();
        assert!(matches!(replies.recv_timeout(Duration::from_secs(2)), Err(mpsc::RecvTimeoutError::Disconnected)));
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
