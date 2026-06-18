## Fork changes: startup password protection

This fork keeps the original Moonlight Qt purpose and behavior, but adds startup password protection for the Moonlight client private key.

Moonlight PC is an open source PC client for NVIDIA GameStream and Sunshine. It supports hardware-accelerated game streaming, multiple video codecs, HDR, surround sound, gamepads, touch input, and remote desktop/game control.

### What this fork adds

This fork protects the Moonlight client private key stored in `Moonlight.ini`.

In the original behavior, Moonlight stores the client identity in the settings file. This fork keeps the client certificate visible, but encrypts the client private key with a startup password.

The goal is mainly for portable use: copying the portable Moonlight folder should not be enough to reuse an already-paired client identity without knowing the password.

### User behavior

On first launch, Moonlight asks for a password.

If no protected identity exists yet, the first password entered becomes the password for the new client identity.

Moonlight then generates a new client certificate and private key, encrypts the private key, and writes the protected identity to `Moonlight.ini`.

On later launches, Moonlight asks for the same password before loading the main UI.

If the password is correct, Moonlight decrypts the private key and starts normally.

If the password is wrong or the dialog is cancelled, Moonlight exits immediately.

### Forgotten password

The password cannot be recovered.

If the password is forgotten, delete or rename the settings directory and pair hosts again.

For a portable build, this is usually:

```bat
ren "Moonlight Game Streaming Project" "Moonlight Game Streaming Project.forgot-password"
```

After reset, Moonlight generates a new client certificate and private key. Existing host pairings will no longer match, because the host will see this as a different Moonlight client.

### What is stored in Moonlight.ini

The client certificate remains stored normally:

```ini
certificate=...
```

The plaintext private key is removed.

Instead of the old plaintext key field:

```ini
key=...
```

this fork stores encrypted private key data:

```ini
key_protection=password-v1
key_kdf=pbkdf2-sha256
key_iter=800000
key_salt=...
key_nonce=...
key_ciphertext=...
```

### Files changed

The password protection patch changes these files:

```text
app/backend/protectedkeystore.h
app/backend/protectedkeystore.cpp
app/backend/identitymanager.cpp
app/app.pro
app/main.cpp
```

### protectedkeystore.h / protectedkeystore.cpp

These two files add helper logic for encrypted private key storage.

They are responsible for:

```text
1. Detecting whether an encrypted private key exists.
2. Encrypting the Moonlight client private key.
3. Decrypting the private key with the startup password.
4. Writing encrypted private key fields to QSettings.
5. Avoiding plaintext private key storage in Moonlight.ini.
```

Encryption design:

```text
KDF: PBKDF2-HMAC-SHA256
Iterations: 800000
Encryption: AES-256-GCM
Salt: 16 bytes
Nonce: 12 bytes
Tag: 16 bytes
Key: 32 bytes
```

### app.pro changes

`widgets` is added because the password prompt uses `QInputDialog`.

```pro
QT += core quick network quickcontrols2 svg widgets
```

The new protected key storage files are added to the project:

```pro
SOURCES += \
    backend/protectedkeystore.cpp

HEADERS += \
    backend/protectedkeystore.h
```

### main.cpp changes

The application type is changed from `QGuiApplication` to `QApplication`.

Before:

```cpp
#include <QGuiApplication>

QGuiApplication app(argc, argv);
```

After:

```cpp
#include <QApplication>

QApplication app(argc, argv);
```

This allows the Qt Widgets password dialog to be used before the main QML UI loads.

### identitymanager.cpp changes

`identitymanager.cpp` is where the Moonlight client identity is loaded or created.

This fork changes that flow so the private key is protected by a startup password.

Main changes:

```text
1. Ask for a password at startup.
2. Load the existing client certificate from settings.
3. Decrypt the encrypted private key when protected key fields exist.
4. Exit immediately when the password is wrong or cancelled.
5. Migrate an old plaintext private key to encrypted storage.
6. Remove the old plaintext private key field after migration.
7. Encrypt newly generated private keys before saving them.
8. Keep the client certificate stored normally.
9. Verify that the certificate is readable.
10. Verify that the decrypted private key is readable.
11. Verify that the certificate and decrypted private key match.
12. Log the certificate SHA256 fingerprint for troubleshooting.
```

Expected first-run log:

```text
No existing credentials found
Wrote new protected identity credentials to settings
Identity certificate and private key match
Identity certificate SHA256: ...
```

Expected normal startup log with the correct password:

