#include "protectedkeystore.h"

#include <QDebug>

#include <openssl/evp.h>
#include <openssl/rand.h>

#define SER_KEY_PROTECTION "key_protection"
#define SER_KEY_KDF "key_kdf"
#define SER_KEY_ITER "key_iter"
#define SER_KEY_SALT "key_salt"
#define SER_KEY_NONCE "key_nonce"
#define SER_KEY_CIPHERTEXT "key_ciphertext"

static const char* KEY_PROTECTION_VERSION = "password-v1";
static const char* KEY_KDF_NAME = "pbkdf2-sha256";

static constexpr int KEY_SIZE = 32;
static constexpr int SALT_SIZE = 16;
static constexpr int NONCE_SIZE = 12;
static constexpr int GCM_TAG_SIZE = 16;
static constexpr int PBKDF2_ITERATIONS = 800000;

static QByteArray randomBytes(int size)
{
    QByteArray data(size, Qt::Uninitialized);

    if (RAND_bytes(reinterpret_cast<unsigned char*>(data.data()), size) != 1) {
        qWarning() << "RAND_bytes failed";
        return QByteArray();
    }

    return data;
}

static QByteArray deriveKey(const QString& password,
                            const QByteArray& salt,
                            int iterations)
{
    QByteArray key(KEY_SIZE, Qt::Uninitialized);
    QByteArray passwordBytes = password.toUtf8();

    int ok = PKCS5_PBKDF2_HMAC(
        passwordBytes.constData(),
        passwordBytes.size(),
        reinterpret_cast<const unsigned char*>(salt.constData()),
        salt.size(),
        iterations,
        EVP_sha256(),
        key.size(),
        reinterpret_cast<unsigned char*>(key.data())
    );

    if (ok != 1) {
        qWarning() << "PBKDF2 failed";
        return QByteArray();
    }

    return key;
}

static QByteArray aes256GcmEncrypt(const QByteArray& plaintext,
                                   const QByteArray& key,
                                   const QByteArray& nonce)
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return QByteArray();
    }

    QByteArray ciphertext(plaintext.size(), Qt::Uninitialized);
    QByteArray tag(GCM_TAG_SIZE, Qt::Uninitialized);

    int len = 0;
    int ciphertextLen = 0;

    bool ok =
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
        EVP_EncryptInit_ex(
            ctx,
            nullptr,
            nullptr,
            reinterpret_cast<const unsigned char*>(key.constData()),
            reinterpret_cast<const unsigned char*>(nonce.constData())
        ) == 1 &&
        EVP_EncryptUpdate(
            ctx,
            reinterpret_cast<unsigned char*>(ciphertext.data()),
            &len,
            reinterpret_cast<const unsigned char*>(plaintext.constData()),
            plaintext.size()
        ) == 1;

    if (ok) {
        ciphertextLen = len;

        ok =
            EVP_EncryptFinal_ex(
                ctx,
                reinterpret_cast<unsigned char*>(ciphertext.data()) + len,
                &len
            ) == 1 &&
            EVP_CIPHER_CTX_ctrl(
                ctx,
                EVP_CTRL_GCM_GET_TAG,
                GCM_TAG_SIZE,
                tag.data()
            ) == 1;
    }

    EVP_CIPHER_CTX_free(ctx);

    if (!ok) {
        return QByteArray();
    }

    ciphertext.resize(ciphertextLen + len);

    // Store tag at the end: ciphertext || tag
    return ciphertext + tag;
}

