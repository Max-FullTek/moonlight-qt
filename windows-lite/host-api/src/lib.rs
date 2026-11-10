//! Windows Lite control API. No profile access, GUI, worker or media transport.
pub mod pairing;
pub mod transport;
pub mod xml;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::{fmt, time::Duration};
pub use tokio_util::sync::CancellationToken;

// Temporary read-only context from the current profile owner. It is consumed
// per operation, never persisted, logged or returned to the C++ HTTP layer.
#[derive(Deserialize)]
pub struct HostContext {
    pub base_url: String,
    pub server_certificate: String,
    pub client_certificate: String,
    pub client_key: String,
    pub unique_id: String,
    pub client_name: String,
    pub nonce: String,
}
impl fmt::Debug for HostContext {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("HostContext").finish_non_exhaustive()
    }
}
#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct Display {
    pub name: String,
    pub target: String,
}
#[derive(Debug, PartialEq, Eq)]
pub enum ApiError {
    Transport(transport::Error),
    InvalidContext,
    Redirect,
    InvalidResponse,
    HostStatus,
}
fn integer(value: &Value) -> i32 {
    value
        .as_f64()
        .filter(|number| {
            number.is_finite()
                && number.fract() == 0.0
                && *number >= i32::MIN as f64
                && *number <= i32::MAX as f64
        })
        .map_or(0, |number| number as i32)
}
pub fn json_object(body: &[u8]) -> Result<Value, ApiError> {
    let body = body.strip_prefix(b"\xef\xbb\xbf").unwrap_or(body);
    let value: Value = serde_json::from_slice(body).map_err(|_| ApiError::InvalidResponse)?;
    if !value.is_object() {
        return Err(ApiError::InvalidResponse);
    }
    Ok(value)
}
pub fn parse_displays(body: &[u8]) -> Result<Vec<Display>, ApiError> {
    let root = json_object(body)?;
    if integer(&root["status_code"]) != 200 {
        return Err(ApiError::HostStatus);
    }
    let Some(items) = root["displays"].as_array() else {
        return Ok(Vec::new());
    };
    Ok(items
        .iter()
        .enumerate()
        .map(|(index, display)| {
            let display_name = display["display_name"].as_str().unwrap_or("");
            let mut friendly = display["friendly_name"].as_str().unwrap_or("").to_owned();
            if friendly.is_empty() {
                friendly = display_name.to_owned();
            }
            if friendly.is_empty() {
                friendly = format!("Display {}", index + 1);
            }
            let mut target = display["device_id"].as_str().unwrap_or("").to_owned();
            if target.is_empty() {
                target = if display_name.is_empty() {
                    friendly.clone()
                } else {
                    display_name.to_owned()
                };
            }
            Display {
                name: friendly,
                target,
            }
        })
        .collect())
}
fn display_request(
    context: HostContext,
    cancellation: CancellationToken,
) -> Result<transport::Request, ApiError> {
    let mut url = reqwest::Url::parse(&context.base_url).map_err(|_| ApiError::InvalidContext)?;
    if url.scheme() != "https"
        || url.host_str().is_none()
        || url.port_or_known_default().is_none()
        || !url.username().is_empty()
        || url.password().is_some()
        || url.query().is_some()
        || url.fragment().is_some()
        || url.path() != "/"
    {
        return Err(ApiError::InvalidContext);
    }
    url.set_path("/displays");
    url.query_pairs_mut()
        .append_pair("uniqueid", &context.unique_id)
        .append_pair("uuid", &context.nonce)
        .append_pair("clientname", &context.client_name);
    let pin = rustls_pemfile::certs(&mut context.server_certificate.as_bytes())
        .next()
        .ok_or(ApiError::InvalidContext)?
        .map_err(|_| ApiError::InvalidContext)?;
    Ok(transport::Request {
        url: url.to_string(),
        pin: pin.as_ref().to_vec(),
        certificate: context.client_certificate.into_bytes(),
        key: context.client_key.into_bytes(),
        body: None,
        timeout: Some(Duration::from_secs(5)),
        max_bytes: 0,
        allow_unpaired_http: false,
        cancellation,
    })
}
pub async fn displays(
    context: HostContext,
    cancellation: CancellationToken,
) -> Result<Vec<Display>, ApiError> {
    let request = display_request(context, cancellation)?;
    let response = transport::perform(&request).await;
    if response.error != transport::Error::None {
        return Err(ApiError::Transport(response.error));
    }
    if (300..400).contains(&response.status) {
        return Err(ApiError::Redirect);
    }
    if response.status >= 400 {
        return Err(ApiError::HostStatus);
    }
    // A successful HTTP response still requires the host JSON status_code.
    parse_displays(&response.body)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn display_fallbacks_and_invalid_fields_match_existing_owner() {
        let parsed = parse_displays(br#"{"status_code":200,"displays":[{"display_name":"screen","friendly_name":"friendly","device_id":"id"},{"display_name":"screen2"},{},null]}"#).unwrap();
        assert_eq!(
            parsed,
            vec![
                Display {
                    name: "friendly".into(),
                    target: "id".into()
                },
                Display {
                    name: "screen2".into(),
                    target: "screen2".into()
                },
                Display {
                    name: "Display 3".into(),
                    target: "Display 3".into()
                },
                Display {
                    name: "Display 4".into(),
                    target: "Display 4".into()
                }
            ]
        );
        assert!(
            parse_displays(br#"{"status_code":200}"#)
                .unwrap()
                .is_empty()
        );
        for body in [
            br#"{"status_code":4294967496}"#.as_slice(),
            br#"{"status_code":"200"}"#,
            br#"{"status_code":200.5}"#,
        ] {
            assert_eq!(parse_displays(body), Err(ApiError::HostStatus));
        }
    }
    #[test]
    fn strict_json_and_bom() {
        assert!(json_object(b"\xef\xbb\xbf{\"status_code\":200}").is_ok());
        for body in [
            b"{\"text\":\"\xff\"}".as_slice(),
            br#"{"text":"\q"}"#,
            br#"{"text":"\ud800"}"#,
            b"[]",
        ] {
            assert_eq!(json_object(body), Err(ApiError::InvalidResponse));
        }
        let body = format!("{{\"x\":{}0{}}}", "[".repeat(128), "]".repeat(128));
        assert_eq!(json_object(body.as_bytes()), Err(ApiError::InvalidResponse));
    }
    #[test]
    fn context_debug_never_prints_credentials() {
        let context = HostContext {
            base_url: "secret".into(),
            server_certificate: "secret".into(),
            client_certificate: "secret".into(),
            client_key: "secret".into(),
            unique_id: "secret".into(),
            client_name: "secret".into(),
            nonce: "secret".into(),
        };
        assert_eq!(format!("{context:?}"), "HostContext { .. }");
    }
}
