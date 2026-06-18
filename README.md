# Moonlight Qt Password Fork

Moonlight Qt is the PC client for Moonlight Game Streaming. It connects to NVIDIA GameStream-compatible hosts such as Sunshine and provides remote game or desktop streaming with video, audio, keyboard, mouse, gamepad, and related client features.

This fork keeps the original Moonlight Qt behavior, but adds startup password protection for the Moonlight client private key.

## Purpose

The goal is to protect portable Moonlight folders.

In the original behavior, Moonlight stores the client identity in the settings file. This fork keeps the client certificate visible, but encrypts the client private key with a startup password.

This means that copying the portable Moonlight folder should not be enough to reuse an already-paired client identity unless the password is known.

## User behavior

On first launch, Moonlight asks for a password.

If no protected identity exists yet, the first password entered becomes the password for the newly generated client identity.

On later launches, Moonlight asks for the same password before loading the main UI.

If the password is correct, Moonlight decrypts the private key and starts normally.

If the password is wrong or the dialog is cancelled, Moonlight exits immediately.

## Forgotten password

The password cannot be recovered.

If the password is forgotten, delete or rename the settings directory and pair hosts again.

For portable builds, the settings directory is usually:

```text
Moonlight Game Streaming Project
```

Example reset:

```bat
ren "Moonlight Game Streaming Project" "Moonlight Game Streaming Project.forgot-password"
```

After reset, Moonlight generates a new client certificate and private key. Existing host pairings will no longer match, because the host will see this as a different Moonlight client.

## Moonlight.ini storage change

The client certificate remains stored normally:

```ini
certificate=...
```

The plaintext private key is removed.

Original plaintext key field:

```ini
key=...
```

This fork replaces it with encrypted private key fields:

```ini
key_protection=password-v1
key_kdf=pbkdf2-sha256
key_iter=800000
key_salt=...
key_nonce=...
key_ciphertext=...
```

## Files changed

This fork changes these files:

```text
app/backend/protectedkeystore.h
app/backend/protectedkeystore.cpp
app/backend/identitymanager.cpp
app/app.pro
app/main.cpp
```

## 1. New file: app/backend/protectedkeystore.h

This file declares a helper class for encrypted private key storage.

The important public API is:

```cpp
class ProtectedKeyStore
{
public:
    static bool hasEncryptedPrivateKey(QSettings& settings);
    static bool saveEncryptedPrivateKey(QSettings& settings,
                                        const QByteArray& privateKey,
                                        const QString& password);
    static QByteArray decryptPrivateKey(QSettings& settings,
                                        const QString& password);
};
```

Purpose of each method:

```text
hasEncryptedPrivateKey()
- Checks whether Moonlight.ini contains protected private key fields.

saveEncryptedPrivateKey()
- Encrypts the plaintext Moonlight client private key with the startup password.
- Writes key_protection / key_kdf / key_iter / key_salt / key_nonce / key_ciphertext.

decryptPrivateKey()
- Reads the encrypted private key fields.
- Uses the startup password to decrypt the private key.
- Returns an empty QByteArray if the password is wrong or decryption fails.
```

## 2. New file: app/backend/protectedkeystore.cpp

This file implements encrypted private key storage.

It uses:

```text
KDF: PBKDF2-HMAC-SHA256
Iterations: 800000
Encryption: AES-256-GCM
Salt: 16 bytes
Nonce: 12 bytes
Tag: 16 bytes
Key: 32 bytes
```

Important storage keys:

```cpp
key_protection=password-v1
key_kdf=pbkdf2-sha256
key_iter=800000
key_salt=...
key_nonce=...
key_ciphertext=...
```

Important behavior:

```text
1. Wrong password must not produce a private key.
2. AES-GCM authentication failure returns an empty private key.
3. The plaintext key field must not be written back.
4. Existing plaintext keys are migrated by identitymanager.cpp.
```

## 3. app/app.pro changes

`widgets` is added because the startup password prompt uses `QInputDialog`.

Before:

```pro
QT += core quick network quickcontrols2 svg
```

After:

```pro
QT += core quick network quickcontrols2 svg widgets
```

Add the new source file:

```pro
SOURCES += \
    backend/protectedkeystore.cpp
```

Add the new header file:

```pro
HEADERS += \
    backend/protectedkeystore.h
```

When applying this patch to a newer Moonlight Qt version, make sure `backend/protectedkeystore.cpp` is included in the build system. Otherwise, compilation may succeed but linking will fail when `ProtectedKeyStore` is referenced.

## 4. app/main.cpp changes

The application class is changed from `QGuiApplication` to `QApplication`.