```text
Loaded protected private key from settings
Identity certificate and private key match
Identity certificate SHA256: ...
```

If the decrypted private key does not match the stored certificate, Moonlight exits with:

```text
Identity certificate and private key do not match
```

### Pairing behavior

The startup password is local only. It does not come from the host.

Moonlight generates its own client certificate and private key locally.

During pairing, the host records the Moonlight client certificate.

On later connections, Moonlight uses the private key to prove it is the same paired client.

Pairing normally does not change the client private key.

Deleting `Moonlight.ini` or the portable settings directory creates a new client identity, so hosts must be paired again.

### Security scope

This fork does not encrypt the whole configuration file.

It protects the Moonlight client private key so that a copied portable folder cannot directly reuse the paired client identity without the startup password.

It does not hide all host records or all settings in `Moonlight.ini`.

It does not protect against malware already running as the same user while Moonlight is open.

## Implementation notes: what this fork changes in code

This fork keeps the original Moonlight Qt functionality, but changes how the Moonlight client private key is stored and loaded.

The main goal is:

```text
Do not store the Moonlight client private key as plaintext in Moonlight.ini.
Ask for a startup password.
Use that password to encrypt/decrypt the client private key.
Exit immediately if the password is wrong or cancelled.
```

### Upstream Moonlight Qt summary

Moonlight Qt is the PC client for Moonlight Game Streaming. It connects to NVIDIA GameStream-compatible hosts such as Sunshine and allows remote game or desktop streaming with video, audio, input, gamepad, and related client features.

This fork only adds local password protection for the client private key. It does not change the core streaming purpose of Moonlight.

---

## Files added

### `app/backend/protectedkeystore.h`

This header declares helper functions for protected private key storage.

The helper is responsible for:

```text
1. Detecting whether Moonlight.ini already contains an encrypted private key.
2. Encrypting a plaintext private key with the startup password.
3. Decrypting the encrypted private key with the startup password.
4. Writing encrypted private key fields into QSettings.
```

Expected helper methods:

```cpp
ProtectedKeyStore::hasEncryptedPrivateKey(...)
ProtectedKeyStore::saveEncryptedPrivateKey(...)
ProtectedKeyStore::decryptPrivateKey(...)
```

### `app/backend/protectedkeystore.cpp`

This file implements the actual private key encryption and decryption.

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

It stores the encrypted key data in `Moonlight.ini` using fields like:

```ini
key_protection=password-v1
key_kdf=pbkdf2-sha256
key_iter=800000
key_salt=...
key_nonce=...
key_ciphertext=...
```

The old plaintext private key field should not remain:

```ini
key=...
```

---

## Files modified

### `app/app.pro`

`widgets` is added because the password prompt uses `QInputDialog`, which belongs to Qt Widgets.

Before:

```pro
QT += core quick network quickcontrols2 svg
```

After:

```pro
QT += core quick network quickcontrols2 svg widgets
```

The new protected key store source and header are added to the project:

```pro
SOURCES += \
    backend/protectedkeystore.cpp

HEADERS += \
    backend/protectedkeystore.h
```

When applying this patch to a newer upstream Moonlight Qt version, make sure the new `.cpp` file is included in the build system, otherwise the linker will fail.

---

### `app/main.cpp`

The application class is changed from `QGuiApplication` to `QApplication`.

Before:

```cpp
#include <QGuiApplication>

QGuiApplication app(argc, argv);
```

After:

```cpp
#include <QApplication>

QApplication app(argc, argv);
```

Reason:

```text
The original Moonlight UI is QML/Qt Quick, but this fork shows a startup password dialog using QInputDialog.
QInputDialog requires Qt Widgets, so QApplication is used instead of QGuiApplication.
```

---

### `app/backend/identitymanager.cpp`

This is the main file changed by the password protection patch.

Additional includes are added:

```cpp
#include "protectedkeystore.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QCryptographicHash>
#include <cstdlib>

#include <openssl/evp.h>
```

The exact include layout may differ between Moonlight versions, but these are needed for:

```text
protectedkeystore.h  -> encrypted private key helper
QInputDialog         -> startup password prompt
QLineEdit            -> password input mode
QCryptographicHash   -> certificate SHA256 log
cstdlib              -> std::exit(0)
openssl/evp.h        -> EVP_PKEY_eq / EVP_PKEY_cmp
```

---

## Password prompt logic

A new static helper is added near the top of `identitymanager.cpp`:

```cpp
static QString getPrivateKeyPassword()
```

Its behavior:

```text
1. Ask for the password once per process.
2. Cache the password in a static QString.
3. If the user cancels or enters an empty password, exit immediately.
4. Return the cached password for later private key decrypt/encrypt operations.
```

The dialog uses:

```cpp
QInputDialog::getText(..., QLineEdit::Password, ...)
```

This keeps the password hidden while typing.

---

## Certificate/private key match check

A new helper is added:

```cpp
static bool certificateMatchesPrivateKey(const QByteArray& pemCert, const QByteArray& pemKey)
```

Purpose:

```text
Verify that the certificate stored in Moonlight.ini matches the private key decrypted from key_ciphertext.
```

It does this by:

```text
1. Reading the PEM certificate with PEM_read_bio_X509.
2. Reading the PEM private key with PEM_read_bio_PrivateKey.
3. Extracting the public key from the certificate with X509_get_pubkey.
4. Comparing the certificate public key with the private key using:
   - EVP_PKEY_eq on OpenSSL 3.x
   - EVP_PKEY_cmp on older OpenSSL
```

This check prevents a broken state such as:

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

The SHA256 log is only the certificate fingerprint. The private key is not printed.

---

## `IdentityManager::createCredentials()` changes

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

The changed logic is:

```cpp
settings.setValue(SER_CERT, m_CachedPemCert);

if (!ProtectedKeyStore::saveEncryptedPrivateKey(settings, m_CachedPrivateKey, getPrivateKeyPassword())) {
    qFatal("Failed to encrypt private key");
}

settings.remove(SER_KEY);
settings.sync();

qInfo() << "Wrote new protected identity credentials to settings";
```

Important behavior:

```text
On first launch with no existing identity, the first password entered becomes the password for the newly generated client identity.
```

---

## `IdentityManager::IdentityManager()` changes

This constructor now handles three cases.

### Case 1: encrypted private key exists

If `Moonlight.ini` contains protected key fields:

```text
key_protection=password-v1
key_ciphertext=...
```

Moonlight asks for the password and tries to decrypt the private key.

If decryption succeeds:

```text
Loaded protected private key from settings
```

If decryption fails:

```text
Moonlight exits immediately.
```

No retry dialog is shown.

---

### Case 2: old plaintext private key exists

If the old plaintext field exists:

```ini
key=...
```

the fork migrates it.

Migration behavior:

```text
1. Load the old plaintext private key.
2. Ask for the startup password.
3. Encrypt the private key with the password.
4. Save encrypted key fields.
5. Remove the old plaintext key field.
6. Sync settings.
```

Expected log:

```text
Migrating plaintext private key to protected storage
```

This allows existing Moonlight settings to be upgraded without manually deleting the old identity.

---

### Case 3: no private key exists

If no certificate/private key exists, Moonlight creates a new identity.

Expected log:

```text
No existing credentials found
Wrote new protected identity credentials to settings
```

This is normal on first run or after deleting `Moonlight.ini`.

---

## Final identity validation

After loading or creating credentials, the fork still checks:

```text
1. Certificate is readable.
2. Private key is readable.
```

Then it adds one more check:

```text
3. Certificate and private key match.
```

Expected successful startup:

```text
Loaded protected private key from settings
Identity certificate and private key match
Identity certificate SHA256: ...
```

Expected first run:

```text
No existing credentials found
Wrote new protected identity credentials to settings
Identity certificate and private key match
Identity certificate SHA256: ...
```

Wrong password or cancelled dialog:

```text
Moonlight exits before loading the main UI.
```

---

## Pairing behavior

The startup password is local only.

It does not come from the host.

Moonlight generates its own client certificate and private key locally.

During pairing, the host records the Moonlight client certificate.

On later connections, Moonlight uses the private key to prove it is the same paired client.

Pairing normally does not change the client private key.

If `Moonlight.ini` or the portable settings directory is deleted, Moonlight generates a new client certificate/private key. Existing host pairings will no longer match, so hosts must be paired again.

---

## Forgotten password behavior

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

After reset:

```text
1. Moonlight asks for a new first-run password.
2. A new client certificate/private key is generated.
3. Hosts must be paired again.
```

---

## Security scope

This fork does not encrypt the whole configuration file.

It only protects the Moonlight client private key.

This means:

```text
Protected:
- Client private key
- Ability to reuse an already-paired client identity from a copied portable folder

Not protected:
- All host records
- All Moonlight settings
- Runtime memory after the correct password is entered
- Malware already running as the same user
```

The intended use case is portable-folder protection, not full disk or full configuration encryption.
