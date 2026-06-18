#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>

class ProtectedKeyStore
{
public:
    static bool hasEncryptedPrivateKey(QSettings& settings);

    static QByteArray decryptPrivateKey(QSettings& settings,
                                        const QString& password);

    static bool saveEncryptedPrivateKey(QSettings& settings,
                                        const QByteArray& privateKeyPem,
                                        const QString& password);
};
