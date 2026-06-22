// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "net/identity.h"

#include <cstdint>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

namespace ase::net {

namespace {

// Generate an RSA-2048 keypair. Portable across OpenSSL 1.1 / 3.x (no EVP_RSA_gen). nullptr on fail.
EVP_PKEY* rsa_2048() {
  EVP_PKEY* pkey = nullptr;
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  if (ctx && EVP_PKEY_keygen_init(ctx) == 1 && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) == 1) {
    EVP_PKEY_keygen(ctx, &pkey);
  }
  EVP_PKEY_CTX_free(ctx);
  return pkey;
}

// Read the whole contents of a memory BIO into a std::string.
std::string bio_to_string(BIO* bio) {
  char* data = nullptr;
  long len = BIO_get_mem_data(bio, &data);
  return std::string(data, static_cast<size_t>(len < 0 ? 0 : len));
}

}  // namespace

bool generate_identity(ClientIdentity& out) {
  EVP_PKEY* pk = rsa_2048();
  if (!pk) return false;

  bool ok = false;
  X509* cert = X509_new();
  BIO* keyBio = BIO_new(BIO_s_mem());
  BIO* certBio = BIO_new(BIO_s_mem());
  X509_NAME* name = X509_NAME_new();

  if (cert && keyBio && certBio && name) {
    X509_set_version(cert, 2);  // X509 v3
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 0);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 60L * 60 * 24 * 365 * 20);  // 20 years
    X509_set_pubkey(cert, pk);

    X509_NAME_add_entry_by_txt(
        name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("NVIDIA GameStream Client"), -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, name);  // self-signed

    if (X509_sign(cert, pk, EVP_sha256()) != 0 &&
        PEM_write_bio_PrivateKey(keyBio, pk, nullptr, nullptr, 0, nullptr, nullptr) == 1 &&
        PEM_write_bio_X509(certBio, cert) == 1) {
      out.keyPem = bio_to_string(keyBio);
      out.certPem = bio_to_string(certBio);
      ok = !out.keyPem.empty() && !out.certPem.empty();
    }
  }

  X509_NAME_free(name);
  BIO_free(certBio);
  BIO_free(keyBio);
  X509_free(cert);
  EVP_PKEY_free(pk);
  return ok;
}

std::string generate_unique_id() {
  uint64_t uid = 0;
  if (RAND_bytes(reinterpret_cast<unsigned char*>(&uid), sizeof(uid)) != 1) return "";
  static const char* digits = "0123456789abcdef";
  std::string out;
  // Lowercase hex, most-significant nibble first, no leading-zero trimming (stable 16 chars).
  for (int shift = 60; shift >= 0; shift -= 4) out += digits[(uid >> shift) & 0xf];
  return out;
}

}  // namespace ase::net