static QByteArray aes256GcmDecrypt(const QByteArray& ciphertextWithTag,
                                   const QByteArray& key,
                                   const QByteArray& nonce)
{
    if (ciphertextWithTag.size() <= GCM_TAG_SIZE) {
        return QByteArray();
    }

    QByteArray ciphertext = ciphertextWithTag.left(ciphertextWithTag.size() - GCM_TAG_SIZE);
    QByteArray tag = ciphertextWithTag.right(GCM_TAG_SIZE);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return QByteArray();
    }

    QByteArray plaintext(ciphertext.size(), Qt::Uninitialized);

    int len = 0;
    int plaintextLen = 0;

    bool ok =
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
        EVP_DecryptInit_ex(
            ctx,
            nullptr,
            nullptr,
            reinterpret_cast<const unsigned char*>(key.constData()),
            reinterpret_cast<const unsigned char*>(nonce.constData())
        ) == 1 &&
        EVP_DecryptUpdate(
            ctx,
            reinterpret_cast<unsigned char*>(plaintext.data()),
            &len,
            reinterpret_cast<const unsigned char*>(ciphertext.constData()),
            ciphertext.size()
        ) == 1;

    if (ok) {
        plaintextLen = len;

        ok =
            EVP_CIPHER_CTX_ctrl(
                ctx,
                EVP_CTRL_GCM_SET_TAG,
                GCM_TAG_SIZE,
                const_cast<char*>(tag.constData())
            ) == 1 &&
            EVP_DecryptFinal_ex(
                ctx,
                reinterpret_cast<unsigned char*>(plaintext.data()) + len,
                &len
            ) == 1;
    }

    EVP_CIPHER_CTX_free(ctx);

    if (!ok) {
        return QByteArray();
    }

    plaintext.resize(plaintextLen + len);
    return plaintext;
}

bool ProtectedKeyStore::hasEncryptedPrivateKey(QSettings& settings)
{
    return settings.value(SER_KEY_PROTECTION).toString() == KEY_PROTECTION_VERSION &&
           settings.contains(SER_KEY_SALT) &&
           settings.contains(SER_KEY_NONCE) &&
           settings.contains(SER_KEY_CIPHERTEXT);
}

QByteArray ProtectedKeyStore::decryptPrivateKey(QSettings& settings,
                                                const QString& password)
{
    if (!hasEncryptedPrivateKey(settings)) {
        return QByteArray();
    }

    int iterations = settings.value(SER_KEY_ITER, PBKDF2_ITERATIONS).toInt();

    QByteArray salt = QByteArray::fromBase64(settings.value(SER_KEY_SALT).toByteArray());
    QByteArray nonce = QByteArray::fromBase64(settings.value(SER_KEY_NONCE).toByteArray());
    QByteArray ciphertext = QByteArray::fromBase64(settings.value(SER_KEY_CIPHERTEXT).toByteArray());

    QByteArray key = deriveKey(password, salt, iterations);
    if (key.isEmpty()) {
        return QByteArray();
    }

    return aes256GcmDecrypt(ciphertext, key, nonce);
}

bool ProtectedKeyStore::saveEncryptedPrivateKey(QSettings& settings,
                                                const QByteArray& privateKeyPem,
                                                const QString& password)
{
    QByteArray salt = randomBytes(SALT_SIZE);
    QByteArray nonce = randomBytes(NONCE_SIZE);

    if (salt.isEmpty() || nonce.isEmpty()) {
        return false;
    }

    QByteArray key = deriveKey(password, salt, PBKDF2_ITERATIONS);
    if (key.isEmpty()) {
        return false;
    }

    QByteArray ciphertext = aes256GcmEncrypt(privateKeyPem, key, nonce);
    if (ciphertext.isEmpty()) {
        return false;
    }

    settings.setValue(SER_KEY_PROTECTION, KEY_PROTECTION_VERSION);
    settings.setValue(SER_KEY_KDF, KEY_KDF_NAME);
    settings.setValue(SER_KEY_ITER, PBKDF2_ITERATIONS);
    settings.setValue(SER_KEY_SALT, QString::fromLatin1(salt.toBase64()));
    settings.setValue(SER_KEY_NONCE, QString::fromLatin1(nonce.toBase64()));
    settings.setValue(SER_KEY_CIPHERTEXT, QString::fromLatin1(ciphertext.toBase64()));

    settings.sync();
    return true;
}
