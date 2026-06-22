// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "net/pairing_crypto.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

namespace ase::net {

namespace {

// One-shot digest with the given EVP_MD. Returns raw bytes, "" on failure.
std::string digest(const EVP_MD* md, const std::string& data) {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return "";
  std::string out(EVP_MAX_MD_SIZE, '\0');
  unsigned int len = 0;
  bool ok = EVP_DigestInit_ex(ctx, md, nullptr) == 1 &&
            EVP_DigestUpdate(ctx, data.data(), data.size()) == 1 &&
            EVP_DigestFinal_ex(ctx, reinterpret_cast<unsigned char*>(&out[0]), &len) == 1;
  EVP_MD_CTX_free(ctx);
  if (!ok) return "";
  out.resize(len);
  return out;
}

// AES-128-ECB, no padding, in either direction. `data` must be a non-zero multiple of the 16-byte
// block size and `key` exactly 16 bytes (matches NvPairingManager::encrypt/decrypt).
std::string aes_ecb(const std::string& data, const std::string& key, bool encrypt) {
  if (key.size() != 16 || data.empty() || data.size() % 16 != 0) return "";

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return "";

  const auto* k = reinterpret_cast<const unsigned char*>(key.data());
  std::string out(data.size(), '\0');
  int outLen = 0;
  bool ok =
      (encrypt ? EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, k, nullptr)
               : EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, k, nullptr)) == 1;
  if (ok) {
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    const auto* in = reinterpret_cast<const unsigned char*>(data.data());
    auto* o = reinterpret_cast<unsigned char*>(&out[0]);
    ok = (encrypt ? EVP_EncryptUpdate(ctx, o, &outLen, in, static_cast<int>(data.size()))
                  : EVP_DecryptUpdate(ctx, o, &outLen, in, static_cast<int>(data.size()))) == 1;
  }
  EVP_CIPHER_CTX_free(ctx);
  if (!ok || static_cast<size_t>(outLen) != data.size()) return "";
  return out;
}

// Parse a PEM-encoded X509 cert from an in-memory buffer. Caller owns the returned X509 (X509_free).
X509* read_pem_cert(const std::string& pem) {
  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (!bio) return nullptr;
  X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free_all(bio);
  return cert;
}

}  // namespace

std::string random_bytes(int length) {
  if (length <= 0) return "";
  std::string out(static_cast<size_t>(length), '\0');
  if (RAND_bytes(reinterpret_cast<unsigned char*>(&out[0]), length) != 1) return "";
  return out;
}

std::string sha256(const std::string& data) { return digest(EVP_sha256(), data); }
std::string sha1(const std::string& data) { return digest(EVP_sha1(), data); }

std::string derive_aes_key(const std::string& salt, const std::string& pin, bool gen7plus) {
  std::string hashed = (gen7plus ? sha256 : sha1)(salt + pin);
  if (hashed.size() < 16) return "";
  hashed.resize(16);
  return hashed;
}

std::string aes_ecb_encrypt(const std::string& plaintext, const std::string& key) {
  return aes_ecb(plaintext, key, /*encrypt=*/true);
}
std::string aes_ecb_decrypt(const std::string& ciphertext, const std::string& key) {
  return aes_ecb(ciphertext, key, /*encrypt=*/false);
}

std::string signature_from_pem_cert(const std::string& pem_cert) {
  X509* cert = read_pem_cert(pem_cert);
  if (!cert) return "";
  const ASN1_BIT_STRING* sig = nullptr;
  X509_get0_signature(&sig, nullptr, cert);
  std::string out;
  if (sig) {
    out.assign(reinterpret_cast<const char*>(ASN1_STRING_get0_data(sig)),
               static_cast<size_t>(ASN1_STRING_length(sig)));
  }
  X509_free(cert);
  return out;
}

bool verify_signature(const std::string& data, const std::string& signature,
                      const std::string& pem_cert) {
  X509* cert = read_pem_cert(pem_cert);
  if (!cert) return false;
  EVP_PKEY* pubKey = X509_get_pubkey(cert);
  bool ok = false;
  if (pubKey) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx) {
      ok = EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pubKey) == 1 &&
           EVP_DigestVerifyUpdate(ctx, data.data(), data.size()) == 1 &&
           EVP_DigestVerifyFinal(ctx,
                                 reinterpret_cast<const unsigned char*>(signature.data()),
                                 signature.size()) == 1;
      EVP_MD_CTX_free(ctx);
    }
    EVP_PKEY_free(pubKey);
  }
  X509_free(cert);
  return ok;
}

std::string sign_message(const std::string& message, const std::string& pem_private_key) {
  BIO* bio = BIO_new_mem_buf(pem_private_key.data(), static_cast<int>(pem_private_key.size()));
  if (!bio) return "";
  EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free_all(bio);
  if (!key) return "";

  std::string out;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx) {
    size_t sigLen = 0;
    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, key) == 1 &&
        EVP_DigestSignUpdate(ctx, message.data(), message.size()) == 1 &&
        EVP_DigestSignFinal(ctx, nullptr, &sigLen) == 1) {
      out.resize(sigLen);
      if (EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char*>(&out[0]), &sigLen) == 1) {
        out.resize(sigLen);
      } else {
        out.clear();
      }
    }
    EVP_MD_CTX_free(ctx);
  }
  EVP_PKEY_free(key);
  return out;
}

}  // namespace ase::net
