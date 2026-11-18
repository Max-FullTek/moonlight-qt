#include "backend/nativehttprequests.h"
#include <thread>
#include <vector>

int main()
{
    NativeHttp::RequestGroup requests;
    auto active = requests.acquire();
    if (active->requested()) return 1;
    std::vector<std::shared_ptr<NativeHttp::Cancellation>> tokens;
    std::thread producer([&] {
        for (int i = 0; i < 256; ++i) tokens.push_back(requests.acquire());
    });
    requests.close();
    producer.join();
    requests.close();
    if (!active->requested()) return 2;
    for (const auto& token : tokens) if (!token->requested()) return 3;

    NativeHttp::Request request;
    request.url = "http://127.0.0.1:1/";
    request.allowUnpairedHttp = true;
    request.cancellation = requests.acquire();
    request.timeoutMs = 0;
    return NativeHttp::perform(request).error == NativeHttp::Error::Cancelled ? 0 : 4;
}
