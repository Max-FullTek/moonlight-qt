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
#[derive(Clone, Debug, Deserialize)]
pub struct HostDisplay {
    pub name: String,
    pub target: String,
}
#[derive(Debug, Deserialize)]
#[serde(tag = "event", rename_all = "snake_case")]
pub enum Event {
    Ready {
        qml_loaded: bool,
    },
    Hosts {
        hosts: Vec<Host>,
    },
    Displays {
        host: String,
        displays: Vec<HostDisplay>,
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
}
impl Client {
    pub fn process_id(&self) -> u32 { self.child_pid }
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
        let child_pid = child.id();
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
            child_pid,
            commands: Some(tx),
            events,
        })
    }
    pub fn send(&self, request: Value) -> anyhow::Result<()> {
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
        self.events.try_iter().collect()
    }
}
#[cfg(windows)]
#[link(name = "user32")]
unsafe extern "system" {
    fn AllowSetForegroundWindow(process_id: u32) -> i32;
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
