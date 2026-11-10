//! Streaming control protocol. C++ keeps media configuration and scheduling;
//! this module owns endpoint selection, encoding, HTTP/TLS and reply parsing.
use crate::{CancellationToken, HostContext, hosts, transport, xml};
use serde::Deserialize;
use serde_json::{Value, json};
use std::time::Duration;

#[derive(Deserialize)]
#[serde(tag = "operation", rename_all = "snake_case", deny_unknown_fields)]
pub enum Operation {
    HostInfo {
        http_url: String,
    },
    Launch {
        params: Launch,
    },
    Info {
        http_url: String,
    },
    Quit {
        http_url: String,
    },
    AbrCapabilities,
    AbrConfigure {
        enabled: bool,
        min_bitrate: i32,
        max_bitrate: i32,
        mode: String,
        timeout_ms: u32,
    },
    AbrFeedback {
        packet_loss: Option<f64>,
        rtt_ms: Option<f64>,
        decode_fps: Option<f64>,
        dropped_frames: i32,
        current_bitrate: i32,
        timeout_ms: u32,
    },
    Bitrate {
        bitrate: i32,
        client_name: String,
    },
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Launch {
    pub resume: bool,
    pub is_gfe: bool,
    pub app_id: i32,
    pub width: i32,
    pub height: i32,
    pub fps: i32,
    pub original_width: i32,
    pub original_height: i32,
    pub remote_resolution: bool,
    pub remote_width: i32,
    pub remote_height: i32,
    pub remote_fps: bool,
    pub remote_fps_rate: i32,
    pub sops: bool,
    pub input_key: [u8; 16],
    pub input_key_id: i32,
    pub ten_bit: bool,
    pub hdr_mode: i32,
    pub local_audio: bool,
    pub surround_audio: i32,
    pub gamepad_mask: i32,
    pub persist_gamepads: bool,
    pub screen_mode: i32,
    pub use_vdd: Option<bool>,
    pub display: String,
    pub max_brightness: f32,
    pub min_brightness: f32,
    pub average_brightness: f32,
    pub sdr_white: f32,
    pub core_query: String,
}
fn base(context: &HostContext, endpoint: &str) -> Result<reqwest::Url, hosts::Error> {
    let mut url = hosts::base(&context.base_url, "https")?;
    url.set_path(endpoint);
    Ok(url)
}
fn identity(url: &mut reqwest::Url, context: &HostContext) -> Result<(), hosts::Error> {
    let mut nonce = [0; 16];
    openssl::rand::rand_bytes(&mut nonce).map_err(|_| hosts::Error::InvalidContext)?;
    url.query_pairs_mut()
        .append_pair("uniqueid", &context.unique_id)
        .append_pair("uuid", &xml::hex(&nonce))
        .append_pair("clientname", &context.client_name);
    Ok(())
}
fn launch_url(context: &HostContext, p: &Launch) -> Result<reqwest::Url, hosts::Error> {
    let mut url = base(context, if p.resume { "/resume" } else { "/launch" })?;
    identity(&mut url, context)?;
    let (mut width, mut height) = (p.width, p.height);
    let mut fps = if p.is_gfe && p.fps > 60 { 0 } else { p.fps };
    if p.original_width > 0 && p.original_height > 0 {
        width = p.original_width;
        height = p.original_height;
    }
    if p.remote_resolution {
        if p.remote_width > 0 {
            width = p.remote_width;
        }
        if p.remote_height > 0 {
            height = p.remote_height;
        }
    }
    if p.remote_fps && p.remote_fps_rate > 0 {
        fps = p.remote_fps_rate;
    }
    let mut q = url.query_pairs_mut();
    for (key, value) in [
        ("appid", p.app_id.to_string()),
        ("mode", format!("{width}x{height}x{fps}")),
        ("additionalStates", "1".into()),
        ("sops", u8::from(p.sops).to_string()),
        ("rikey", xml::hex(&p.input_key)),
        ("rikeyid", p.input_key_id.to_string()),
    ] {
        q.append_pair(key, &value);
    }
    if p.ten_bit {
        q.append_pair("hdrMode", &p.hdr_mode.to_string())
            .append_pair("clientHdrCapVersion", "0")
            .append_pair("clientHdrCapSupportedFlagsInUint32", "0")
            .append_pair("clientHdrCapMetaDataId", "NV_STATIC_METADATA_TYPE_1")
            .append_pair("clientHdrCapDisplayData", "0x0x0x0x0x0x0x0x0x0x0");
    }
    for (key, value) in [
        ("localAudioPlayMode", u8::from(p.local_audio).to_string()),
        ("surroundAudioInfo", p.surround_audio.to_string()),
        ("remoteControllersBitmap", p.gamepad_mask.to_string()),
        ("gcmap", p.gamepad_mask.to_string()),
        ("gcpersist", u8::from(p.persist_gamepads).to_string()),
    ] {
        q.append_pair(key, &value);
    }
    if p.screen_mode != -1 {
        q.append_pair("customScreenMode", &p.screen_mode.to_string());
    }
    if let Some(vdd) = p.use_vdd {
        q.append_pair("useVdd", if vdd { "1" } else { "0" });
    }
    if !p.display.is_empty() {
        q.append_pair("display_name", &p.display);
    }
    if p.max_brightness > 0.0 {
        q.append_pair("maxBrightness", &format!("{:.3}", p.max_brightness))
            .append_pair("minBrightness", &format!("{:.6}", p.min_brightness))
            .append_pair(
                "maxAverageBrightness",
                &format!("{:.3}", p.average_brightness),
            );
    }
    if (50.0..=1000.0).contains(&p.sdr_white) {
        q.append_pair("sdrBrightness", &p.sdr_white.round().to_string());
    }
    drop(q);
    // common-c owns this extension; it contains protocol parameters, not a URL.
    for (key, value) in reqwest::Url::parse(&format!(
        "https://localhost/?{}",
        p.core_query.trim_start_matches('&')
    ))
    .map_err(|_| hosts::Error::InvalidContext)?
    .query_pairs()
    {
        url.query_pairs_mut().append_pair(&key, &value);
    }
    Ok(url)
}
async fn transfer(
    context: &HostContext,
    url: reqwest::Url,
    body: Option<Value>,
    timeout: u32,
    cancel: CancellationToken,
) -> Result<Vec<u8>, hosts::Error> {
    if timeout == 0 || timeout > 120000 {
        return Err(hosts::Error::InvalidContext);
    }
    let pin = rustls_pemfile::certs(&mut context.server_certificate.as_bytes())
        .next()
        .ok_or(hosts::Error::InvalidContext)?
        .map_err(|_| hosts::Error::InvalidContext)?
        .as_ref()
        .to_vec();
    let json_get = body.is_none() && url.path() == "/api/abr/capabilities";
    let request = transport::Request {
        url: url.to_string(),
        pin,
        certificate: context.client_certificate.as_bytes().to_vec(),
        key: context.client_key.as_bytes().to_vec(),
        body: body.map(|v| serde_json::to_vec(&v).unwrap()),
        timeout: Some(Duration::from_millis(timeout as u64)),
        max_bytes: 0,
        allow_unpaired_http: false,
        cancellation: cancel,
    };
    let response = if json_get {
        transport::perform_json_get(&request).await
    } else {
        transport::perform(&request).await
    };
    if response.error != transport::Error::None {
        return Err(hosts::Error::Transport(response.error));
    }
    if (300..400).contains(&response.status) {
        return Err(hosts::Error::Redirect);
    }
    if response.status >= 400 {
        return Err(hosts::Error::Http(response.status));
    }
    Ok(response.body)
}
pub async fn execute(
    context: HostContext,
    op: Operation,
    cancel: CancellationToken,
) -> Result<Value, hosts::Error> {
    let (url, body, timeout) = match op {
        Operation::HostInfo { http_url } => {
            // Preserve the one delayed retry for a host whose HTTP service is
            // visible before it is ready. Shutdown interrupts the delay.
            let result =
                hosts::server_info(context.clone(), &http_url, false, cancel.clone()).await;
            let info = if matches!(result, Err(hosts::Error::Http(503))) {
                tokio::select! {
                    _=cancel.cancelled()=>return Err(hosts::Error::Transport(transport::Error::Cancelled)),
                    _=tokio::time::sleep(Duration::from_secs(5))=>{}
                }
                hosts::server_info(context, &http_url, false, cancel).await?
            } else {
                result?
            };
            return Ok(serde_json::to_value(info).unwrap());
        }
        Operation::Quit { http_url } => {
            hosts::quit(context, &http_url, cancel).await?;
            return Ok(json!({}));
        }
        Operation::Info { http_url } => {
            return hosts::session_info(context, &http_url, cancel).await;
        }
        Operation::Launch { params } => {
            let url = launch_url(&context, &params)?;
            // Existing NvHTTP uses 120 seconds for both launch and resume.
            let bytes = transfer(&context, url, None, 120000, cancel).await?;
            let (doc, code, message) = xml::parse_response(&bytes).map_err(hosts::Error::Xml)?;
            if code != 200 {
                return Err(hosts::Error::Rejected { code, message });
            }
            return Ok(
                json!({"session_url":doc.fields.get("sessionUrl0").cloned().unwrap_or_default()}),
            );
        }
        Operation::Bitrate {
            bitrate,
            client_name,
        } => {
            let mut url = base(&context, "/bitrate")?;
            identity(&mut url, &context)?;
            url.query_pairs_mut()
                .append_pair("bitrate", &bitrate.to_string())
                .append_pair("clientname", &client_name);
            // Legacy API accepts any successful HTTP body; do not invent a JSON
            // success requirement that the existing runtime control lacks.
            transfer(&context, url, None, 5000, cancel).await?;
            return Ok(json!({}));
        }
        Operation::AbrCapabilities => (base(&context, "/api/abr/capabilities")?, None, 2000),
        Operation::AbrConfigure {
            enabled,
            min_bitrate,
            max_bitrate,
            mode,
            timeout_ms,
        } => {
            let body = if enabled {
                json!({"enabled":true,"minBitrate":min_bitrate,"maxBitrate":max_bitrate,"mode":mode})
            } else {
                json!({"enabled":false})
            };
            (base(&context, "/api/abr")?, Some(body), timeout_ms)
        }
        Operation::AbrFeedback {
            packet_loss,
            rtt_ms,
            decode_fps,
            dropped_frames,
            current_bitrate,
            timeout_ms,
        } => (
            base(&context, "/api/abr/feedback")?,
            Some(
                json!({"packetLoss":packet_loss,"rttMs":rtt_ms,"decodeFps":decode_fps,"droppedFrames":dropped_frames,"currentBitrate":current_bitrate}),
            ),
            timeout_ms,
        ),
    };
    let bytes = transfer(&context, url, body, timeout, cancel).await?;
    let value = crate::json_object(&bytes).map_err(|_| hosts::Error::InvalidJson)?;
    // Return only fields used by the media owner, preserving typed fallbacks.
    let mut result = serde_json::Map::new();
    for key in [
        "supported",
        "success",
        "hostMaxBitrate",
        "initialBitrate",
        "maxBitrate",
        "newBitrate",
        "reason",
    ] {
        if let Some(value) = value.get(key) {
            result.insert(key.into(), value.clone());
        }
    }
    Ok(Value::Object(result))
}
