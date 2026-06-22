// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Unit tests for the GameStream pairing crypto (src/net/pairing_crypto.cpp). Needs OpenSSL, so it
// is only built in the ASE_WITH_OPENSSL configuration. Deterministic primitives are checked against
// published known-answer vectors (FIPS-180 SHA, FIPS-197 AES); the cert/sign/verify path is
// exercised against a throwaway self-signed key generated at runtime via OpenSSL.
#include <cstdio>
#include <string>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "net/pairing_crypto.h"

using namespace ase::net;

static int g_failures = 0;
#define CHECK(cond, what)                                                       \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (what), __FILE__, __LINE__);  \
      ++g_failures;                                                             \
    }                                                                          \
  } while (0)

// Local hex helpers so the test stays independent of gamestream_xml.
static std::string to_hex(const std::string& b) {
  static const char* d = "0123456789abcdef";
  std::string o;
  for (unsigned char c : b) { o += d[c >> 4]; o += d[c & 0xf]; }
  return o;
}
static std::string from_hex(const std::string& h) {
  auto v = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string o;
  for (size_t i = 0; i + 1 < h.size(); i += 2) o += static_cast<char>((v(h[i]) << 4) | v(h[i + 1]));
  return o;
}

static void test_sha() {
  // FIPS 180 examples for the input "abc".
  CHECK(to_hex(sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256(\"abc\")");
  CHECK(to_hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1(\"abc\")");
}

static void test_aes_kat() {
  // FIPS-197 Appendix B / C.1 single-block AES-128 known-answer.
  std::string key = from_hex("000102030405060708090a0b0c0d0e0f");
  std::string pt = from_hex("00112233445566778899aabbccddeeff");
  std::string ct = from_hex("69c4e0d86a7b0430d8cdb78070b4c55a");
  CHECK(aes_ecb_encrypt(pt, key) == ct, "AES-128-ECB encrypt KAT");
  CHECK(aes_ecb_decrypt(ct, key) == pt, "AES-128-ECB decrypt KAT");
}

static void test_aes_roundtrip_and_misuse() {
  std::string key = random_bytes(16);
  std::string pt = "sixteen bytes!!!";  // exactly one block
  CHECK(aes_ecb_decrypt(aes_ecb_encrypt(pt, key), key) == pt, "AES round-trips");
  CHECK(aes_ecb_encrypt("short", key) == "", "non-block-multiple rejected");
  CHECK(aes_ecb_encrypt(pt, "badkey") == "", "wrong key size rejected");
  CHECK(aes_ecb_encrypt("", key) == "", "empty input rejected");
}

static void test_derive_aes_key() {
  std::string salt = from_hex("00112233445566778899aabbccddeeff");
  std::string pin = "1234";
  // Wiring check: derived key == first 16 bytes of the corresponding hash of salt||pin.
  CHECK(derive_aes_key(salt, pin, true) == sha256(salt + pin).substr(0, 16),
        "gen7+ key = sha256(salt+pin)[:16]");
  CHECK(derive_aes_key(salt, pin, false) == sha1(salt + pin).substr(0, 16),
        "pre-gen7 key = sha1(salt+pin)[:16]");
  CHECK(derive_aes_key(salt, pin, true).size() == 16, "derived key is 16 bytes");
}

static void test_random_bytes() {
  CHECK(random_bytes(16).size() == 16, "random_bytes length");
  CHECK(random_bytes(0) == "" && random_bytes(-1) == "", "non-positive length rejected");
  CHECK(random_bytes(32) != random_bytes(32), "two draws differ");
}

// Generate a throwaway RSA-2048 self-signed cert + private key as PEM strings. Returns false on
// any OpenSSL failure. Portable across OpenSSL 1.1 / 3.x (no EVP_RSA_gen).
static bool make_identity(std::string& certPem, std::string& keyPem) {
  bool ok = false;
  EVP_PKEY* pkey = nullptr;
  EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  if (kctx && EVP_PKEY_keygen_init(kctx) == 1 &&
      EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 2048) == 1 && EVP_PKEY_keygen(kctx, &pkey) == 1) {
    X509* x = X509_new();
    if (x) {
      ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
      X509_gmtime_adj(X509_getm_notBefore(x), 0);
      X509_gmtime_adj(X509_getm_notAfter(x), 60 * 60 * 24);
      X509_set_pubkey(x, pkey);
      X509_NAME* name = X509_get_subject_name(x);
      X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                 reinterpret_cast<const unsigned char*>("ASE Test"), -1, -1, 0);
      X509_set_issuer_name(x, name);  // self-signed
      if (X509_sign(x, pkey, EVP_sha256()) != 0) {
        BIO* cb = BIO_new(BIO_s_mem());
        BIO* kb = BIO_new(BIO_s_mem());
        if (cb && kb && PEM_write_bio_X509(cb, x) == 1 &&
            PEM_write_bio_PrivateKey(kb, pkey, nullptr, nullptr, 0, nullptr, nullptr) == 1) {
          char* p = nullptr;
          long n = BIO_get_mem_data(cb, &p);
          certPem.assign(p, n);
          n = BIO_get_mem_data(kb, &p);
          keyPem.assign(p, n);
          ok = true;
        }
        BIO_free(cb);
        BIO_free(kb);
      }
      X509_free(x);
    }
  }
  EVP_PKEY_free(pkey);
  EVP_PKEY_CTX_free(kctx);
  return ok;
}

static void test_sign_verify_and_cert_signature() {
  std::string certPem, keyPem;
  if (!make_identity(certPem, keyPem)) {
    std::fprintf(stderr, "FAIL: could not generate test identity\n");
    ++g_failures;
    return;
  }

  CHECK(!signature_from_pem_cert(certPem).empty(), "cert carries a signature");
  CHECK(signature_from_pem_cert("not a cert") == "", "garbage PEM -> empty signature");

  std::string msg = "client pairing secret bytes";
  std::string sig = sign_message(msg, keyPem);
  CHECK(!sig.empty(), "sign_message produces a signature");
  CHECK(verify_signature(msg, sig, certPem), "signature verifies against matching cert");
  CHECK(!verify_signature("tampered", sig, certPem), "altered data fails verification");
  CHECK(!verify_signature(msg, sig, "not a cert"), "bad cert fails verification");
  CHECK(sign_message(msg, "not a key") == "", "bad private key -> empty signature");
}

int main() {
  test_sha();
  test_aes_kat();
  test_aes_roundtrip_and_misuse();
  test_derive_aes_key();
  test_random_bytes();
  test_sign_verify_and_cert_signature();

  if (g_failures == 0) {
    std::printf("all pairing-crypto tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d check(s) failed\n", g_failures);
  return 1;
}
