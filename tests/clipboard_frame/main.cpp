#include "streaming/clipboardframe.h"
#include "streaming/clipboardpipelines.h"

#include <iostream>
#include <sstream>
#include <string>

int main()
{
    using namespace ClipboardFrame;
    const auto header = encodeHeader(Png, 0xfedcba98, 3);
    const std::string expected("\x01\x02\x98\xba\xdc\xfe\x03\0\0\0", HeaderSize);
    if (!header || std::string_view(header->data(), header->size()) != expected) return 1;

    const std::string payload("a\0b", 3);
    const std::string frame = expected + payload;
    const auto decoded = decode(frame);
    if (!decoded || decoded->kind != Png || decoded->token != 0xfedcba98 ||
            decoded->payload != payload || decoded->payload.data() != frame.data() + HeaderSize) return 2;

    const auto inboundLimit = expected.substr(0, 6) + std::string("\xf5\xff\0\0", 4);
    const auto inboundOversize = expected.substr(0, 6) + std::string("\xf6\xff\0\0", 4);
    if (!encodeHeader(Text, 0, MaxInlinePayload) || encodeHeader(Text, 0, MaxInlinePayload + 1) ||
            !decode(inboundLimit + std::string(MaxPayload, 'x')) ||
            decode(inboundOversize + std::string(MaxPayload + 1, 'x'))) return 3;

    auto wrongVersion = frame;
    wrongVersion[0] = 2;
    if (decode(wrongVersion) || decode(std::string_view(frame).substr(0, HeaderSize - 1)) ||
            decode(std::string_view(frame).substr(0, frame.size() - 1)) || decode(frame + "x")) return 4;

    using namespace ClipboardPipe;
    const std::string atLimit(MaxLineBytes, 'x');
    const auto found = findLine("one\npartial");
    if (found.status != LineStatus::Complete || found.length != 3 ||
        findLine("partial").status != LineStatus::Incomplete ||
        findLine(atLimit + "x").status != LineStatus::Oversized) return 5;

    std::istringstream pipe(atLimit + "\r\n" + atLimit + "xx\n\nlast");
    std::string line;
    if (readLine(pipe, line) != LineStatus::Complete || line != atLimit ||
        readLine(pipe, line) != LineStatus::Oversized || !line.empty() ||
        readLine(pipe, line) != LineStatus::Complete || !line.empty() ||
        readLine(pipe, line) != LineStatus::Complete || line != "last" ||
        readLine(pipe, line) != LineStatus::Incomplete) return 6;

    std::cout << "Clipboard frame bytes, payload bounds and pipe framing passed\n";
}
