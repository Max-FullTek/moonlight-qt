//! Protocol 1 uses inherited anonymous pipes, never a listening network socket.
//! The matching Moonlight.exe must be beside this frontend. Its core flag skips
//! QML and uses an independent profile. Network/pairing/SDL stay in C++.
use serde::Deserialize;
use serde_json::{Value, json};
use std::{
    io::{BufRead, BufReader, Write},
    process::{Command, Stdio},
    sync::mpsc,
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
}
#[derive(Debug, Deserialize)]
#[serde(tag = "event", rename_all = "snake_case")]
pub enum Event {
    Ready { qml_loaded: bool },
    Hosts { hosts: Vec<Host> },
    Pairing { host: String, pin: String },
    Paired,
    Added,
    Launching,
    Stage { stage: String },
    StreamStarted,
    StreamFinished,
    Error { code: String, message: String },
}
pub struct Client {
    commands: Option<mpsc::Sender<Value>>,
    events: mpsc::Receiver<Event>,
}
impl Client {
    pub fn spawn() -> anyhow::Result<Self> {
        let exe = std::env::current_exe()?
            .parent()
            .unwrap()
            .join("Moonlight.exe");
        anyhow::ensure!(
            exe.is_file(),
            "請使用整合封裝：同一資料夾內需要 Moonlight.exe 核心。"
        );
        let profile = super::settings::preferences_path()
            .parent()
            .unwrap()
            .join("core");
        std::fs::create_dir_all(&profile)?;
        let log = std::fs::File::create(profile.join("core-stderr.log"))?;
        let mut command = Command::new(exe);
        command
            .arg("--gpui-core")
            .env("MOONLIGHT_GPUI_PROFILE", profile)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::from(log));
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000); // CREATE_NO_WINDOW, SDL still creates its stream window.
        }
        let mut child = command.spawn()?;
        let mut stdin = child.stdin.take().unwrap();
        let stdout = child.stdout.take().unwrap();
        let (tx, rx) = mpsc::channel();
        let (events_tx, events) = mpsc::channel();
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
        thread::spawn(move || {
            let mut ready = false;
            for line in BufReader::new(stdout).lines() {
                let event = line.ok().and_then(|line| decode(&line).ok());
                match event {
                    Some(Event::Ready { qml_loaded: false }) if !ready => {
                        ready = true;
                        if events_tx.send(Event::Ready { qml_loaded: false }).is_err() {
                            break;
                        }
                    }
                    Some(event) if ready => {
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
            let _ = events_tx.send(Event::Error {
                code: "core_exited".into(),
                message: "核心程序已結束；請重新開啟 GPUI。".into(),
            });
        });
        Ok(Self {
            commands: Some(tx),
            events,
        })
    }
    pub fn send(&self, request: Value) -> anyhow::Result<()> {
        self.commands
            .as_ref()
            .ok_or_else(|| anyhow::anyhow!("核心未啟動"))?
            .send(request)?;
        Ok(())
    }
    pub fn drain(&self) -> Vec<Event> {
        self.events.try_iter().collect()
    }
}
impl Drop for Client {
    fn drop(&mut self) {
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
    let (width, height) = [(1280, 720), (1920, 1080), (2560, 1440), (3840, 2160)][s.resolution];
    json!({"width":width,"height":height,"fps":[30,60,90,120,144][s.fps],"codec":s.codec,
        "bitrate":s.bitrate,"abr":s.abr,"fec":match s.fec {0=>-2,1=>-1,_=>s.fec_percent as i32},
        "enhancement":s.enhancement,"scale":s.scale,"scale_percent":s.scale_percent})
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn reject_unknown_protocol_and_non_json() {
        assert!(decode("{\"event\":\"ready\",\"qml_loaded\":false,\"protocol\":2}").is_err());
        assert!(decode("diagnostic output").is_err());
        assert!(matches!(
            decode("{\"event\":\"ready\",\"qml_loaded\":false,\"protocol\":1}").unwrap(),
            Event::Ready { qml_loaded: false }
        ));
    }
    #[test]
    fn map_native_codec_fec_and_bitrate_units() {
        let value = stream_settings(&super::super::settings::Settings::default());
        assert_eq!(value["width"], 1920);
        assert_eq!(value["bitrate"], 20);
        assert_eq!(value["fec"], -2);
    }
}
