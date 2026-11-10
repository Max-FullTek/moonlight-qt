//! Replaces PcMonitorThread for the GPUI client. One async task per host on
//! the existing control runtime; no new threads or profile owner.
use moonlight_host_api::{CancellationToken, HostContext, hosts};
use serde::Deserialize;
use serde_json::{Value, json};
use std::{sync::mpsc::Sender, time::Duration};

#[derive(Debug, Deserialize)]
pub struct Spec {
    pub host: String,
    pub generation: u32,
    pub context: HostContext,
    pub addresses: Vec<String>,
    pub online: bool,
    pub has_apps: bool,
}
pub async fn run(spec: Spec, commands: Sender<Value>, cancellation: CancellationToken) {
    let mut online = spec.online;
    let mut has_apps = spec.has_apps;
    let mut polls_since_apps = 10u32;
    loop {
        let mut found = None;
        for _ in 0..if online { 2 } else { 1 } {
            for address in &spec.addresses {
                if cancellation.is_cancelled() {
                    return;
                }
                let mut context = spec.context.clone();
                // PcMonitorThread discovers the port at each address rather
                // than trusting a port learned for another address.
                context.base_url.clear();
                if let Ok(info) =
                    hosts::server_info(context, address, true, cancellation.clone()).await
                {
                    if info.uuid == spec.host {
                        found = Some((address, info));
                        break;
                    }
                }
            }
            if found.is_some() {
                break;
            }
        }
        online = found.is_some();
        polls_since_apps = polls_since_apps.saturating_add(1);
        let update = |details: Value| {
            commands.send(json!({"command":"monitor_update", "host":spec.host,
            "generation":spec.generation, "update":details}))
        };
        if let Some((address, info)) = found {
            // Publish status before fetching apps, so a slow app list cannot
            // delay the online indication or hide a usable cached list.
            if update(json!({"online":true, "address":address, "info":info})).is_err() {
                return;
            }
            if info.paired && (!has_apps || polls_since_apps >= 10) {
                let mut context = spec.context.clone();
                let Ok(base) = hosts::https_base(address, info.https_port) else {
                    return;
                };
                context.base_url = base;
                if let Ok(apps) = hosts::apps(context, cancellation.clone()).await {
                    if !apps.is_empty() {
                        if update(json!({"apps":apps})).is_err() {
                            return;
                        }
                        has_apps = true;
                        polls_since_apps = 0;
                    }
                }
            }
        } else if update(json!({"online":false})).is_err() {
            return;
        }
        tokio::select! {
            _ = cancellation.cancelled() => return,
            _ = tokio::time::sleep(Duration::from_secs(3)) => {}
        }
    }
}
