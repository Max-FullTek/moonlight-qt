//! Protocol 1 uses inherited anonymous pipes, never a listening network socket.
//! The matching Moonlight.exe must be beside this frontend. Its core flag skips
//! QML and uses an independent profile. Rust owns polling, application/display
//! queries, pairing and session control HTTP/TLS/parsing. Profile persistence,
//! media/session scheduling and SDL remain in C++ during migration; private
//! context never reaches the UI event consumer.
use serde::Deserialize;
use serde_json::{Value, json};
use std::{
    io::{BufRead, BufReader, Write},
    process::{Command, Stdio},
    sync::mpsc,
    sync::atomic::{AtomicU32, Ordering},
    thread,
    time::{Duration, Instant},
};

#[derive(Clone, Debug, Deserialize)]
pub struct Host {
    pub id: String,
    pub name: String,
    pub address: String,
    pub online: bool,
    pub paired: bool,
    pub current_app: i32,
    pub apps: Vec<HostApp>,
}
#[derive(Clone, Debug, Deserialize)]
pub struct HostApp {
    pub id: i32,
    pub name: String,
    #[serde(default)]
    pub hidden: bool,
    #[serde(default)]
    pub direct_launch: bool,
}
impl Host {
    pub fn direct_app(&self) -> Option<i32> {
        self.apps
            .iter()
            .find(|app| app.direct_launch && !app.hidden)
            .map(|app| app.id)
    }
}
pub type HostDisplay = moonlight_host_api::Display;
#[derive(Debug, Deserialize)]
#[serde(tag = "event", rename_all = "snake_case")]
pub enum Event {
    ProfileIdentity {
        identity: crate::profile_identity::ProfileIdentity,
    },
    Ready {
        qml_loaded: bool,
    },
    Hosts {
        hosts: Vec<Host>,
    },
    Displays {
        host: String,
        displays: Vec<HostDisplay>,
        #[serde(default)]
        request_id: Option<u32>,
    },
    HostApiContext {
        host: String,
        request_id: u32,
        context: moonlight_host_api::HostContext,
    },
    MonitorContexts { hosts: Vec<crate::monitor::Spec> },
    SessionApi {
        #[serde(flatten)]
        request: crate::host_api::SessionRequest,
    },
    QuitContext {
        host: String,
        request_id: u32,
        context: moonlight_host_api::HostContext,
        http_url: String,
    },
    PairContext {
        host: String,
        request_id: u32,
        context: moonlight_host_api::pairing::Context,
    },
    Pairing {
        host: String,
        pin: String,
    },
    Paired,
    Added,
    HostRenamed,
    HostRemoved {
        host: String,
    },
    AppQuit,
    Launching,
    Stage {
        stage: String,
    },
    StreamStarted,
    StreamFinished,
    Controls {
        active: bool,
        gamepad_mouse: bool,
        has_gamepad: bool,
    },
    ControlsNavigate {
        direction: String,
    },
    BitrateResult {
        request_id: u32,
        requested_kbps: u32,
        success: bool,
        message: String,
    },
    Error {
        code: String,
        message: String,
    },
}
pub struct Client {
    child_pid: u32,
    commands: Option<mpsc::Sender<Value>>,
    events: mpsc::Receiver<Event>,
    api: super::host_api::Client,
    next_api_request: AtomicU32,
}
impl Client {
    pub fn process_id(&self) -> u32 { self.child_pid }
    pub fn spawn() -> anyhow::Result<Self> {
        let exe = std::env::current_exe()?
            .parent()
            .unwrap()
            .join("Moonlight.exe");
        let profile = super::settings::preferences_path()
            .parent()
            .unwrap()
            .join("core");
        Self::spawn_at(exe, profile)
    }
    fn spawn_at(exe: std::path::PathBuf, profile: std::path::PathBuf) -> anyhow::Result<Self> {
        anyhow::ensure!(exe.is_file(), "請使用整合封裝：同一資料夾內需要 Moonlight.exe 核心。");
        std::fs::create_dir_all(&profile)?;
        let log = std::fs::File::create(profile.join("core-stderr.log"))?;
        let mut command = Command::new(exe);
        command
            .arg("--gpui-core")
            .env("MOONLIGHT_GPUI_PROFILE", &profile)
            .env("MOONLIGHT_GPUI_RUST_IDENTITY", "1")
            .env("MOONLIGHT_GPUI_RUST_MONITOR", "1")
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::from(log));
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000); // CREATE_NO_WINDOW, SDL still creates its stream window.
        }
        let (tx, rx) = mpsc::channel();
        let (events_tx, events) = mpsc::channel();
        let api = super::host_api::Client::spawn(events_tx.clone(), tx.clone())?;
        let mut child = command.spawn()?;
        let child_pid = child.id();
        let mut stdin = child.stdin.take().unwrap();
        let stdout = child.stdout.take().unwrap();
        let writer_errors = events_tx.clone();
        thread::spawn(move || {
            while let Ok(value) = rx.recv() {
                if serde_json::to_writer(&mut stdin, &value).is_err()
                    || stdin.write_all(b"\n").is_err()
                    || stdin.flush().is_err()
                {
                    let _ = writer_errors.send(Event::Error {
                        code: "pipe_write".into(),
                        message: "核心通訊中斷。".into(),
                    });
                    break;
                }
            }
            drop(stdin); // EOF asks the core to stop streaming and exit gracefully.
            let deadline = Instant::now() + Duration::from_secs(12);
            loop {
                match child.try_wait() {
                    Ok(Some(_)) => break,
                    Ok(None) if Instant::now() < deadline => {
                        thread::sleep(Duration::from_millis(50))
                    }
                    _ => {
                        let _ = child.kill();
                        let _ = child.wait();
                        break;
                    }
                }
            }
        });
        let reader_api = api.clone();
        thread::spawn(move || {
            let mut ready = false;
            for line in BufReader::new(stdout).lines() {
                let event = line.ok().and_then(|line| decode(&line).ok());
                if let Some(Event::Error { code, .. }) = &event {
                    if let Ok(mut log) = std::fs::OpenOptions::new().create(true).append(true).open(
                        super::settings::preferences_path().with_file_name("operation-errors.log"),
                    ) {
                        let _ = writeln!(
                            log,
                            "{:?} core_error={}",
                            std::time::SystemTime::now(),
                            code
                        );
                    }
                }
                match event {
                    Some(Event::ProfileIdentity { identity }) if !ready => {
                        if identity.install(&profile).is_err() {
                            let _ = events_tx.send(Event::Error {
                                code: "identity_store".into(),
                                message: "無法儲存配對身分。".into(),
                            });
                            break;
                        }
                    }
                    Some(Event::MonitorContexts {hosts}) if ready => reader_api.monitors(hosts),
                    Some(Event::SessionApi {request}) if ready => reader_api.session(request),
                    Some(Event::QuitContext {host, request_id, context, http_url}) if ready => {
                        reader_api.quit_context(host, request_id, context, http_url);
                    }
                    Some(event @ (Event::Error {..} | Event::Paired | Event::AppQuit)) if ready => {
                        reader_api.worker_reply();
                        if events_tx.send(event).is_err() { break; }
                    }
                    Some(Event::PairContext {host, request_id, context}) if ready => {
                        reader_api.pair_context(host, request_id, context);
                    }
                    Some(Event::HostApiContext {host, request_id, context}) if ready => {
                        reader_api.context(host, request_id, context);
                    }
                    Some(Event::Ready { qml_loaded: false }) if !ready => {
                        ready = true;
                        if events_tx.send(Event::Ready { qml_loaded: false }).is_err() {
                            break;
                        }
                    }
                    Some(event) if ready && !matches!(event, Event::ProfileIdentity { .. }) => {
                        if events_tx.send(event).is_err() {
                            break;
                        }
                    }
                    _ => {
                        let _ = events_tx.send(Event::Error {
                            code: "protocol".into(),
                            message: "核心版本或訊息格式不相容。".into(),
                        });
                        break;
                    }
                }
            }
            reader_api.stop();
            let _ = events_tx.send(Event::Error {
                code: "core_exited".into(),
                message: "核心程序已結束；請重新開啟 GPUI。".into(),
            });
        });
        Ok(Self {
            child_pid,
            commands: Some(tx),
            events,
            api,
            next_api_request: AtomicU32::new(1),
        })
    }
    pub fn send(&self, request: Value) -> anyhow::Result<()> {
        if request["command"] == "displays" || request["command"] == "pair" || request["command"] == "quit" {
            let pairing = request["command"] == "pair";
            let quitting = request["command"] == "quit";
            let host = request["host"].as_str().ok_or_else(|| anyhow::anyhow!("缺少主機識別碼"))?;
            let request_id = self.next_api_request.try_update(Ordering::Relaxed, Ordering::Relaxed,
                |id| id.checked_add(1).filter(|next| *next <= i32::MAX as u32))
                .map_err(|_| anyhow::anyhow!("請重新啟動主機控制服務"))?;
            if quitting { self.api.begin_quit(host.to_owned(), request_id)?; }
            else if pairing { self.api.begin_pair(host.to_owned(), request_id)?; }
            else { self.api.begin(host.to_owned(), request_id)?; }
            self.commands.as_ref().ok_or_else(|| anyhow::anyhow!("核心未啟動"))?
                .send(json!({"command":if quitting {"quit_rust"} else if pairing {"pair_rust"} else {"host_api_context"}, "host":host, "request_id":request_id}))?;
            return Ok(());
        }
        if request["command"] == "controls" && request["action"] == "resume" {
            // Let our existing worker restore its SDL window after local input.
            #[cfg(windows)]
            unsafe {
                AllowSetForegroundWindow(self.child_pid);
            }
        }
        self.commands
            .as_ref()
            .ok_or_else(|| anyhow::anyhow!("核心未啟動"))?
            .send(request)?;
        Ok(())
    }
    pub fn drain(&self) -> Vec<Event> {
        let latest = self.next_api_request.load(Ordering::Relaxed) - 1;
        self.events.try_iter().filter(|event| !matches!(event,
            Event::Displays {request_id: Some(id), ..} if *id != latest)).collect()
    }
}
#[cfg(windows)]
#[link(name = "user32")]
unsafe extern "system" {
    fn AllowSetForegroundWindow(process_id: u32) -> i32;
}
impl Drop for Client {
    fn drop(&mut self) {
        self.api.stop();
        if let Some(tx) = self.commands.take() {
            let _ = tx.send(json!({"command":"shutdown"}));
        }
    }
}
fn decode(line: &str) -> anyhow::Result<Event> {
    anyhow::ensure!(line.len() <= 4 * 1024 * 1024, "Oversized core event");
    let value: Value = serde_json::from_str(line)?;
    anyhow::ensure!(value["protocol"] == 1, "Unknown protocol");
    Ok(serde_json::from_value(value)?)
}
pub fn stream_settings(s: &super::settings::Settings) -> Value {
    let (width, height) = s.dimensions();
    json!({"width":width,"height":height,"fps":s.frame_rate(),"codec":s.codec,
        "bitrate":s.bitrate,"abr":s.abr,"fec":match s.fec {0=>-2,1=>-1,_=>s.fec_percent as i32},
        "enhancement":s.enhancement,"scale":s.scale,"scale_percent":s.scale_percent,
        "advanced":s.advanced_values(),"auto_resolution":s.resolution==5,"language":s.language,
        "display":{"window_mode":s.window_mode,"decoder":s.decoder,"hdr":s.hdr,"vsync":s.vsync,"frame_pacing":s.frame_pacing}})
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    #[ignore = "requires isolated add fixture and real worker; run check_add.py"]
    fn rust_add_through_real_worker() -> anyhow::Result<()> {
        let client=Client::spawn_at(std::env::var("RUST_HOST_API_WORKER")?.into(),std::env::var("RUST_HOST_API_PROFILE")?.into())?;
        let mode=std::env::var("RUST_ADD_MODE")?;
        let mut queued=std::collections::VecDeque::new();
        let mut wait=|predicate:&dyn Fn(&Event)->bool|->anyhow::Result<Event> {
            let deadline=Instant::now()+Duration::from_secs(20);
            while Instant::now()<deadline {
                queued.extend(client.drain());
                while let Some(event)=queued.pop_front() {
                    anyhow::ensure!(!matches!(event,Event::SessionApi {..}),"Private API request reached UI");
                    if predicate(&event) {return Ok(event);}
                    if let Event::Error {code,..}=event {anyhow::bail!("Unexpected worker error: {code}");}
                }
                thread::sleep(Duration::from_millis(10));
            }
            anyhow::bail!("Expected add result did not arrive")
        };
        let result=(||->anyhow::Result<()> {
            wait(&|event|matches!(event,Event::Ready {..}))?;
            client.send(json!({"command":"add","address":std::env::var("RUST_ADD_ADDRESS")?}))?;
            if mode=="cancel" {
                thread::sleep(Duration::from_millis(500));return Ok(());
            }
            client.send(json!({"command":"rename","host":"rust-add-host","name":"must-not-apply"}))?;
            wait(&|event|matches!(event,Event::Error {code,..} if code=="busy"))?;
            if mode=="bad-pin" || mode=="fail" {
                wait(&|event|matches!(event,Event::Error {code,..} if code=="add_failed"))?;
                client.send(json!({"command":"rename","host":"rust-add-host","name":"my-custom-name"}))?;
                if mode=="bad-pin" {wait(&|event|matches!(event,Event::HostRenamed))?;}
                else {wait(&|event|matches!(event,Event::Error {code,..} if code=="host_not_found"))?;}
            } else {
                wait(&|event|matches!(event,Event::Added))?;
                client.send(json!({"command":"list"}))?;
                let event=wait(&|event|matches!(event,Event::Hosts {hosts} if hosts.len()==1 && hosts[0].online))?;
                if let Event::Hosts {hosts}=event {
                    let host=&hosts[0];
                    anyhow::ensure!(host.id=="rust-add-host","Wrong host identity");
                    if mode=="existing" {anyhow::ensure!(host.name=="my-custom-name" && host.paired && host.apps[0].hidden,"Merge lost existing preferences or pin");}
                    else {anyhow::ensure!(host.name=="新增主機 🎮" && !host.paired,"New host metadata changed");}
                }
            }
            Ok(())
        })();
        client.send(json!({"command":"shutdown"}))?;
        wait(&|event|matches!(event,Event::Error {code,..} if code=="core_exited"))?;
        result
    }
    #[test]
    #[ignore = "requires an isolated host and Windows desktop; run check_bridge.py with launch-rejected"]
    fn rust_rejected_launch_releases_real_worker_session() -> anyhow::Result<()> {
        let client = Client::spawn_at(
            std::env::var("RUST_HOST_API_WORKER")?.into(),
            std::env::var("RUST_HOST_API_PROFILE")?.into(),
        )?;
        let wait = |mut accept: Box<dyn FnMut(Event) -> anyhow::Result<bool>>| {
            let deadline = Instant::now() + Duration::from_secs(15);
            while Instant::now() < deadline {
                for event in client.drain() {
                    if accept(event)? {
                        return Ok(());
                    }
                }
                thread::sleep(Duration::from_millis(10));
            }
            anyhow::bail!("Expected session event did not arrive")
        };
        wait(Box::new(|event| Ok(matches!(event, Event::Hosts { hosts }
            if hosts.iter().any(|host| host.online && host.paired && host.apps.iter().any(|app| app.id == 1))))))?;
        let mut settings = stream_settings(&crate::settings::Settings::default());
        settings["auto_resolution"] = json!(false);
        settings["codec"] = json!(1);
        settings["display"]["decoder"] = json!(2);
        for _ in 0..2 {
            client.send(json!({"command":"start","host":"rust-display-host","app":1,"settings":settings}))?;
            let mut launching = false;
            let mut rejected = false;
            wait(Box::new(move |event| {
                match event {
                    Event::Launching => launching = true,
                    Event::Error { code, message } => {
                        anyhow::ensure!(code == "launch_failed" && message.contains("fixture launch denied"),
                            "Unexpected launch error: {code}: {message}");
                        rejected = true;
                    }
                    Event::StreamFinished => {
                        anyhow::ensure!(launching && rejected, "Session finished before reporting the host rejection");
                        return Ok(true);
                    }
                    Event::StreamStarted => anyhow::bail!("Rejected launch started streaming"),
                    _ => {}
                }
                Ok(false)
            }))?;
        }
        client.send(json!({"command":"shutdown"}))?;
        wait(Box::new(|event| Ok(matches!(event, Event::Error {code,..} if code=="core_exited"))))
    }
    #[test]
    #[ignore = "requires isolated loopback host and a real worker; run check_bridge.py with quit"]
    fn rust_quit_through_real_worker_context() -> anyhow::Result<()> {
        let client = Client::spawn_at(std::env::var("RUST_HOST_API_WORKER")?.into(),
            std::env::var("RUST_HOST_API_PROFILE")?.into())?;
        let wait = |predicate: &dyn Fn(&Event)->bool| -> anyhow::Result<Event> {
            let deadline=Instant::now()+Duration::from_secs(15);
            while Instant::now()<deadline {
                for event in client.drain() {
                    anyhow::ensure!(!matches!(event,Event::QuitContext {..}),"Private quit context reached UI");
                    if predicate(&event) { return Ok(event); }
                    anyhow::ensure!(!matches!(event,Event::AppQuit),"Unexpected quit completion");
                    if let Event::Error {code,..} = event { anyhow::bail!("Unexpected error: {code}"); }
                }
                thread::sleep(Duration::from_millis(10));
            }
            anyhow::bail!("Expected quit event did not arrive")
        };
        let outcome=(|| -> anyhow::Result<()> {
            wait(&|event| matches!(event,Event::Hosts {hosts} if hosts.iter().any(|h| h.online && h.paired && h.current_app==42)))?;
            // A success response alone is insufficient: the server stays busy.
            client.send(json!({"command":"quit","host":"rust-display-host"}))?;
            client.send(json!({"command":"quit_complete","host":"rust-display-host","request_id":999,"error":""}))?;
            client.send(json!({"command":"rename","host":"rust-display-host","name":"must-not-apply"}))?;
            wait(&|event| matches!(event,Event::Error {code,..} if code=="busy"))?;
            let result=wait(&|event| matches!(event,Event::Error {code,..} if code=="quit_failed"))?;
            anyhow::ensure!(matches!(result,Event::Error {message,..} if message.contains("wasn't started")),"Ownership failure lost");
            // Failure must release the operation lock and allow a retry.
            std::fs::write(std::env::var("RUST_QUIT_PHASE")?,"accept")?;
            client.send(json!({"command":"quit","host":"rust-display-host"}))?;
            wait(&|event| matches!(event,Event::AppQuit))?;
            client.send(json!({"command":"list"}))?;
            wait(&|event| matches!(event,Event::Hosts {hosts} if hosts.iter().any(|h| h.current_app==0)))?;
            // Already-free hosts acknowledge without another mutation.
            client.send(json!({"command":"quit","host":"rust-display-host"}))?;
            wait(&|event| matches!(event,Event::AppQuit))?;
            // EOF must drop an in-flight mutation without issuing verification
            // or reporting success. The fixture signals when cancel is pending.
            let phase=std::path::PathBuf::from(std::env::var("RUST_QUIT_PHASE")?);
            std::fs::write(&phase,"restart")?;
            wait(&|event| matches!(event,Event::Hosts {hosts} if hosts.iter().any(|h| h.current_app==42)))?;
            std::fs::write(&phase,"hold")?;
            client.send(json!({"command":"quit","host":"rust-display-host"}))?;
            let deadline=Instant::now()+Duration::from_secs(5);
            while !phase.with_extension("started").exists() {
                anyhow::ensure!(Instant::now()<deadline,"Pending cancellation did not start");
                thread::sleep(Duration::from_millis(10));
            }
            Ok(())
        })();
        let _=client.send(json!({"command":"shutdown"}));
        wait(&|event| matches!(event,Event::Error {code,..} if code=="core_exited"))?;
        outcome
    }
    #[test]
    #[ignore = "requires isolated lifecycle fixture; run check_monitor.py"]
    fn rust_monitor_through_real_worker() -> anyhow::Result<()> {
        let client = Client::spawn_at(std::env::var("RUST_HOST_API_WORKER")?.into(),
            std::env::var("RUST_HOST_API_PROFILE")?.into())?;
        let wait_host = |predicate: &dyn Fn(&Host)->bool| -> anyhow::Result<Host> {
            let deadline=Instant::now()+Duration::from_secs(20);
            while Instant::now()<deadline {
                for event in client.drain() {
                    match event {
                        Event::Hosts {hosts} => if let Some(host)=hosts.into_iter().find(|host| predicate(host)) {return Ok(host);},
                        Event::Error {code,..} => anyhow::bail!("Worker error: {code}"),
                        Event::MonitorContexts {..} => anyhow::bail!("Private monitor context reached UI"),
                        _=>{}
                    }
                }
                thread::sleep(Duration::from_millis(20));
            }
            anyhow::bail!("Expected monitored host state did not arrive")
        };
        let outcome=(||->anyhow::Result<()> {
            let count=std::env::var("RUST_MONITOR_APP_COUNT")?.parse::<usize>()?;
            let host=wait_host(&|host| host.online && host.apps.len()==count)?;
            anyhow::ensure!(host.name=="custom-name" && host.paired && host.current_app==42,"Host metadata changed");
            anyhow::ensure!(host.apps.iter().take(3).map(|app| app.id).collect::<Vec<_>>()==[7,99,42],"App order changed");
            anyhow::ensure!(host.apps[0].hidden && host.apps[2].direct_launch && host.apps[2].name=="桌面 🎮","Client app preferences lost");
            let phase=std::env::var("RUST_MONITOR_PHASE")?;
            if std::env::var("RUST_MONITOR_CHECK_INTERVAL").as_deref()==Ok("1") {
                std::fs::write(&phase,"steady")?;
                thread::sleep(Duration::from_secs(31));
            }
            std::fs::write(&phase,"offline")?;
            let host=wait_host(&|host| !host.online)?;
            anyhow::ensure!(host.apps.len()==count && host.apps[2].direct_launch,"Offline state removed cached apps");
            std::fs::write(&phase,"recover")?;
            let host=wait_host(&|host| host.online)?;
            anyhow::ensure!(host.apps.len()==count && host.apps[2].direct_launch,"Reconnect lost cached preferences");
            client.send(json!({"command":"remove","host":"rust-monitor-host"}))?;
            let deadline=Instant::now()+Duration::from_secs(10);
            let mut removed=false;
            while Instant::now()<deadline && !removed {
                removed=client.drain().iter().any(|event| matches!(event,Event::HostRemoved {host} if host=="rust-monitor-host"));
                thread::sleep(Duration::from_millis(20));
            }
            anyhow::ensure!(removed,"Host removal did not finish");
            std::fs::write(&phase,"removed")?;
            thread::sleep(Duration::from_millis(3300));
            anyhow::ensure!(!client.drain().iter().any(|event| matches!(event,Event::Hosts {hosts} if !hosts.is_empty())),"Deleted host reappeared");
            Ok(())
        })();
        let _=client.send(json!({"command":"shutdown"}));
        let deadline=Instant::now()+Duration::from_secs(15);
        while Instant::now()<deadline {
            if client.drain().iter().any(|event| matches!(event,Event::Error {code,..} if code=="core_exited")) {return outcome;}
            thread::sleep(Duration::from_millis(20));
        }
        anyhow::bail!("Worker did not shut down")
    }
    #[test]
    #[ignore = "requires isolated pairing host and real worker; run check_bridge.py with pairing peer"]
    fn rust_pairing_through_real_worker_context() -> anyhow::Result<()> {
        let client = Client::spawn_at(std::env::var("RUST_HOST_API_WORKER")?.into(),
            std::env::var("RUST_HOST_API_PROFILE")?.into())?;
        let outcome = (|| -> anyhow::Result<()> {
            let deadline = Instant::now() + Duration::from_secs(15);
            loop {
                anyhow::ensure!(Instant::now() < deadline, "Unpaired host did not become ready");
                if client.drain().iter().any(|event| matches!(event, Event::Hosts {hosts} if hosts.iter().any(|host|
                    host.id == "rust-display-host" && host.online && !host.paired))) { break; }
                thread::sleep(Duration::from_millis(20));
            }
            client.send(json!({"command":"pair", "host":"rust-display-host"}))?;
            let expected = std::env::var("RUST_PAIRING_EXPECTED")?;
            let deadline = Instant::now() + Duration::from_secs(15);
            loop {
                anyhow::ensure!(Instant::now() < deadline, "Rust pairing did not complete");
                for event in client.drain() {
                    match event {
                        Event::Pairing {host, pin} => {
                            anyhow::ensure!(host == "rust-display-host" && pin.len() == 4 && pin.bytes().all(|b| b.is_ascii_digit()), "Invalid pairing PIN");
                            std::fs::write(std::env::var("RUST_PAIRING_PIN_FILE")?, pin)?;
                        }
                        Event::Paired => { anyhow::ensure!(expected == "paired", "Unexpected success"); return Ok(()); }
                        Event::Error {code, ..} => { anyhow::ensure!(code == expected, "Unexpected pairing error: {code}"); return Ok(()); }
                        Event::PairContext {..} => anyhow::bail!("Private context reached UI"),
                        _ => {}
                    }
                }
                thread::sleep(Duration::from_millis(20));
            }
        })();
        let _ = client.send(json!({"command":"shutdown"}));
        let deadline = Instant::now() + Duration::from_secs(15);
        while Instant::now() < deadline {
            if client.drain().iter().any(|event| matches!(event, Event::Error {code, ..} if code == "core_exited")) { return outcome; }
            thread::sleep(Duration::from_millis(20));
        }
        anyhow::bail!("Worker did not shut down")
    }
    #[test]
    fn display_requests_use_rust_and_discard_already_queued_old_results() {
        let (commands, incoming) = mpsc::channel();
        let (events_tx, events) = mpsc::channel();
        let api = super::super::host_api::Client::spawn(events_tx.clone(), commands.clone()).unwrap();
        let client = Client {child_pid:0, commands:Some(commands), events, api, next_api_request:AtomicU32::new(1)};
        client.send(json!({"command":"displays","host":"same"})).unwrap();
        assert_eq!(incoming.recv().unwrap(), json!({"command":"host_api_context","host":"same","request_id":1}));
        events_tx.send(Event::Displays {host:"same".into(), displays:Vec::new(), request_id:Some(1)}).unwrap();
        client.send(json!({"command":"displays","host":"same"})).unwrap();
        assert_eq!(incoming.recv().unwrap()["request_id"], 2);
        events_tx.send(Event::Displays {host:"same".into(), displays:Vec::new(), request_id:Some(2)}).unwrap();
        let results = client.drain();
        assert_eq!(results.len(), 1);
        assert!(matches!(results[0], Event::Displays {request_id:Some(2), ..}));
    }
    #[test]
    #[ignore = "requires isolated loopback host and a real worker; run check_bridge.py"]
    fn rust_displays_through_real_worker_context() -> anyhow::Result<()> {
        let client = Client::spawn_at(std::env::var("RUST_HOST_API_WORKER")?.into(),
            std::env::var("RUST_HOST_API_PROFILE")?.into())?;
        let outcome = (|| -> anyhow::Result<()> {
            let deadline = Instant::now() + Duration::from_secs(15);
            loop {
                anyhow::ensure!(Instant::now() < deadline, "Host did not become ready");
                if client.drain().iter().any(|event| matches!(event, Event::Hosts {hosts} if hosts.iter().any(|host|
                    host.id == "rust-display-host" && host.online && host.paired))) { break; }
                thread::sleep(Duration::from_millis(20));
            }
            client.send(json!({"command":"displays", "host":"rust-display-host"}))?;
            let deadline = Instant::now() + Duration::from_secs(10);
            loop {
                anyhow::ensure!(Instant::now() < deadline, "Rust displays did not complete");
                for event in client.drain() {
                    if let Event::Displays {host, displays, ..} = event {
                        if std::env::var("RUST_DISPLAY_HTTP_ERROR").as_deref() == Ok("1") {
                            anyhow::ensure!(host == "rust-display-host" && displays.is_empty(), "HTTP error accepted as a display list");
                            return Ok(());
                        }
                        anyhow::ensure!(host == "rust-display-host" && displays.len() == 2, "Incorrect display result");
                        anyhow::ensure!(displays[0].name == "家中螢幕 🎮" && displays[0].target == "screen-1"
                            && displays[1].name == "Display 2" && displays[1].target == "Display 2", "Display mapping changed");
                        return Ok(());
                    }
                }
                thread::sleep(Duration::from_millis(20));
            }
        })();
        let _ = client.send(json!({"command":"shutdown"}));
        let deadline = Instant::now() + Duration::from_secs(15);
        let mut exited = false;
        while Instant::now() < deadline {
            if client.drain().iter().any(|event| matches!(event, Event::Error {code, ..} if code == "core_exited")) {
                exited = true;
                break;
            }
            thread::sleep(Duration::from_millis(20));
        }
        anyhow::ensure!(exited, "Worker did not shut down");
        outcome
    }
    #[test]
    fn reject_unknown_protocol_and_non_json() {
        assert!(decode("{\"event\":\"ready\",\"qml_loaded\":false,\"protocol\":2}").is_err());
        assert!(decode("diagnostic output").is_err());
        assert!(matches!(
            decode("{\"event\":\"ready\",\"qml_loaded\":false,\"protocol\":1}").unwrap(),
            Event::Ready { qml_loaded: false }
        ));
        assert!(matches!(decode(r#"{"protocol":1,"event":"controls","active":true,"gamepad_mouse":true,"has_gamepad":true}"#).unwrap(),
            Event::Controls { active: true, gamepad_mouse: true, has_gamepad: true }));
        assert!(decode(r#"{"protocol":1,"event":"controls","active":true}"#).is_err());
        assert!(
            matches!(decode(r#"{"protocol":1,"event":"bitrate_result","request_id":7,"requested_kbps":24000,"success":false,"message":"拒絕變更"}"#).unwrap(),
            Event::BitrateResult { request_id: 7, requested_kbps: 24000, success: false, message } if message == "拒絕變更")
        );
        assert!(
            decode(
                r#"{"protocol":1,"event":"bitrate_result","request_id":7,"requested_kbps":24000}"#
            )
            .is_err()
        );
    }
    #[test]
    fn old_snapshots_default_to_visible_and_direct_launch_excludes_hidden_apps() {
        let mut host: Host = serde_json::from_value(json!({"id":"host","name":"我的電腦","address":"localhost","online":true,"paired":true,"current_app":0,"apps":[{"id":1,"name":"桌面"}]})).unwrap();
        assert!(!host.apps[0].hidden);
        assert_eq!(host.direct_app(), None);
        host.apps[0].direct_launch = true;
        assert_eq!(host.direct_app(), Some(1));
        host.apps[0].hidden = true;
        assert_eq!(host.direct_app(), None);
    }
    #[test]
    fn map_native_codec_fec_and_bitrate_units() {
        let value = stream_settings(&super::super::settings::Settings::default());
        assert_eq!(value["width"], 1920);
        assert_eq!(value["bitrate"], 20);
        assert_eq!(value["fec"], -2);
        assert_eq!(value["language"], 0);
        assert_eq!(value["display"]["window_mode"], 1);
        assert_eq!(value["advanced"]["desktop_mouse"], true);
        let configured = stream_settings(&super::super::settings::Settings {
            window_mode: 1,
            decoder: 2,
            hdr: true,
            vsync: false,
            frame_pacing: true,
            language: 2,
            ..super::super::settings::Settings::default()
        });
        assert_eq!(
            configured["display"],
            json!({"window_mode":1,"decoder":2,"hdr":true,"vsync":false,"frame_pacing":true})
        );
        assert_eq!(configured["language"], 2);
    }
    #[test]
    fn custom_display_and_retained_options_reach_the_worker() {
        let mut settings = super::super::settings::Settings {
            resolution: 4,
            fps: 5,
            ..Default::default()
        };
        for (key, value) in [
            ("custom_width", json!(3440)),
            ("custom_height", json!(1440)),
            ("custom_fps", json!(165)),
            ("host_display", json!("螢幕-2")),
            ("vdd", json!(2)),
            ("quit_host", json!(true)),
            ("hdr_min", json!(0.01)),
        ] {
            settings.advanced.insert(key.into(), value);
        }
        let request = stream_settings(&settings);
        assert_eq!(request["width"], 3440);
        assert_eq!(request["height"], 1440);
        assert_eq!(request["fps"], 165);
        assert_eq!(request["advanced"]["host_display"], "螢幕-2");
        assert_eq!(request["advanced"]["vdd"], 2);
        assert_eq!(request["advanced"]["quit_host"], true);
        assert_eq!(request["advanced"]["hdr_min"], 0.01);
        assert!(request["advanced"].get("microphone").is_none());
        settings.resolution = 5;
        assert_eq!(stream_settings(&settings)["auto_resolution"], true);
        assert!(matches!(decode(r#"{"protocol":1,"event":"displays","host":"h","displays":[{"name":"Display 2","target":"id-2"}]}"#).unwrap(), Event::Displays { .. }));
        assert!(matches!(
            decode(r#"{"protocol":1,"event":"host_removed","host":"h"}"#).unwrap(),
            Event::HostRemoved { .. }
        ));
    }
}
