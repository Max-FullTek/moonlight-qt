//! Probe normalization for the shared Qt comparison corpus, not a host model.
use quick_xml::{
    events::Event,
    name::{LocalName, ResolveResult},
    reader::NsReader,
};
use serde_json::{Value, json};

fn name(namespace: ResolveResult<'_>, local: LocalName<'_>) -> Result<String, ()> {
    let local = local.as_ref();
    match namespace {
        ResolveResult::Unbound => Ok(local.to_owned()),
        ResolveResult::Bound(uri) => Ok(format!("{}\u{1f}{local}", uri.as_ref())),
        ResolveResult::Unknown(_) => Err(()),
    }
}
fn text(events: &mut Vec<Value>, value: &str) {
    if let Some(previous) = events.last_mut().filter(|event| event[0] == "text") {
        let mut merged = previous[1].as_str().unwrap().to_owned();
        merged.push_str(value);
        previous[1] = Value::String(merged);
    } else if !value.is_empty() {
        events.push(json!(["text", value]));
    }
}
fn xml(body: &[u8]) -> Result<Value, ()> {
    let input = std::str::from_utf8(body).map_err(|_| ())?;
    let mut reader = NsReader::from_str(input);
    reader.config_mut().expand_empty_elements = true;
    reader.config_mut().check_comments = true;
    let mut events = Vec::new();
    let mut depth = 0usize;
    let mut root = false;
    loop {
        match reader.read_event().map_err(|_| ())? {
            Event::Start(element) => {
                if depth == 0 {
                    if root {
                        return Err(());
                    }
                    root = true;
                }
                depth += 1;
                let (namespace, local) = reader.resolver().resolve_element(element.name());
                let element_name = name(namespace, local)?;
                let mut attributes = serde_json::Map::new();
                for attribute in element.attributes() {
                    let attribute = attribute.map_err(|_| ())?;
                    if attribute.key.as_ref() == "xmlns"
                        || attribute.key.as_ref().starts_with("xmlns:")
                    {
                        continue;
                    }
                    let (namespace, local) = reader.resolver().resolve_attribute(attribute.key);
                    let key = name(namespace, local)?;
                    let value = attribute
                        .normalized_value(quick_xml::XmlVersion::Implicit1_0)
                        .map_err(|_| ())?
                        .into_owned();
                    if attributes.insert(key, Value::String(value)).is_some() {
                        return Err(());
                    }
                }
                events.push(json!(["start", element_name, attributes]));
            }
            Event::End(element) => {
                depth = depth.checked_sub(1).ok_or(())?;
                let (namespace, local) = reader.resolver().resolve_element(element.name());
                events.push(json!(["end", name(namespace, local)?]));
            }
            Event::Text(value) => {
                let value = value.xml10_content();
                if depth == 0 {
                    if !value.trim().is_empty() {
                        return Err(());
                    }
                } else {
                    text(&mut events, &value);
                }
            }
            Event::CData(value) if depth != 0 => text(&mut events, &value.xml10_content()),
            Event::GeneralRef(reference) if depth != 0 => {
                let reference = format!("&{};", reference.as_ref());
                text(
                    &mut events,
                    &quick_xml::escape::unescape(&reference).map_err(|_| ())?,
                );
            }
            // Candidate only: no entity resolver, files or network access.
            // Product DTD/invalid-input policy still requires SA confirmation.
            Event::DocType(_) | Event::CData(_) | Event::GeneralRef(_) => return Err(()),
            Event::Eof => break,
            _ => {}
        }
    }
    if !root || depth != 0 {
        return Err(());
    }
    Ok(json!({"valid": true, "events": events}))
}
pub fn parse(kind: &str, body: &[u8]) -> Value {
    if kind == "xml" {
        return xml(body).unwrap_or(json!({"valid": false}));
    }
    // Retain serde_json's bounded default recursion. No unbounded-depth feature.
    // Qt accepts a UTF-8 BOM; strip only that optional prefix for comparison.
    let body = body.strip_prefix(b"\xef\xbb\xbf").unwrap_or(body);
    match serde_json::from_slice::<Value>(body) {
        Ok(value) if value.is_object() => {
            let integer = value["number"]
                .as_f64()
                .filter(|number| {
                    number.is_finite()
                        && number.fract() == 0.0
                        && *number >= i32::MIN as f64
                        && *number <= i32::MAX as f64
                })
                .map_or(0, |number| number as i32);
            json!({"valid": true, "value": value, "integer": integer})
        }
        _ => json!({"valid": false}),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_incomplete_or_multiple_documents() {
        for body in [
            "<root>",
            "<root/><other/>",
            "<root/>trailing",
            "<![CDATA[x]]>",
        ] {
            assert_eq!(
                parse("xml", body.as_bytes()),
                json!({"valid":false}),
                "{body}"
            );
        }
    }
    #[test]
    fn integer_fields_preserve_i32_range() {
        for (body, expected) in [
            ("{\"number\":2147483647}", i32::MAX),
            ("{\"number\":-2147483648}", i32::MIN),
            ("{\"number\":2147483648}", 0),
            ("{\"number\":1.5}", 0),
            ("{\"number\":\"5\"}", 0),
        ] {
            assert_eq!(parse("json", body.as_bytes())["integer"], expected);
        }
    }
}
