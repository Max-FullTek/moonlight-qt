// Reference semantics of the existing Qt 6 XML/JSON path, not a product path.
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>
#include <iostream>
#include <string>

QString name(QStringView uri, QStringView local) {
    return uri.isEmpty() ? local.toString() : uri.toString() + QChar(0x1f) + local;
}
int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const auto body = QByteArray::fromHex(request.value("hex").toString().toLatin1());
        QJsonObject result;
        if (request.value("kind").toString() == "xml") {
            QXmlStreamReader xml(QString::fromUtf8(body));
            QJsonArray events;
            while (!xml.atEnd()) {
                const auto token = xml.readNext();
                if (token == QXmlStreamReader::StartElement) {
                    QJsonObject attributes;
                    for (const auto& attribute : xml.attributes())
                        attributes.insert(name(attribute.namespaceUri(), attribute.name()), attribute.value().toString());
                    events.append(QJsonArray{"start", name(xml.namespaceUri(), xml.name()), attributes});
                } else if (token == QXmlStreamReader::EndElement) {
                    events.append(QJsonArray{"end", name(xml.namespaceUri(), xml.name())});
                } else if (token == QXmlStreamReader::Characters) {
                    if (!events.isEmpty() && events.last().toArray().first().toString() == "text") {
                        auto previous = events.last().toArray();
                        previous[1] = previous[1].toString() + xml.text();
                        events.replace(events.size() - 1, previous);
                    } else events.append(QJsonArray{"text", xml.text().toString()});
                }
            }
            result.insert("valid", !xml.hasError());
            if (!xml.hasError()) result.insert("events", events);
        } else {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(body, &error);
            const bool valid = error.error == QJsonParseError::NoError && document.isObject();
            result.insert("valid", valid);
            if (valid) {
                result.insert("value", document.object());
                result.insert("integer", document.object().value("number").toInt());
            }
        }
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n' << std::flush;
    }
}
