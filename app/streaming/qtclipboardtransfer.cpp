#include "qtclipboardtransfer.h"
#include "backend/windowshttptransfer.h"
#include <QMetaObject>
#include <utility>

QtClipboardTransfer::QtClipboardTransfer(NativeHttp::Request request, Completion completed, QObject* parent)
    : QObject(parent), m_Completed(std::move(completed))
{
    m_Transfer = std::make_unique<NativeHttp::WindowsTransfer>(std::move(request),
        [this](NativeHttp::Response response) { finish(std::move(response)); });
    if (!m_Transfer->active()) {
        QMetaObject::invokeMethod(this, [this] {
            if (!m_Completed) return;
            NativeHttp::Response response;
            response.error = NativeHttp::Error::Setup;
            response.message = "Unable to initialize native HTTP events";
            finish(std::move(response));
        }, Qt::QueuedConnection);
    }
}

QtClipboardTransfer::~QtClipboardTransfer() = default;

void QtClipboardTransfer::cancel()
{
    m_Completed = {};
    m_Transfer.reset();
    deleteLater();
}

void QtClipboardTransfer::finish(NativeHttp::Response response)
{
    deleteLater();
    auto completed = std::move(m_Completed);
    if (completed) completed(this, std::move(response));
}
