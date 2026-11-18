#pragma once

#include <QString>

inline void redactSessionParameters(QString& message)
{
    const auto redact = [&message](QLatin1String prefix, auto isValueCharacter) {
        for (auto offset = message.indexOf(prefix); offset >= 0;) {
            const auto first = offset + prefix.size();
            auto end = first;
            while (end < message.size() && isValueCharacter(message.at(end).unicode())) {
                ++end;
            }
            if (end > first) {
                const QLatin1String replacement("REDACTED");
                message.replace(first, end - first, replacement);
                end = first + replacement.size();
            }
            offset = message.indexOf(prefix, end);
        }
    };
    redact(QLatin1String("&rikey="), [](auto c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') || c == '_';
    });
    redact(QLatin1String("&rikeyid="), [](auto c) {
        return (c >= '0' && c <= '9') || c == '-';
    });
}
