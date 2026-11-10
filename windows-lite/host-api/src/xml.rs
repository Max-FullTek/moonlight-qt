//! Complete-document parsing under the approved strict UTF-8/no-DTD policy.
use quick_xml::{events::Event, name::ResolveResult, reader::NsReader};
use std::collections::HashMap;

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Invalid,
    Status(i32),
}
pub struct Document {
    pub fields: HashMap<String, String>,
    pub elements: Vec<(String, String, bool)>,
}
struct Element {
    name: String,
    value: String,
    first: bool,
    child: bool,
    index: usize,
}
pub fn parse(body: &[u8]) -> Result<Document, Error> {
    let (document, status, _) = parse_response(body)?;
    if status != 200 {
        return Err(Error::Status(status));
    }
    Ok(document)
}
/// Retain host status diagnostics for APIs that surface errors to the user.
/// Parsing and document validation remain identical to `parse`.
pub(crate) fn parse_response(body: &[u8]) -> Result<(Document, i32, String), Error> {
    let input = std::str::from_utf8(body).map_err(|_| Error::Invalid)?;
    let mut reader = NsReader::from_str(input);
    reader.config_mut().expand_empty_elements = true;
    reader.config_mut().check_comments = true;
    let mut stack: Vec<Element> = Vec::new();
    let mut fields = HashMap::new();
    let mut elements = Vec::new();
    let mut status = None;
    let mut status_message = String::new();
    let mut root = false;
    loop {
        match reader.read_event().map_err(|_| Error::Invalid)? {
            Event::Start(element) => {
                let (namespace, local) = reader.resolver().resolve_element(element.name());
                if matches!(namespace, ResolveResult::Unknown(_)) {
                    return Err(Error::Invalid);
                }
                let name = local.as_ref().to_owned();
                if stack.is_empty() {
                    if root || name != "root" {
                        return Err(Error::Invalid);
                    }
                    root = true;
                }
                for attribute in element.attributes() {
                    let attribute = attribute.map_err(|_| Error::Invalid)?;
                    let value = attribute
                        .normalized_value(quick_xml::XmlVersion::Implicit1_0)
                        .map_err(|_| Error::Invalid)?;
                    if stack.is_empty() && attribute.key.as_ref() == "status_code" {
                        // Qt reads u32 then casts, including GFE's 0xffffffff.
                        status = Some(value.trim().parse::<u32>().unwrap_or(0) as i32);
                    }
                    if stack.is_empty() && attribute.key.as_ref() == "status_message" {
                        status_message = value.into_owned();
                    }
                }
                // getXmlString finds the first matching local name at any
                // depth. Reserve on start so nested duplicates cannot win.
                let first = !stack.is_empty() && !fields.contains_key(&name);
                if first {
                    fields.insert(name.clone(), String::new());
                }
                if let Some(parent) = stack.last_mut() {
                    parent.child = true;
                }
                stack.push(Element {
                    name: name.clone(),
                    value: String::new(),
                    first,
                    child: false,
                    index: elements.len(),
                });
                elements.push((name, String::new(), false));
            }
            Event::End(_) => {
                let element = stack.pop().ok_or(Error::Invalid)?;
                if element.first {
                    fields.insert(element.name, element.value.clone());
                }
                elements[element.index].1 = element.value;
                elements[element.index].2 = element.child;
            }
            Event::Text(text) => {
                let text = text.xml10_content();
                if let Some(element) = stack.last_mut() {
                    // Qt readElementText returns the prefix when encountering
                    // a child element, rather than flattening nested markup.
                    if !element.child {
                        element.value.push_str(&text);
                    }
                } else if !text.trim().is_empty() {
                    return Err(Error::Invalid);
                }
            }
            Event::CData(text) if !stack.is_empty() => {
                let element = stack.last_mut().unwrap();
                if !element.child {
                    element.value.push_str(&text.xml10_content());
                }
            }
            Event::GeneralRef(reference) if !stack.is_empty() => {
                let encoded = format!("&{};", reference.as_ref());
                let text = quick_xml::escape::unescape(&encoded).map_err(|_| Error::Invalid)?;
                let element = stack.last_mut().unwrap();
                if !element.child {
                    element.value.push_str(&text);
                }
            }
            Event::DocType(_) | Event::CData(_) | Event::GeneralRef(_) => {
                return Err(Error::Invalid);
            }
            Event::Eof => break,
            _ => {}
        }
    }
    if !root || !stack.is_empty() {
        return Err(Error::Invalid);
    }
    Ok((
        Document { fields, elements },
        status.unwrap_or(0),
        status_message,
    ))
}
// QByteArray::fromHex compatibility: skip nonhex and left-pad odd digits.
pub fn unhex(text: &str) -> Vec<u8> {
    let digits: Vec<u8> = text
        .bytes()
        .filter_map(|value| match value {
            b'0'..=b'9' => Some(value - b'0'),
            b'a'..=b'f' => Some(value - b'a' + 10),
            b'A'..=b'F' => Some(value - b'A' + 10),
            _ => None,
        })
        .collect();
    let mut out = Vec::new();
    let mut index = 0;
    if digits.len() % 2 != 0 {
        out.push(digits[0]);
        index = 1;
    }
    for pair in digits[index..].chunks_exact(2) {
        out.push(pair[0] * 16 + pair[1]);
    }
    out
}
pub fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn full_document_and_standard_entities() {
        let doc = parse(
            "<root status_code=\"200\"><pairname>繁中 &amp; &#x1f3ae;</pairname></root>".as_bytes(),
        )
        .unwrap();
        assert_eq!(doc.fields["pairname"], "繁中 & 🎮");
        let nested = parse(b"<root status_code=\"200\"><wrapper><paired>1</paired></wrapper><paired>0</paired><pairname>first<pairname>nested</pairname>tail</pairname></root>").unwrap();
        assert_eq!(nested.fields["paired"], "1");
        assert_eq!(nested.fields["pairname"], "first");
        for body in [
            "<root status_code=\"200\">",
            "<root status_code=\"200\"/>extra",
            "<!DOCTYPE root><root status_code=\"200\"/>",
            "<root status_code=\"200\"/><root/>",
        ] {
            assert!(parse(body.as_bytes()).is_err());
        }
        assert!(parse(b"<root status_code=\"200\">\xff</root>").is_err());
        assert!(matches!(
            parse(b"<root status_code=\"4294967295\"/>"),
            Err(Error::Status(-1))
        ));
        assert_eq!(unhex("z1:2a:bc?0f"), vec![1, 0x2a, 0xbc, 0xf]);
    }
}
