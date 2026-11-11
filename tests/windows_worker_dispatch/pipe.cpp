#include "backend/workercommandpipe.h"

#include <memory>

int main()
{
    std::FILE* file = nullptr;
    if (tmpfile_s(&file) || !file) return 1;
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> input(file, std::fclose);
    const std::string maximum(WorkerCommandPipe::MaxLineBytes, 'x');
    const std::string data = "\nfirst\r\n" + maximum + "\n" + maximum +
                             "overflow\nnext\nunterminated";
    if (std::fwrite(data.data(), 1, data.size(), file) != data.size() ||
        std::fseek(file, 0, SEEK_SET)) return 2;

    using WorkerCommandPipe::ReadStatus;
    std::string line;
    if (WorkerCommandPipe::read(file, line) != ReadStatus::Command || !line.empty()) return 3;
    if (WorkerCommandPipe::read(file, line) != ReadStatus::Command || line != "first\r") return 4;
    if (WorkerCommandPipe::read(file, line) != ReadStatus::Command || line != maximum) return 5;
    if (WorkerCommandPipe::read(file, line) != ReadStatus::Oversized ||
        line.size() > WorkerCommandPipe::MaxLineBytes) return 6;
    if (WorkerCommandPipe::read(file, line) != ReadStatus::Command || line != "next") return 7;
    if (WorkerCommandPipe::read(file, line) != ReadStatus::EndOfInput || !line.empty()) return 8;
    if (WorkerCommandPipe::read(file, line) != ReadStatus::EndOfInput) return 9;
    return 0;
}
