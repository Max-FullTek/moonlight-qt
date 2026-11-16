#include "backend/workercommandrouting.h"

int main(int argc, char** argv)
{
    using namespace WorkerCommandPipe;
    if (argc != 2 || std::string_view(argv[1]) != "--gpui-core") return 1;
    if (!write(Ready)) return 2;
    bool outputOk = true;
    WorkerCommandRouting::run(stdin, [&outputOk](ReadStatus status, std::string line) {
        if (status == ReadStatus::Command && line.size() > MaxControlLineBytes) {
            outputOk = write(R"({"event":"error","protocol":1,"code":"request_too_large"})");
        }
    });
    return outputOk ? 0 : 3;
}
