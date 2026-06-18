#include "protectedkeystore.h"

#include <QDebug>

#include <openssl/evp.h>
#include <openssl/rand.h>

#define KEY_PROTECTION "key_protection"
#define KEY_KDF "key_kdf"
#define KEY_ITER "key_iter"
#define KEY_SALT "key_salt"
#define KEY_NONCE "key_nonce"
#define KEY_CIPHERTEXT "key_ciphertext"

static const char* kProtectionVersion = "password-v1";
static const char* kKdfName = "pbkdf2-sha256";

static const int kSaltSize = 16;
static const int kNonceSize = 12;
static const int kTagSize = 16;
static const int kKeySize = 32;
static const int kIterations = 800000;

static QByteArray fromBase64Setting(QSettings& settings, const char* name)
{
    return QByteArray::fromBase64(settings.value(name).toString().toLatin1());
}

static void setBase64Setting(QSettings& settings, const char* name, const QByteArray& value)
{
    settings.setValue(name, QString::fromLatin1(value.toBase64()));
}

static bool deriveKey(const QString& password, const QByteArray& salt, QByteArray& key)
{
    QByteArray passwordBytes = password.toUtf8();

    key.resize(kKeySize);

    return PKCS5_PBKDF2_HMAC(
        passwordBytes.constData(),
        passwordBytes.size(),
        reinterpret_cast<const unsigned char*>(salt.constData()),
        salt.size(),
        kIterations,
        EVP_sha256(),
        key.size(),
        reinterpret_cast<unsigned char*>(key.data())
    ) == 1;
}

bool ProtectedKeyStore::hasEncryptedPrivateKey(QSettings& settings)
{
    return settings.value(KEY_PROTECTION).toString() == QString::fromLatin1(kProtectionVersion)
        && settings.value(KEY_KDF).toString() == QString::fromLatin1(kKdfName)
        && settings.contains(KEY_ITER)
        && settings.contains(KEY_SALT)
        && settings.contains(KEY_NONCE)
        && settings.contains(KEY_CIPHERTEXT);
}

QByteArray ProtectedKeyStore::decryptPrivateKey(QSettings& settings, const QString& password)
{
    if (!hasEncryptedPrivateKey(settings)) {
        return QByteArray();
    }

    if (settings.value(KEY_ITER).toInt() != kIterations) {
        return QByteArray();
    }

    QByteArray salt = fromBase64Setting(settings, KEY_SALT);
    QByteArray nonce = fromBase64Setting(settings, KEY_NONCE);
    QByteArray encryptedBlob = fromBase64Setting(settings, KEY_CIPHERTEXT);

    if (salt.size() != kSaltSize || nonce.size() != kNonceSize || encryptedBlob.size() <= kTagSize) {
        return QByteArray();
    }

    QByteArray tag = encryptedBlob.right(kTagSize);
    QByteArray ciphertext = encryptedBlob.left(encryptedBlob.size() - kTagSize);

    QByteArray key;
    if (!deriveKey(password, salt, key)) {
        return QByteArray();
    }

    QByteArray plaintext;
    plaintext.resize(ciphertext.size());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        return QByteArray();
    }

    bool ok = false;
    int outLen = 0;
    int finalLen = 0;

    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            break;
        }

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1) {
            break;
        }

        if (EVP_DecryptInit_ex(
                ctx,
                nullptr,
                nullptr,
                reinterpret_cast<const unsigned char*>(key.constData()),
                reinterpret_cast<const unsigned char*>(nonce.constData())) != 1) {
            break;
        }

        if (EVP_DecryptUpdate(
                ctx,
                reinterpret_cast<unsigned char*>(plaintext.data()),
                &outLen,
                reinterpret_cast<const unsigned char*>(ciphertext.constData()),
                ciphertext.size()) != 1) {
            break;
        }

        if (EVP_CIPHER_CTX_ctrl(
                ctx,
                EVP_CTRL_GCM_SET_TAG,
                tag.size(),
                const_cast<char*>(tag.constData())) != 1) {
            break;
        }

        if (EVP_DecryptFinal_ex(
                ctx,
                reinterpret_cast<unsigned char*>(plaintext.data()) + outLen,
                &finalLen) != 1) {
            break;
        }

        plaintext.resize(outLen + finalLen);
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);

    if (!ok) {
        return QByteArray();
    }

    return plaintext;
}

bool ProtectedKeyStore::saveEncryptedPrivateKey(QSettings& settings,
                                                const QByteArray& privateKeyPem,
                                                const QString& password)
{
    QByteArray salt;
    QByteArray nonce;

    salt.resize(kSaltSize);
    nonce.resize(kNonceSize);

    if (RAND_bytes(reinterpret_cast<unsigned char*>(salt.data()), salt.size()) != 1) {
        return false;
    }

    if (RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()), nonce.size()) != 1) {
        return false;
    }

    QByteArray key;
    if (!deriveKey(password, salt, key)) {
        return false;
    }

    QByteArray ciphertext;
    ciphertext.resize(privateKeyPem.size());

    QByteArray tag;
    tag.resize(kTagSize);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        return false;
    }

    bool ok = false;
    int outLen = 0;
    int finalLen = 0;

    do {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            break;
        }

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1) {
            break;
        }

        if (EVP_EncryptInit_ex(
                ctx,
                nullptr,
                nullptr,
                reinterpret_cast<const unsigned char*>(key.constData()),
                reinterpret_cast<const unsigned char*>(nonce.constData())) != 1) {
            break;
        }

        if (EVP_EncryptUpdate(
                ctx,
                reinterpret_cast<unsigned char*>(ciphertext.data()),
                &outLen,
                reinterpret_cast<const unsigned char*>(privateKeyPem.constData()),
                privateKeyPem.size()) != 1) {
            break;
        }

        if (EVP_EncryptFinal_ex(
                ctx,
                reinterpret_cast<unsigned char*>(ciphertext.data()) + outLen,
                &finalLen) != 1) {
            break;
        }

        ciphertext.resize(outLen + finalLen);

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) != 1) {
            break;
        }

        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);

    if (!ok) {
        return false;
    }

    QByteArray encryptedBlob = ciphertext + tag;

    settings.setValue(KEY_PROTECTION, QString::fromLatin1(kProtectionVersion));
    settings.setValue(KEY_KDF, QString::fromLatin1(kKdfName));
    settings.setValue(KEY_ITER, kIterations);

    setBase64Setting(settings, KEY_SALT, salt);
    setBase64Setting(settings, KEY_NONCE, nonce);
    setBase64Setting(settings, KEY_CIPHERTEXT, encryptedBlob);

    return true;
}