Before:

```cpp
#include <QGuiApplication>
```

After:

```cpp
#include <QApplication>
```

Before:

```cpp
QGuiApplication app(argc, argv);
```

After:

```cpp
QApplication app(argc, argv);
```

Reason:

```text
The original Moonlight UI is QML / Qt Quick.
This fork adds a startup password prompt using QInputDialog.
QInputDialog belongs to Qt Widgets.
Therefore QApplication is required.
```

## 5. app/backend/identitymanager.cpp changes

This is the main file changed by the password protection patch.

### 5.1 Added includes

Add these includes near the top of `identitymanager.cpp`:

```cpp
#include "protectedkeystore.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QCryptographicHash>

#include <cstdlib>

#include <openssl/evp.h>
```

Purpose:

```text
protectedkeystore.h  - encrypted private key helper
QInputDialog         - startup password prompt
QLineEdit            - password input mode
QCryptographicHash   - certificate SHA256 fingerprint log
cstdlib              - std::exit(0)
openssl/evp.h        - EVP_PKEY_eq / EVP_PKEY_cmp
```

### 5.2 Added startup password helper

Add this helper near the top of `identitymanager.cpp`, after the `SER_*` defines and before `IdentityManager* IdentityManager::s_Im = nullptr;`.

```cpp
static QString getPrivateKeyPassword()
{
    static bool initialized = false;
    static QString password;

    if (!initialized) {
        bool ok = false;

        password = QInputDialog::getText(
            nullptr,
            QString(),
            QString(),
            QLineEdit::Password,
            QString(),
            &ok
        );

        if (!ok || password.isEmpty()) {
            std::exit(0);
        }

        initialized = true;
    }

    return password;
}
```

Behavior:

```text
1. Password is requested once per process.
2. Password is cached in a static QString.
3. Empty password is not allowed.
4. Cancel exits immediately.
5. Wrong password exits later when decryption fails.
```

### 5.3 Added certificate/private key match helper

Add this helper after `getPrivateKeyPassword()` and before `IdentityManager* IdentityManager::s_Im = nullptr;`.

```cpp
static bool certificateMatchesPrivateKey(const QByteArray& pemCert, const QByteArray& pemKey)
{
    bool match = false;

    BIO* certBio = BIO_new_mem_buf(pemCert.constData(), pemCert.size());
    if (!certBio) {
        return false;
    }

    X509* cert = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
    BIO_free(certBio);

    if (!cert) {
        return false;
    }

    BIO* keyBio = BIO_new_mem_buf(pemKey.constData(), pemKey.size());
    if (!keyBio) {
        X509_free(cert);
        return false;
    }

    EVP_PKEY* privateKey = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
    BIO_free(keyBio);

    if (!privateKey) {
        X509_free(cert);
        return false;
    }

    EVP_PKEY* publicKey = X509_get_pubkey(cert);
    if (publicKey) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        match = EVP_PKEY_eq(publicKey, privateKey) == 1;
#else
        match = EVP_PKEY_cmp(publicKey, privateKey) == 1;
#endif
        EVP_PKEY_free(publicKey);
    }

    EVP_PKEY_free(privateKey);
    X509_free(cert);

    return match;
}
```

Purpose:

```text
This verifies that the certificate stored in Moonlight.ini matches the private key decrypted from key_ciphertext.
```

It prevents this broken state:

```text
certificate = A
private key = B
```

If they do not match, Moonlight exits with:

```text
Identity certificate and private key do not match
```

If they match, Moonlight logs:

```text
Identity certificate and private key match
Identity certificate SHA256: ...
```

The SHA256 value is only the certificate fingerprint. The private key is not printed.

## 6. IdentityManager::createCredentials() change

Original behavior:

```text
Moonlight generates a client certificate and private key.
Both are saved to settings.
The private key is saved as plaintext key=...
```

Fork behavior:

```text
Moonlight still generates a client certificate and private key.
The certificate is saved normally.
The private key is encrypted with the startup password.
The encrypted key fields are saved to Moonlight.ini.
The old plaintext key field is removed.
```

Replace the original save logic:

```cpp
settings.setValue(SER_CERT, m_CachedPemCert);
settings.setValue(SER_KEY, m_CachedPrivateKey);

qInfo() << "Wrote new identity credentials to settings";
```

with:

```cpp
settings.setValue(SER_CERT, m_CachedPemCert);

if (!ProtectedKeyStore::saveEncryptedPrivateKey(settings, m_CachedPrivateKey, getPrivateKeyPassword())) {
    qFatal("Failed to encrypt private key");
}

settings.remove(SER_KEY);
settings.sync();

qInfo() << "Wrote new protected identity credentials to settings";
```

