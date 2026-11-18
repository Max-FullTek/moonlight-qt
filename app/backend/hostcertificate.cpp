#include "hostcertificate.h"

#include <openssl/err.h>
#include <openssl/pem.h>

#include <climits>
#include <memory>
#include <utility>

HostCertificate::HostCertificate(const QByteArray& pem)
{
    if (pem.isEmpty() || pem.size() > INT_MAX) {
        return;
    }

    const bool marked = ERR_set_mark() == 1;
    {
        const std::unique_ptr<BIO, decltype(&BIO_free)> input(
            BIO_new_mem_buf(pem.constData(), static_cast<int>(pem.size())), BIO_free);
        const std::unique_ptr<X509, decltype(&X509_free)> certificate(
            input ? PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr) : nullptr, X509_free);
        const int derLength = certificate ? i2d_X509(certificate.get(), nullptr) : 0;
        if (derLength > 0) {
            QByteArray der(derLength, Qt::Uninitialized);
            auto* cursor = reinterpret_cast<unsigned char*>(der.data());
            const std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), BIO_free);
            if (i2d_X509(certificate.get(), &cursor) == derLength && output &&
                PEM_write_bio_X509(output.get(), certificate.get()) == 1) {
                char* bytes = nullptr;
                const auto length = BIO_get_mem_data(output.get(), &bytes);
                m_Pem = QByteArray(bytes, length);
                m_Der = std::move(der);
            }
        }
    }
    if (marked) {
        ERR_pop_to_mark();
    } else {
        ERR_clear_error();
    }
}
