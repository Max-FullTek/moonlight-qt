#pragma once

#include "identitymanager.h"
#include "nvcomputer.h"
#include "nvhttp.h"


class NvPairingManager
{
public:
    enum PairState
    {
        PAIRED,
        PIN_WRONG,
        FAILED,
        ALREADY_IN_PROGRESS
    };

    explicit NvPairingManager(NvComputer* computer);

    ~NvPairingManager();

    PairState
    pair(QString appVersion, QString pin, QSslCertificate& serverCert);

private:
    NvHTTP m_Http;
    NvComputer* m_Computer;
};
