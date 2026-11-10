//! Host discovery/status and application queries. No profile or GUI ownership.
use crate::{CancellationToken, HostContext, transport, xml};
use serde::{Deserialize, Serialize};
use std::time::Duration;
pub fn https_base(http_url: &str, port: u16) -> Result<String, Error> {
    let mut url = base(http_url, "http")?;
    if port == 0 {
        return Err(Error::InvalidContext);
    }
    url.set_scheme("https").map_err(|_| Error::InvalidContext)?;
    url.set_port(Some(port))
        .map_err(|_| Error::InvalidContext)?;
    Ok(url.to_string())
}

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    InvalidContext,
    Transport(transport::Error),
    Http(u16),
    Redirect,
    Xml(xml::Error),
    InvalidAppList,
    InvalidJson,
    Rejected { code: i32, message: String },
}
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct App {
    pub id: i32,
    pub name: Option<String>,
    pub hdr: bool,
    pub collector: bool,
}
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct DisplayMode {
    pub width: i32,
    pub height: i32,
    pub refresh_rate: i32,
}
#[derive(Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct ServerInfo {
    pub name: String,
    pub uuid: String,
    pub mac: Vec<u8>,
    pub codec_support: i32,
    pub max_luma_hevc: i32,
    pub display_modes: Vec<DisplayMode>,
    pub local_ip: String,
    pub external_ip: String,
    pub https_port: u16,
    pub external_port: u16,
    pub nvidia: bool,
    pub paired: bool,
    pub current_app: i32,
    pub app_version: String,
    pub gfe_version: String,
    pub gpu: String,
}
// QString decimal conversions accept leading '+' and surrounding whitespace,
// require ASCII digits and return zero on overflow or trailing garbage.
fn integer(text: &str) -> i32 {
    text.trim().parse().unwrap_or(0)
}
fn port(text: &str, fallback: u16) -> u16 {
    text.trim()
        .parse::<u16>()
        .ok()
        .filter(|n| *n != 0)
        .unwrap_or(fallback)
}
pub fn parse_server_info(body: &[u8], http_port: u16) -> Result<ServerInfo, Error> {
    let document = xml::parse(body).map_err(Error::Xml)?;
    server_info_document(document, http_port)
}
fn server_info_document(document: xml::Document, http_port: u16) -> Result<ServerInfo, Error> {
    let field = |key: &str| document.fields.get(key).map(String::as_str).unwrap_or("");
    let mut modes: Vec<DisplayMode> = Vec::new();
    for (name, value, child) in &document.elements {
        if name == "DisplayMode" {
            modes.push(DisplayMode {
                width: 0,
                height: 0,
                refresh_rate: 0,
            });
        } else if let Some(mode) = modes.last_mut() {
            match name.as_str() {
                "Width" => mode.width = integer(value),
                "Height" => mode.height = integer(value),
                "RefreshRate" => mode.refresh_rate = integer(value),
                _ => {}
            }
            if *child && matches!(name.as_str(), "Width" | "Height" | "RefreshRate") {
                break;
            }
        }
    }
    modes.sort_by_key(|mode| {
        (mode.width as u64)
            .wrapping_mul(mode.height as u64)
            .wrapping_mul(mode.refresh_rate as u64)
    });
    let mac = if field("mac") == "00:00:00:00:00:00" {
        Vec::new()
    } else {
        field("mac")
            .split(':')
            .map(|part| {
                let part = part.trim();
                let (negative, digits) = if let Some(part) = part.strip_prefix('-') {
                    (true, part)
                } else {
                    (false, part.strip_prefix('+').unwrap_or(part))
                };
                let digits = digits
                    .strip_prefix("0x")
                    .or_else(|| digits.strip_prefix("0X"))
                    .unwrap_or(digits);
                let number = i64::from_str_radix(digits, 16).unwrap_or(i64::MAX);
                let number = if negative {
                    number.checked_neg()
                } else {
                    Some(number)
                };
                number.and_then(|n| i32::try_from(n).ok()).unwrap_or(0) as u8
            })
            .collect()
    };
    Ok(ServerInfo {
        name: if field("hostname").is_empty() {
            "UNKNOWN".into()
        } else {
            field("hostname").into()
        },
        uuid: field("uniqueid").into(),
        mac,
        codec_support: if field("ServerCodecModeSupport").is_empty() {
            1
        } else {
            integer(field("ServerCodecModeSupport"))
        },
        max_luma_hevc: integer(field("MaxLumaPixelsHEVC")),
        display_modes: modes,
        local_ip: if field("LocalIP").starts_with("127.") {
            String::new()
        } else {
            field("LocalIP").into()
        },
        external_ip: field("ExternalIP").into(),
        https_port: port(field("HttpsPort"), 47984),
        external_port: port(field("ExternalPort"), http_port),
        nvidia: field("state").contains("MJOLNIR"),
        paired: field("PairStatus") == "1",
        current_app: if field("state").ends_with("_SERVER_BUSY") {
            integer(field("currentgame"))
        } else {
            0
        },
        app_version: field("appversion").into(),
        gfe_version: field("GfeVersion").into(),
        gpu: field("gputype").into(),
    })
}
pub fn parse_apps(body: &[u8]) -> Result<Vec<App>, Error> {
    let document = xml::parse(body).map_err(Error::Xml)?;
    let mut apps: Vec<App> = Vec::new();
    for (name, value, child) in document.elements {
        if name == "App" {
            if apps
                .last()
                .is_some_and(|app| app.id == 0 || app.name.is_none())
            {
                return Err(Error::InvalidAppList);
            }
            apps.push(App {
                id: 0,
                name: None,
                hdr: false,
                collector: false,
            });
        } else if let Some(app) = apps.last_mut() {
            match name.as_str() {
                "AppTitle" => app.name = Some(value),
                "ID" => app.id = integer(&value),
                "IsHdrSupported" => app.hdr = value == "1",
                "IsAppCollectorGame" => app.collector = value == "1",
                _ => {}
            }
            if child
                && matches!(
                    name.as_str(),
                    "AppTitle" | "ID" | "IsHdrSupported" | "IsAppCollectorGame"
                )
            {
                break;
            }
        }
    }
    // Preserve NvHTTP's final-entry behavior, including an incomplete final
    // entry. Tightening that compatibility rule requires a separate decision.
    Ok(apps)
}
pub(crate) fn base(value: &str, scheme: &str) -> Result<reqwest::Url, Error> {
    let url = reqwest::Url::parse(value).map_err(|_| Error::InvalidContext)?;
    if url.scheme() != scheme
        || url.host_str().is_none()
        || !url.username().is_empty()
        || url.password().is_some()
        || url.query().is_some()
        || url.fragment().is_some()
        || url.path() != "/"
        || url.port_or_known_default() == Some(0)
    {
        return Err(Error::InvalidContext);
    }
    Ok(url)
}
async fn request(
    context: &HostContext,
    mut url: reqwest::Url,
    endpoint: &str,
    timeout: u64,
    cancellation: &CancellationToken,
) -> Result<Vec<u8>, Error> {
    let tls = url.scheme() == "https";
    let pin = if tls {
        rustls_pemfile::certs(&mut context.server_certificate.as_bytes())
            .next()
            .ok_or(Error::InvalidContext)?
            .map_err(|_| Error::InvalidContext)?
            .as_ref()
            .to_vec()
    } else {
        Vec::new()
    };
    let mut nonce = [0; 16];
    openssl::rand::rand_bytes(&mut nonce).map_err(|_| Error::InvalidContext)?;
    url.set_path(endpoint);
    url.query_pairs_mut()
        .append_pair("uniqueid", &context.unique_id)
        .append_pair("clientname", &context.client_name)
        .append_pair("uuid", &xml::hex(&nonce));
    let response = transport::perform(&transport::Request {
        url: url.to_string(),
        pin,
        certificate: if tls {
            context.client_certificate.as_bytes().to_vec()
        } else {
            Vec::new()
        },
        key: if tls {
            context.client_key.as_bytes().to_vec()
        } else {
            Vec::new()
        },
        body: None,
        timeout: Some(Duration::from_millis(timeout)),
        max_bytes: 0,
        allow_unpaired_http: !tls,
        cancellation: cancellation.clone(),
    })
    .await;
    if response.error != transport::Error::None {
        return Err(Error::Transport(response.error));
    }
    if (300..400).contains(&response.status) {
        return Err(Error::Redirect);
    }
    if response.status >= 400 {
        return Err(Error::Http(response.status));
    }
    Ok(response.body)
}
pub async fn apps(
    context: HostContext,
    cancellation: CancellationToken,
) -> Result<Vec<App>, Error> {
    let url = base(&context.base_url, "https")?;
    parse_apps(&request(&context, url, "/applist", 5000, &cancellation).await?)
}
/// The host may acknowledge cancel without terminating another client's
/// session. Verify server state just as NvHTTP::quitApp does; never retry a
/// mutation or downgrade its TLS request.
pub async fn quit(
    context: HostContext,
    http_url: &str,
    cancellation: CancellationToken,
) -> Result<(), Error> {
    let url = base(&context.base_url, "https")?;
    let body = request(&context, url, "/cancel", 30000, &cancellation).await?;
    let (_, code, message) = xml::parse_response(&body).map_err(Error::Xml)?;
    if code != 200 {
        return Err(Error::Rejected { code, message });
    }
    let http = base(http_url, "http")?;
    let http_port = http.port_or_known_default().ok_or(Error::InvalidContext)?;
    let (document, code, message) =
        pinned_server_response(&context, http, 5000, &cancellation).await?;
    if code != 200 {
        return Err(Error::Rejected { code, message });
    }
    let info = server_info_document(document, http_port)?;
    if info.current_app != 0 {
        return Err(Error::Xml(xml::Error::Status(599)));
    }
    Ok(())
}
/// Empty base_url means the HTTPS port has not been discovered. A stored pin
/// requires a pinned TLS request after HTTP port discovery; TLS failures never
/// permit downgrade. Only XML status 401 from the pinned peer permits recovery.
pub async fn server_info(
    mut context: HostContext,
    http_url: &str,
    fast_fail: bool,
    cancellation: CancellationToken,
) -> Result<ServerInfo, Error> {
    let http = base(http_url, "http")?;
    let http_port = http.port_or_known_default().ok_or(Error::InvalidContext)?;
    let timeout = if fast_fail { 2000 } else { 5000 };
    if context.server_certificate.is_empty() || context.base_url.is_empty() {
        let body = request(
            &context,
            http.clone(),
            "/serverinfo",
            timeout,
            &cancellation,
        )
        .await?;
        let info = parse_server_info(&body, http_port)?;
        if context.server_certificate.is_empty() {
            return Ok(info);
        }
        let mut https = http.clone();
        https
            .set_scheme("https")
            .map_err(|_| Error::InvalidContext)?;
        https
            .set_port(Some(info.https_port))
            .map_err(|_| Error::InvalidContext)?;
        context.base_url = https.to_string();
    }
    let (document, code, _) =
        pinned_server_response(&context, http, timeout, &cancellation).await?;
    if code != 200 {
        return Err(Error::Xml(xml::Error::Status(code)));
    }
    server_info_document(document, http_port)
}
async fn pinned_server_response(
    context: &HostContext,
    http: reqwest::Url,
    timeout: u64,
    cancellation: &CancellationToken,
) -> Result<(xml::Document, i32, String), Error> {
    let https = base(&context.base_url, "https")?;
    let body = request(context, https, "/serverinfo", timeout, cancellation).await?;
    let response = xml::parse_response(&body).map_err(Error::Xml)?;
    if response.1 == 401 {
        let body = request(context, http, "/serverinfo", timeout, cancellation).await?;
        return xml::parse_response(&body).map_err(Error::Xml);
    }
    Ok(response)
}
pub(crate) async fn session_info(
    context: HostContext,
    http_url: &str,
    cancel: CancellationToken,
) -> Result<serde_json::Value, Error> {
    let http = base(http_url, "http")?;
    let (doc, code, message) = pinned_server_response(&context, http, 2000, &cancel).await?;
    if code != 200 {
        return Err(Error::Rejected { code, message });
    }
    let field = |key: &str| doc.fields.get(key).map(String::as_str).unwrap_or("");
    Ok(
        serde_json::json!({"codec_support":if field("ServerCodecModeSupport").is_empty(){None}else{Some(integer(field("ServerCodecModeSupport")))},
        "app_version":field("appversion"),"gfe_version":field("GfeVersion"),
        "current_app":if field("state").ends_with("_SERVER_BUSY"){integer(field("currentgame"))}else{0}}),
    )
}
