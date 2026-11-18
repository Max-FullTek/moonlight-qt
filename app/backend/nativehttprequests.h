#pragma once

#include "nativehttptransport.h"
#include <algorithm>
#include <mutex>

namespace NativeHttp {
// One worker's request lifetime. Closing cancels active transfers and prevents
// a racing task from starting a new transfer after owner shutdown.
class RequestGroup {
public:
    std::shared_ptr<Cancellation> acquire()
    {
        auto token = std::make_shared<Cancellation>();
        std::lock_guard lock(m_Mutex);
        m_Requests.erase(std::remove_if(m_Requests.begin(), m_Requests.end(),
            [](const auto& request) { return request.expired(); }), m_Requests.end());
        if (m_Closed) token->cancel();
        else m_Requests.emplace_back(token);
        return token;
    }
    void close()
    {
        std::lock_guard lock(m_Mutex);
        m_Closed = true;
        for (const auto& request : m_Requests) {
            if (auto token = request.lock()) token->cancel();
        }
        m_Requests.clear();
    }
private:
    std::mutex m_Mutex;
    bool m_Closed = false;
    std::vector<std::weak_ptr<Cancellation>> m_Requests;
};
}