Important:

```text
On first launch with no existing identity, the first password entered becomes the password for the newly generated client identity.
```

## 7. IdentityManager::IdentityManager() replacement logic

The constructor is changed so it can load encrypted private keys, migrate old plaintext keys, and validate the final identity.

The important structure is:

```cpp
IdentityManager::IdentityManager()
{
    QSettings settings;

    m_CachedPemCert = settings.value(SER_CERT).toByteArray();

    if (ProtectedKeyStore::hasEncryptedPrivateKey(settings)) {
        m_CachedPrivateKey = ProtectedKeyStore::decryptPrivateKey(settings, getPrivateKeyPassword());

        if (m_CachedPrivateKey.isEmpty()) {
            std::exit(0);
        }

        qInfo() << "Loaded protected private key from settings";
    }
    else {
        m_CachedPrivateKey = settings.value(SER_KEY).toByteArray();

        if (!m_CachedPrivateKey.isEmpty()) {
            qInfo() << "Migrating plaintext private key to protected storage";

            if (!ProtectedKeyStore::saveEncryptedPrivateKey(settings, m_CachedPrivateKey, getPrivateKeyPassword())) {
                qFatal("Failed to migrate private key to protected storage");
            }

            settings.remove(SER_KEY);
            settings.sync();
        }
    }

    if (m_CachedPemCert.isEmpty() || m_CachedPrivateKey.isEmpty()) {
        qInfo() << "No existing credentials found";
        createCredentials(settings);
    }
    else if (getSslCertificate().isNull()) {
        qWarning() << "Certificate is unreadable";
        createCredentials(settings);
    }
    else if (getSslKey().isNull()) {
        qWarning() << "Private key is unreadable";
        createCredentials(settings);
    }

    if (getSslCertificate().isNull()) {
        qFatal("Certificate is unreadable");
    }

    if (getSslKey().isNull()) {
        qFatal("Private key is unreadable");
    }

    if (!certificateMatchesPrivateKey(m_CachedPemCert, m_CachedPrivateKey)) {
        qFatal("Identity certificate and private key do not match");
    }

    qInfo() << "Identity certificate and private key match";
    qInfo() << "Identity certificate SHA256:"
            << QCryptographicHash::hash(m_CachedPemCert, QCryptographicHash::Sha256).toHex();
}
```

### Constructor behavior by case

#### Case 1: encrypted private key exists

If these fields exist:

```ini
key_protection=password-v1
key_ciphertext=...
```

Moonlight asks for the startup password and decrypts the private key.

Expected successful log:

```text
Loaded protected private key from settings
```

Wrong password:

```text
Moonlight exits immediately.
```

#### Case 2: old plaintext private key exists

If old settings contain:

```ini
key=...
```

Moonlight migrates it.

Migration flow:

```text
1. Load old plaintext private key.
2. Ask for startup password.
3. Encrypt the private key.
4. Save encrypted private key fields.
5. Remove the old plaintext key field.
6. Sync settings.
```

Expected log:

```text
Migrating plaintext private key to protected storage
```

#### Case 3: no private key exists

If no key exists, Moonlight creates a new identity.

Expected log:

```text
No existing credentials found
Wrote new protected identity credentials to settings
```

## 8. Expected log messages

First run:

```text
No existing credentials found
Wrote new protected identity credentials to settings
Identity certificate and private key match
Identity certificate SHA256: ...
```

Normal startup with correct password:

```text
Loaded protected private key from settings
Identity certificate and private key match
Identity certificate SHA256: ...
```

Wrong password or cancelled dialog:

```text
Moonlight exits before loading the main UI.
```

Certificate/private key mismatch:

```text
Identity certificate and private key do not match
```

## 9. Pairing behavior

The startup password is local only.

It does not come from the host.

Moonlight generates its own client certificate and private key locally.

During pairing, the host records the Moonlight client certificate.

On later connections, Moonlight uses the private key to prove it is the same paired client.

Pairing normally does not change the client private key.

If `Moonlight.ini` or the portable settings directory is deleted, Moonlight generates a new client certificate and private key. Existing host pairings will no longer match, so hosts must be paired again.

## 10. Security scope

This fork does not encrypt the whole configuration file.

It only protects the Moonlight client private key.

Protected:

```text
- Client private key
- Ability to reuse an already-paired client identity from a copied portable folder
```

Not protected:

```text
- All host records
- All Moonlight settings
- Runtime memory after the correct password is entered
- Malware already running as the same user
```

The intended use case is portable-folder protection, not full disk encryption or full configuration encryption.
