//! One control runtime. Context temporarily comes from the current profile
//! owner; HTTP and parsed display results are owned solely by Rust.
use crate::core::Event;
use moonlight_host_api::{CancellationToken, HostContext};
use std::{future::Future, pin::Pin, sync::mpsc};
use tokio::sync::mpsc::{UnboundedSender, unbounded_channel};

enum Command {
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
    pub fn spawn(events: mpsc::Sender<Event>) -> anyhow::Result<Self> {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()?;
        let (sender, mut receiver) = unbounded_channel();
        std::thread::Builder::new().name("host-control".into()).spawn(move || runtime.block_on(async move {
            type Job = Pin<Box<dyn Future<Output = Result<Vec<moonlight_host_api::Display>, moonlight_host_api::ApiError>> + Send>>;
            let mut pending: Option<(String, u32)> = None;
            let mut job: Option<Job> = None;
            let mut cancellation = CancellationToken::new();
            loop {
                tokio::select! {
                    biased;
                    command = receiver.recv() => match command {
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
                        Some(Command::Stop) | None => { cancellation.cancel(); break; }
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
    pub fn context(&self, host: String, id: u32, context: HostContext) {
        let _ = self.0.send(Command::Context { host, id, context });
    }
    pub fn stop(&self) {
        let _ = self.0.send(Command::Stop);
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
    fn ignores_obsolete_context_and_stops_without_polling() {
        let (tx, rx) = mpsc::channel();
        let client = Client::spawn(tx).unwrap();
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
