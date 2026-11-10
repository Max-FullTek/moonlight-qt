//! Rust-owned five-stage GameStream pairing; established OpenSSL primitives,
//! reqwest/rustls transport, no profile I/O or C++ callbacks.
use crate::{CancellationToken, HostContext, transport, xml};
use openssl::{
    hash::{MessageDigest, hash},
    memcmp,
    pkey::{PKey, Private},
    rand::rand_bytes,
    sign::{Signer, Verifier},
    symm::{Cipher, Crypter, Mode},
    x509::X509,
};
use std::time::Duration;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum State {
    Paired = 0,
    PinWrong = 1,
    Failed = 2,
    AlreadyInProgress = 3,
}
#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Crypto,
    InvalidContext,
    Transport(transport::Error),
    Xml(xml::Error),
    Redirect,
    Http(u16),
}
#[derive(serde::Deserialize)]
pub struct Context {
    pub host: HostContext,
    pub http_url: String,
    pub generation: i32,
    pub pin: String,
}
impl std::fmt::Debug for Context {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PairingContext").finish_non_exhaustive()
    }
}
pub struct Outcome {
    pub state: State,
    pub certificate: Option<String>,
}
impl Outcome {
    fn failed(state: State) -> Self {
        Self {
            state,
            certificate: None,
        }
    }
}
fn crypto<T>(result: Result<T, openssl::error::ErrorStack>) -> Result<T, Error> {
    result.map_err(|_| Error::Crypto)
}
pub fn derive_key(salt: &[u8], pin: &str, generation: i32) -> Result<Vec<u8>, Error> {
    if salt.len() != 16 {
        return Err(Error::Crypto);
    }
    let mut input = salt.to_vec();
    input.extend_from_slice(pin.as_bytes());
    Ok(crypto(hash(digest(generation), &input))?[..16].to_vec())
}
fn digest(generation: i32) -> MessageDigest {
    if generation >= 7 {
        MessageDigest::sha256()
    } else {
        MessageDigest::sha1()
    }
}
fn random() -> Result<Vec<u8>, Error> {
    let mut bytes = vec![0; 16];
    crypto(rand_bytes(&mut bytes))?;
    Ok(bytes)
}
fn cipher(input: &[u8], key: &[u8], mode: Mode) -> Result<Vec<u8>, Error> {
    if input.is_empty() || input.len() % 16 != 0 {
        return Ok(Vec::new());
    }
    let mut crypter = crypto(Crypter::new(Cipher::aes_128_ecb(), mode, key, None))?;
    crypter.pad(false);
    let mut output = vec![0; input.len() + 16];
    let count = crypto(crypter.update(input, &mut output))?;
    let tail = crypto(crypter.finalize(&mut output[count..]))?;
    output.truncate(count + tail);
    Ok(output)
}
fn sign(input: &[u8], key: &PKey<Private>) -> Result<Vec<u8>, Error> {
    let mut signer = crypto(Signer::new(MessageDigest::sha256(), key))?;
    crypto(signer.update(input))?;
    crypto(signer.sign_to_vec())
}
fn verify(input: &[u8], signature: &[u8], certificate: &X509) -> Result<bool, Error> {
    let key = crypto(certificate.public_key())?;
    let mut verifier = crypto(Verifier::new(MessageDigest::sha256(), &key))?;
    crypto(verifier.update(input))?;
    Ok(verifier.verify(signature).unwrap_or(false))
}
struct Peer {
    context: Context,
    pin: Vec<u8>,
    cancellation: CancellationToken,
}
impl Peer {
    async fn request(
        &self,
        tls: bool,
        endpoint: &str,
        args: &str,
        timeout: u64,
    ) -> Result<xml::Document, Error> {
        let base = if tls {
            &self.context.host.base_url
        } else {
            &self.context.http_url
        };
        let mut url = reqwest::Url::parse(base).map_err(|_| Error::InvalidContext)?;
        if url.scheme() != if tls { "https" } else { "http" }
            || url.host_str().is_none()
            || !url.username().is_empty()
            || url.password().is_some()
            || url.query().is_some()
            || url.fragment().is_some()
            || url.path() != "/"
        {
            return Err(Error::InvalidContext);
        }
        url.set_path(endpoint);
        let nonce = xml::hex(&random()?);
        url.query_pairs_mut()
            .append_pair("uniqueid", &self.context.host.unique_id)
            .append_pair("uuid", &nonce)
            .append_pair("clientname", &self.context.host.client_name);
        let query = format!(
            "{}{}{}",
            url.query().unwrap_or(""),
            if args.is_empty() { "" } else { "&" },
            args
        );
        url.set_query(Some(&query));
        let request = transport::Request {
            url: url.to_string(),
            pin: if tls { self.pin.clone() } else { Vec::new() },
            certificate: if tls {
                self.context.host.client_certificate.as_bytes().to_vec()
            } else {
                Vec::new()
            },
            key: if tls {
                self.context.host.client_key.as_bytes().to_vec()
            } else {
                Vec::new()
            },
            body: None,
            timeout: if timeout == 0 {
                None
            } else {
                Some(Duration::from_millis(timeout))
            },
            max_bytes: 0,
            allow_unpaired_http: !tls,
            cancellation: self.cancellation.clone(),
        };
        let response = transport::perform(&request).await;
        if response.error != transport::Error::None {
            return Err(Error::Transport(response.error));
        }
        if (300..400).contains(&response.status) {
            return Err(Error::Redirect);
        }
        if response.status >= 400 {
            return Err(Error::Http(response.status));
        }
        if endpoint == "/unpair" {
            return Ok(xml::Document {
                fields: Default::default(),
                elements: Vec::new(),
            });
        }
        xml::parse(&response.body).map_err(Error::Xml)
    }
    async fn exchange(&self, tls: bool, args: &str, timeout: u64) -> Result<xml::Document, Error> {
        self.request(
            tls,
            "/pair",
            &format!("devicename=roth&updateState=1&{args}"),
            timeout,
        )
        .await
    }
    async fn reject(&self, state: State) -> Result<Outcome, Error> {
        self.request(false, "/unpair", "", 5000).await?;
        Ok(Outcome::failed(state))
    }
}
fn field<'a>(response: &'a xml::Document, name: &str) -> &'a str {
    response.fields.get(name).map(String::as_str).unwrap_or("")
}
pub async fn pair(
    context: Context,
    cancellation: CancellationToken,
    mut save_name: impl FnMut(&str),
) -> Result<Outcome, Error> {
    let client = crypto(X509::from_pem(context.host.client_certificate.as_bytes()))?;
    let key = crypto(PKey::private_key_from_pem_callback(
        context.host.client_key.as_bytes(),
        |_| Ok(0),
    ))?;
    let generation = context.generation;
    let salt = random()?;
    let aes = derive_key(&salt, &context.pin, generation)?;
    // Existing HTTP requests use the pair name immediately after stage one.
    let mut peer = Peer {
        context,
        pin: Vec::new(),
        cancellation,
    };
    let response = peer
        .exchange(
            false,
            &format!(
                "phrase=getservercert&salt={}&clientcert={}",
                xml::hex(&salt),
                xml::hex(peer.context.host.client_certificate.as_bytes())
            ),
            0,
        )
        .await?;
    if field(&response, "paired") != "1" {
        return Ok(Outcome::failed(State::Failed));
    }
    let pem = xml::unhex(field(&response, "plaincert"));
    if pem.is_empty() {
        return peer.reject(State::AlreadyInProgress).await;
    }
    let server = match X509::from_pem(&pem) {
        Ok(server) => server,
        Err(_) => return peer.reject(State::Failed).await,
    };
    let name = field(&response, "pairname");
    if !name.is_empty() {
        save_name(name);
        peer.context.host.client_name = name.to_owned();
    }
    peer.pin = crypto(server.to_der())?;
    let challenge = random()?;
    let response = peer
        .exchange(
            false,
            &format!(
                "clientchallenge={}",
                xml::hex(&cipher(&challenge, &aes, Mode::Encrypt)?)
            ),
            5000,
        )
        .await?;
    if field(&response, "paired") != "1" {
        return peer.reject(State::Failed).await;
    }
    let decrypted = cipher(
        &xml::unhex(field(&response, "challengeresponse")),
        &aes,
        Mode::Decrypt,
    )?;
    let hash_len = if generation >= 7 { 32 } else { 20 };
    if decrypted.len() < hash_len + 16 {
        return peer.reject(State::Failed).await;
    }
    let server_response = &decrypted[..hash_len];
    let secret = random()?;
    let mut answer = decrypted[hash_len..hash_len + 16].to_vec();
    answer.extend_from_slice(client.signature().as_slice());
    answer.extend_from_slice(&secret);
    let mut answer = crypto(hash(digest(generation), &answer))?.to_vec();
    answer.resize(32, 0);
    let response = peer
        .exchange(
            false,
            &format!(
                "serverchallengeresp={}",
                xml::hex(&cipher(&answer, &aes, Mode::Encrypt)?)
            ),
            5000,
        )
        .await?;
    let server_secret = xml::unhex(field(&response, "pairingsecret"));
    if field(&response, "paired") != "1" || server_secret.len() <= 16 {
        return peer.reject(State::Failed).await;
    }
    if !verify(&server_secret[..16], &server_secret[16..], &server)? {
        return peer.reject(State::Failed).await;
    }
    let mut expected = challenge;
    expected.extend_from_slice(server.signature().as_slice());
    expected.extend_from_slice(&server_secret[..16]);
    if !memcmp::eq(
        crypto(hash(digest(generation), &expected))?.as_ref(),
        server_response,
    ) {
        return peer.reject(State::PinWrong).await;
    }
    let mut signed_secret = secret.clone();
    signed_secret.extend_from_slice(&sign(&secret, &key)?);
    let response = peer
        .exchange(
            false,
            &format!("clientpairingsecret={}", xml::hex(&signed_secret)),
            5000,
        )
        .await?;
    if field(&response, "paired") != "1" {
        return peer.reject(State::Failed).await;
    }
    let response = peer.exchange(true, "phrase=pairchallenge", 5000).await?;
    if field(&response, "paired") != "1" {
        return peer.reject(State::Failed).await;
    }
    Ok(Outcome {
        state: State::Paired,
        certificate: Some(String::from_utf8(pem).map_err(|_| Error::Crypto)?),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn invalid_or_encrypted_local_keys_fail_before_network_without_prompting() {
        let pem = include_str!("../../../tests/clipboard_payload_routing/fixtures/server.pem");
        let key = include_str!("../../../tests/clipboard_payload_routing/fixtures/server-key.pem");
        let key = PKey::private_key_from_pem(key.as_bytes()).unwrap();
        let encrypted = String::from_utf8(
            key.private_key_to_pem_pkcs8_passphrase(Cipher::aes_256_cbc(), b"fixture-password")
                .unwrap(),
        )
        .unwrap();
        for key in [String::new(), "invalid".into(), encrypted] {
            let context = Context {
                host: HostContext {
                    base_url: "invalid".into(),
                    server_certificate: String::new(),
                    client_certificate: pem.into(),
                    client_key: key,
                    unique_id: String::new(),
                    client_name: String::new(),
                    nonce: String::new(),
                },
                http_url: "invalid".into(),
                generation: 7,
                pin: "private-pin".into(),
            };
            assert_eq!(format!("{context:?}"), "PairingContext { .. }");
            assert!(matches!(
                pair(context, CancellationToken::new(), |_| panic!(
                    "Name saved before validation"
                ))
                .await,
                Err(Error::Crypto)
            ));
        }
    }
    #[test]
    fn binary_derivation_including_zero_bytes() {
        assert_eq!(openssl::version::number(), 0x30600050);
        let salt: Vec<u8> = (0..16).collect();
        assert_eq!(
            xml::hex(&derive_key(&salt, "0214", 6).unwrap()),
            "0083626da4cdf3bf8f34fc8204d09b67"
        );
        assert_eq!(
            xml::hex(&derive_key(&salt, "0605", 7).unwrap()),
            "0062d88815ca075e125da2dc72afcc73"
        );
    }
}
