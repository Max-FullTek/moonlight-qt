#pragma once
#include "nativehttptransport.h"
#include <QNetworkReply>
#include <curl/curl.h>

// Transitional exception vocabulary for NvHTTP callers. No network I/O here.
// Preserve Qt's HTTP status categories, including ComputerManager's 503 retry.
namespace NativeHttp {
inline QNetworkReply::NetworkError qtNetworkError(const Response& response)
{
    using Q = QNetworkReply;
    switch (response.error) {
    case Error::Timeout:
    case Error::Cancelled: return Q::TimeoutError;
    case Error::CertificateMismatch: return Q::SslHandshakeFailedError;
    case Error::InvalidRequest: return Q::ProtocolInvalidOperationError;
    case Error::Setup:
    case Error::Storage:
    case Error::ResponseTooLarge: return Q::UnknownNetworkError;
    case Error::Transport:
        switch (response.transportCode) {
        case CURLE_COULDNT_RESOLVE_HOST: return Q::HostNotFoundError;
        case CURLE_COULDNT_CONNECT: return Q::ConnectionRefusedError;
        case CURLE_OPERATION_TIMEDOUT: return Q::TimeoutError;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CACERT_BADFILE: return Q::SslHandshakeFailedError;
        case CURLE_GOT_NOTHING:
        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
        case CURLE_PARTIAL_FILE: return Q::RemoteHostClosedError;
        case CURLE_UNSUPPORTED_PROTOCOL: return Q::ProtocolUnknownError;
        case CURLE_URL_MALFORMAT: return Q::ProtocolInvalidOperationError;
        default: return Q::UnknownNetworkError;
        }
    case Error::None: break;
    }
    if (response.httpStatus < 400) return Q::NoError;
    // Keep this in step with Qt's statusCodeFromHttp() while callers still
    // expose QNetworkReply::NetworkError. Never turn local TLS errors into 401.
    switch (response.httpStatus) {
    case 400:
    case 418: return Q::ProtocolInvalidOperationError;
    case 401: return Q::AuthenticationRequiredError;
    case 403: return Q::ContentAccessDenied;
    case 404: return Q::ContentNotFoundError;
    case 405: return Q::ContentOperationNotPermittedError;
    case 407: return Q::ProxyAuthenticationRequiredError;
    case 409: return Q::ContentConflictError;
    case 410: return Q::ContentGoneError;
    case 500: return Q::InternalServerError;
    case 501: return Q::OperationNotImplementedError;
    case 503: return Q::ServiceUnavailableError;
    default: return response.httpStatus > 500 ? Q::UnknownServerError : Q::UnknownContentError;
    }
}
inline QString qtErrorText(const Response& response)
{
    if (response.error == Error::Timeout || response.error == Error::Cancelled) return QStringLiteral("Request timed out");
    if (response.error == Error::CertificateMismatch) return QStringLiteral("Server certificate mismatch");
    if (response.error != Error::None) return QString::fromStdString(response.message);
    return QStringLiteral("Host request failed with HTTP status %1").arg(response.httpStatus);
}
}
