//! Host-control HTTP/TLS. The caller owns storage, runtime and cancellation.
use rustls::{
    DigitallySignedStruct, SignatureScheme,
    client::danger::{HandshakeSignatureValid, ServerCertVerified, ServerCertVerifier},
    crypto::{WebPkiSupportedAlgorithms, verify_tls12_signature, verify_tls13_signature},
    pki_types::{CertificateDer, ServerName, UnixTime},
};
use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};
use std::time::Duration;
use tokio_util::sync::CancellationToken;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Error {
    None = 0,
    InvalidRequest = 1,
    Setup = 2,
    Transport = 3,
    Timeout = 4,
    Cancelled = 5,
    ResponseTooLarge = 6,
    CertificateMismatch = 7,
}
pub struct Request {
    pub url: String,
    pub pin: Vec<u8>,
    pub certificate: Vec<u8>,
    pub key: Vec<u8>,
    pub body: Option<Vec<u8>>,
    pub timeout: Option<Duration>,
    pub max_bytes: usize,
    pub allow_unpaired_http: bool,
    pub cancellation: CancellationToken,
}
#[derive(Debug)]
pub struct Response {
    pub error: Error,
    pub status: u16,
    pub verified: bool,
    pub body: Vec<u8>,
}
#[derive(Debug)]
struct PairedLeaf {
    pin: Vec<u8>,
    algorithms: WebPkiSupportedAlgorithms,
    verified: Arc<AtomicBool>,
    mismatch: Arc<AtomicBool>,
}
impl ServerCertVerifier for PairedLeaf {
    fn verify_server_cert(
        &self,
        leaf: &CertificateDer<'_>,
        _: &[CertificateDer<'_>],
        _: &ServerName<'_>,
        _: &[u8],
        _: UnixTime,
    ) -> Result<ServerCertVerified, rustls::Error> {
        // Moonlight trusts the entire explicitly paired leaf, not a public CA,
        // DNS name, expiry or just its public key. Retain that trust model.
        let matches = leaf.as_ref() == self.pin;
        self.verified.store(matches, Ordering::Release);
        self.mismatch.store(!matches, Ordering::Release);
        if matches {
            Ok(ServerCertVerified::assertion())
        } else {
            Err(rustls::Error::InvalidCertificate(
                rustls::CertificateError::ApplicationVerificationFailure,
            ))
        }
    }
    fn verify_tls12_signature(
        &self,
        message: &[u8],
        cert: &CertificateDer<'_>,
        signature: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, rustls::Error> {
        verify_tls12_signature(message, cert, signature, &self.algorithms)
    }
    fn verify_tls13_signature(
        &self,
        message: &[u8],
        cert: &CertificateDer<'_>,
        signature: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, rustls::Error> {
        verify_tls13_signature(message, cert, signature, &self.algorithms)
    }
    fn supported_verify_schemes(&self) -> Vec<SignatureScheme> {
        self.algorithms.supported_schemes()
    }
}

fn client(
    request: &Request,
    verified: Arc<AtomicBool>,
    mismatch: Arc<AtomicBool>,
) -> Result<reqwest::Client, Error> {
    let url = reqwest::Url::parse(&request.url).map_err(|_| Error::InvalidRequest)?;
    if request.url.contains('\0')
        || url.host_str().is_none()
        || !url.username().is_empty()
        || url.password().is_some()
        || url.fragment().is_some()
    {
        return Err(Error::InvalidRequest);
    }
    let mut builder = reqwest::Client::builder()
        .no_proxy()
        .redirect(reqwest::redirect::Policy::none())
        .http1_only()
        .retry(reqwest::retry::never())
        .pool_max_idle_per_host(0);
    match url.scheme() {
        "http"
            if request.allow_unpaired_http
                && request.pin.is_empty()
                && request.certificate.is_empty()
                && request.key.is_empty() =>
        {
            // Even HTTP-only reqwest clients construct a TLS connector. Keep
            // its provider explicit and its CA roots empty as well.
            let config = rustls::ClientConfig::builder_with_provider(Arc::new(
                rustls::crypto::ring::default_provider(),
            ))
            .with_safe_default_protocol_versions()
            .map_err(|_| Error::Setup)?
            .with_root_certificates(rustls::RootCertStore::empty())
            .with_no_client_auth();
            builder = builder.tls_backend_preconfigured(config);
        }
        "https" => {
            let der = CertificateDer::from(request.pin.as_slice());
            rustls::server::ParsedCertificate::try_from(&der).map_err(|_| Error::InvalidRequest)?;
            if request.certificate.is_empty() != request.key.is_empty() {
                return Err(Error::InvalidRequest);
            }
            let provider = rustls::crypto::ring::default_provider();
            let verifier = PairedLeaf {
                pin: request.pin.clone(),
                algorithms: provider.signature_verification_algorithms,
                verified,
                mismatch,
            };
            let tls = rustls::ClientConfig::builder_with_provider(Arc::new(provider))
                .with_safe_default_protocol_versions()
                .map_err(|_| Error::Setup)?
                .dangerous()
                .with_custom_certificate_verifier(Arc::new(verifier));
            let config = if request.certificate.is_empty() {
                tls.with_no_client_auth()
            } else {
                let certificates = rustls_pemfile::certs(&mut request.certificate.as_slice())
                    .collect::<Result<Vec<_>, _>>()
                    .map_err(|_| Error::InvalidRequest)?;
                let key = rustls_pemfile::private_key(&mut request.key.as_slice())
                    .map_err(|_| Error::InvalidRequest)?
                    .ok_or(Error::InvalidRequest)?;
                tls.with_client_auth_cert(certificates, key)
                    .map_err(|_| Error::InvalidRequest)?
            };
            builder = builder.tls_backend_preconfigured(config);
        }
        _ => return Err(Error::InvalidRequest),
    }
    builder.build().map_err(|_| Error::Setup)
}

pub async fn perform(request: &Request) -> Response {
    let mut response = Response {
        error: Error::None,
        status: 0,
        verified: false,
        body: Vec::new(),
    };
    if request.cancellation.is_cancelled() {
        response.error = Error::Cancelled;
        return response;
    }
    let verified = Arc::new(AtomicBool::new(false));
    let mismatch = Arc::new(AtomicBool::new(false));
    let client = match client(request, verified.clone(), mismatch.clone()) {
        Ok(client) => client,
        Err(error) => {
            response.error = error;
            return response;
        }
    };
    let transfer = async {
        let builder = if let Some(body) = &request.body {
            client
                .post(&request.url)
                .header(reqwest::header::CONTENT_TYPE, "application/json")
                .body(body.clone())
        } else {
            client.get(&request.url)
        };
        let mut received = builder.send().await.map_err(|_| Error::Transport)?;
        response.status = received.status().as_u16();
        while let Some(chunk) = received.chunk().await.map_err(|_| Error::Transport)? {
            if request.max_bytes != 0
                && chunk.len() > request.max_bytes.saturating_sub(response.body.len())
            {
                return Err(Error::ResponseTooLarge);
            }
            response.body.extend_from_slice(&chunk);
        }
        Ok(())
    };
    let deadline = async {
        if let Some(timeout) = request.timeout {
            tokio::time::sleep(timeout).await
        } else {
            std::future::pending::<()>().await
        }
    };
    let result = tokio::select! {
        biased;
        _ = request.cancellation.cancelled() => Err(Error::Cancelled),
        _ = deadline => Err(Error::Timeout),
        result = transfer => result,
    };
    response.verified = verified.load(Ordering::Acquire);
    if let Err(error) = result {
        response.error = if mismatch.load(Ordering::Acquire) {
            Error::CertificateMismatch
        } else {
            error
        };
        response.body.clear(); // Never expose a truncated response on any failure.
    }
    response
}
