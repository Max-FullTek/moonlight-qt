mod parsers;
use moonlight_host_api::transport;
use std::{
    io::{self, BufRead},
    path::Path,
    time::Duration,
};
use transport::{Error, Request};

fn certificate(path: &Path) -> Vec<u8> {
    rustls_pemfile::certs(&mut std::fs::read(path).unwrap().as_slice())
        .next()
        .unwrap()
        .unwrap()
        .as_ref()
        .to_vec()
}
#[tokio::main(flavor = "current_thread")]
async fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() == 6
        && matches!(
            args[5].as_str(),
            "status-fields" | "app-fields" | "status-discover" | "status-unpaired" | "quit"
        )
    {
        use moonlight_host_api::{HostContext, hosts};
        let fixtures = Path::new(&args[3]);
        let context = HostContext {
            base_url: if args[5] == "status-discover" {
                String::new()
            } else {
                args[1].clone()
            },
            server_certificate: if args[5] == "status-unpaired" {
                String::new()
            } else {
                std::fs::read_to_string(&args[2]).unwrap()
            },
            client_certificate: std::fs::read_to_string(fixtures.join("server.pem")).unwrap(),
            client_key: std::fs::read_to_string(fixtures.join("server-key.pem")).unwrap(),
            unique_id: "fixture-client".into(),
            client_name: "fixture-client-name".into(),
            nonce: String::new(),
        };
        let cancellation = moonlight_host_api::CancellationToken::new();
        if std::env::var_os("RUST_HOST_QUERY_CANCEL").is_some() {
            let token = cancellation.clone();
            tokio::spawn(async move {
                tokio::time::sleep(Duration::from_millis(100)).await;
                token.cancel();
            });
        }
        let result = if args[5] == "quit" {
            let http_url = format!("http://127.0.0.1:{}/", std::env::var("NATIVE_HTTP_FIXTURE_HTTP_PORT").unwrap());
            hosts::quit(context, &http_url, cancellation).await.map(|_| serde_json::Value::Null)
        } else if args[5] == "app-fields" {
            hosts::apps(context, cancellation)
                .await
                .map(|value| serde_json::to_value(value).unwrap())
        } else {
            let http_url = std::env::var("RUST_HOST_QUERY_HTTP_URL").unwrap_or_else(|_| {
                format!(
                    "http://127.0.0.1:{}/",
                    std::env::var("NATIVE_HTTP_FIXTURE_HTTP_PORT").unwrap()
                )
            });
            hosts::server_info(context, &http_url, true, cancellation)
                .await
                .map(|value| serde_json::to_value(value).unwrap())
        };
        println!(
            "{}",
            match result {
                Ok(value) => serde_json::json!({"ok":true,"value":value}),
                Err(hosts::Error::InvalidAppList) =>
                    serde_json::json!({"ok":false,"kind":"invalid_apps"}),
                Err(error) => serde_json::json!({"ok":false,"error":format!("{error:?}")}),
            }
        );
        return;
    }
    if args.len() == 6 && args[5].starts_with("pair-") {
        let fixtures = Path::new(&args[3]);
        let context = moonlight_host_api::pairing::Context {
            host: moonlight_host_api::HostContext {
                base_url: args[1].clone(),
                server_certificate: String::new(),
                client_certificate: std::fs::read_to_string(fixtures.join("server.pem")).unwrap(),
                client_key: std::fs::read_to_string(fixtures.join("server-key.pem")).unwrap(),
                unique_id: "fixture-client".into(),
                client_name: "fixture".into(),
                nonce: "0123456789abcdef0123456789abcdef".into(),
            },
            http_url: format!(
                "http://127.0.0.1:{}/",
                std::env::var("NATIVE_HTTP_FIXTURE_HTTP_PORT").unwrap()
            ),
            generation: args[5][5..].parse().unwrap(),
            pin: if std::env::var_os("NATIVE_HTTP_WRONG_PIN").is_some() {
                "0000"
            } else {
                "1234"
            }
            .into(),
        };
        let mut saved = String::new();
        let cancellation = moonlight_host_api::CancellationToken::new();
        let expected = std::env::var("RUST_PAIRING_EXPECTED_ERROR").unwrap_or_default();
        if expected == "cancel" {
            let token = cancellation.clone();
            tokio::spawn(async move {
                tokio::time::sleep(Duration::from_millis(100)).await;
                token.cancel();
            });
        }
        let result = moonlight_host_api::pairing::pair(context, cancellation, |name| {
            saved = name.to_owned()
        })
        .await;
        if !expected.is_empty() {
            use moonlight_host_api::pairing::Error as PairError;
            assert!(
                match (expected.as_str(), result) {
                    ("cancel", Err(PairError::Transport(Error::Cancelled))) => true,
                    ("invalid-xml", Err(PairError::Xml(_))) => true,
                    (mode, Err(PairError::Http(503))) if mode.starts_with("http-error-") => true,
                    _ => false,
                },
                "Unexpected pairing exception class"
            );
            println!("ok=1 error=2 status=0");
            return;
        }
        let result = result.unwrap();
        assert_eq!(
            result.certificate.is_some(),
            result.state == moonlight_host_api::pairing::State::Paired
        );
        if result.certificate.is_some() {
            assert_eq!(saved, "測試主機");
        }
        println!("ok=1 error={} status=0", result.state as u8);
        return;
    }
    if args.len() == 1 {
        for line in io::stdin().lock().lines() {
            let request: serde_json::Value = serde_json::from_str(&line.unwrap()).unwrap();
            let hex = request["hex"].as_str().unwrap().as_bytes();
            let body: Vec<u8> = hex
                .chunks_exact(2)
                .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
                .collect();
            println!(
                "{}",
                parsers::parse(request["kind"].as_str().unwrap(), &body)
            );
        }
        return;
    }
    assert!(
        (5..=6).contains(&args.len()),
        "URL pin fixtures identity [mode]"
    );
    let fixtures = Path::new(&args[3]);
    let mode = args.get(5).map(String::as_str).unwrap_or("");
    let mut request = Request {
        url: args[1].clone(),
        pin: certificate(Path::new(&args[2])),
        certificate: Vec::new(),
        key: Vec::new(),
        body: None,
        timeout: Some(Duration::from_millis(700)),
        max_bytes: 64,
        allow_unpaired_http: false,
        cancellation: Default::default(),
    };
    if args[4] == "1" {
        request.certificate = std::fs::read(fixtures.join("server.pem")).unwrap();
        request.key = std::fs::read(fixtures.join("server-key.pem")).unwrap();
    }
    match mode {
        "no-pin" => request.pin.clear(),
        "invalid-pin" => request.pin = vec![0],
        "partial-identity" => request.key.clear(),
        "exact-limit" => request.max_bytes = 2,
        "unpaired" => {
            request.allow_unpaired_http = true;
            request.pin.clear();
        }
        "unpaired-credentials" => request.allow_unpaired_http = true,
        "post" => request.body = Some("{\"message\":\"繁中\"}".as_bytes().to_vec()),
        "pre-cancel" => request.cancellation.cancel(),
        "cancel" => {
            request.timeout = None;
            let token = request.cancellation.clone();
            // Test-owned cancellation trigger. Transport itself adds no threads.
            std::thread::spawn(move || {
                let mut line = String::new();
                io::stdin().read_line(&mut line).unwrap();
                token.cancel();
            });
        }
        _ => {}
    }
    let mut response = transport::perform(&request).await;
    if mode == "replace-pin" || mode == "remove-identity" {
        assert_eq!(response.error, Error::None);
        if mode == "replace-pin" {
            request.pin = certificate(&fixtures.join("other.pem"));
        } else {
            request.certificate.clear();
            request.key.clear();
        }
        response = transport::perform(&request).await;
    }
    println!(
        "code={} status={} verified={} oversized={} bytes={} error={} cancelled={}",
        response.error as u8,
        response.status,
        u8::from(response.verified),
        u8::from(response.error == Error::ResponseTooLarge),
        response.body.len(),
        response.error as u8,
        u8::from(response.error == Error::Cancelled)
    );
}
