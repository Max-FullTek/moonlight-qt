#include "windowshttptransfer.h"

// WSAAsyncSelect deliberately uses the owner's existing message queue;
// WSAEventSelect would require a separate HANDLE wait integration.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <algorithm>
#include <utility>

namespace NativeHttp {
struct WindowsTransfer::Window {
    static constexpr wchar_t ClassName[] = L"MoonlightLiteHttpTransfer";
    static constexpr UINT SocketMessage = WM_APP;
    static constexpr UINT AdvanceMessage = WM_APP + 1;
    HWND handle = nullptr;
    UINT_PTR timer = 0;
    bool started = false;
    std::vector<SocketInterest> sockets;
    std::unique_ptr<AsyncRequest> request;
    Completion completed;

    Window(Request input, Completion completion)
        : request(std::make_unique<AsyncRequest>(std::move(input))), completed(std::move(completion))
    {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = dispatch;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = ClassName;
        if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        handle = CreateWindowExW(0, ClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
        started = handle && PostMessageW(handle, AdvanceMessage, 0, 0) != 0;
    }
    ~Window()
    {
        clearWatchers();
        request.reset();
        if (handle) DestroyWindow(handle);
    }
    void clearWatchers()
    {
        if (timer) KillTimer(handle, timer);
        timer = 0;
        for (const auto& socket : sockets) WSAAsyncSelect(static_cast<SOCKET>(socket.socket), handle, 0, 0);
        sockets.clear();
        // Cancelling WSAAsyncSelect/KillTimer leaves already queued messages.
        // Remove them before libcurl can close or reuse a socket or timer ID.
        if (handle) {
            MSG message{};
            while (PeekMessageW(&message, handle, SocketMessage, AdvanceMessage, PM_REMOVE)) {}
            while (PeekMessageW(&message, handle, WM_TIMER, WM_TIMER, PM_REMOVE)) {}
        }
    }
    void finish(Response response)
    {
        clearWatchers();
        request.reset();
        auto callback = std::move(completed);
        if (callback) callback(std::move(response)); // May destroy this owner.
    }
    void setupFailed()
    {
        Response response;
        response.error = Error::Setup;
        response.message = "Unable to register native HTTP events";
        finish(std::move(response));
    }
    void advance(ReadyEvent event)
    {
        if (!request) return;
        clearWatchers();
        if (auto response = request->advance(event)) {
            finish(std::move(*response));
            return;
        }
        const auto state = request->waitState();
        for (const auto& socket : state.sockets) {
            long mask = FD_CLOSE | FD_CONNECT | FD_OOB;
            if (socket.read) mask |= FD_READ;
            if (socket.write) mask |= FD_WRITE;
            sockets.push_back(socket);
            if (WSAAsyncSelect(static_cast<SOCKET>(socket.socket), handle, SocketMessage, mask) == SOCKET_ERROR) {
                setupFailed();
                return;
            }
        }
        if (state.timeoutMs == 0) {
            if (!PostMessageW(handle, AdvanceMessage, 0, 0)) setupFailed();
        } else if (state.timeoutMs > 0) {
            timer = SetTimer(handle, 1, static_cast<UINT>(state.timeoutMs), nullptr);
            if (!timer) setupFailed();
        }
    }
    static LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        } else {
            auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (self && message == SocketMessage) {
                const auto descriptor = static_cast<std::uintptr_t>(wParam);
                const auto found = std::find_if(self->sockets.begin(), self->sockets.end(),
                    [descriptor](const SocketInterest& socket) { return socket.socket == descriptor; });
                if (found != self->sockets.end()) {
                    const int event = WSAGETSELECTEVENT(lParam);
                    self->advance({ReadyEvent::Kind::Socket, descriptor,
                        event == FD_READ || event == FD_CLOSE, event == FD_WRITE || event == FD_CONNECT,
                        WSAGETSELECTERROR(lParam) != 0 || event == FD_OOB});
                }
                return 0;
            }
            if (self && (message == AdvanceMessage || (message == WM_TIMER && wParam == self->timer))) {
                self->advance({ReadyEvent::Kind::Timeout});
                return 0;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
};

WindowsTransfer::WindowsTransfer(Request request, Completion completed)
    : m_Window(std::make_unique<Window>(std::move(request), std::move(completed))) {}
WindowsTransfer::~WindowsTransfer() = default;
bool WindowsTransfer::active() const { return m_Window->started && bool(m_Window->request); }
void WindowsTransfer::cancel() { m_Window->advance({ReadyEvent::Kind::Cancel}); }
}
