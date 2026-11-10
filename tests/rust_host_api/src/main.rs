mod parsers;
mod transport;
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
