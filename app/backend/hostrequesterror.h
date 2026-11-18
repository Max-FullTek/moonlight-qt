#pragma once

#ifdef LITE_GPUI_WORKER
// Keep the established error numbers shown by the worker in protocol messages.
enum class HostRequestError {
    NoError = 0,
    ConnectionRefusedError = 1,
    RemoteHostClosedError = 2,
    HostNotFoundError = 3,
    TimeoutError = 4,
    OperationCanceledError = 5,
    SslHandshakeFailedError = 6,
    UnknownNetworkError = 99,
    ProxyAuthenticationRequiredError = 105,
    ContentAccessDenied = 201,
    ContentOperationNotPermittedError = 202,
    ContentNotFoundError = 203,
    AuthenticationRequiredError = 204,
    ContentConflictError = 206,
    ContentGoneError = 207,
    UnknownContentError = 299,
    ProtocolUnknownError = 301,
    ProtocolInvalidOperationError = 302,
    InternalServerError = 401,
    OperationNotImplementedError = 402,
    ServiceUnavailableError = 403,
    UnknownServerError = 499
};
#else
#include <QNetworkReply>
using HostRequestError = QNetworkReply::NetworkError;
#endif
