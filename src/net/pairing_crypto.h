// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Cryptographic primitives for the GameStream (NvHTTP) pairing handshake — the Qt-free port of
// moonlight-qt's NvPairingManager crypto. Built directly on OpenSSL (the library we already link
// for moonlight-common-c), so no Qt / QCryptographicHash. Byte buffers are std::string; functions
// return "" / false on failure or misuse. Only compiled when ASE_WITH_OPENSSL is on.
#pragma once
#include <string>

namespace ase::net {

// `length` cryptographically-random bytes (OpenSSL RAND_bytes). "" if length <= 0 or RNG fails.
std::string random_bytes(int length);

// One-shot digests, raw bytes (not hex). "" on failure.
std::string sha256(const std::string& data);
std::string sha1(const std::string& data);

// The AES-128 pairing key: hash(salt || pin) truncated to 16 bytes. `gen7plus` selects SHA-256
// (true, GFE generation 7+) or SHA-1 (false), matching the server's major version. `pin` is the
// UTF-8 PIN string; `salt` is the 16 random bytes sent as the `salt` query param.
std::string derive_aes_key(const std::string& salt, const std::string& pin, bool gen7plus);

// AES-128-ECB, no padding (matches NvPairingManager::encrypt/decrypt). `key` must be 16 bytes and
// `data` length a non-zero multiple of 16; otherwise returns "". ECB with no IV is correct here —
// the GameStream pairing protocol defines it, and each block carries fresh random/hashed material.
std::string aes_ecb_encrypt(const std::string& plaintext, const std::string& key);
std::string aes_ecb_decrypt(const std::string& ciphertext, const std::string& key);

// Raw signature bytes embedded in a PEM-encoded X509 certificate
// (NvPairingManager::getSignatureFromCert / getSignatureFromPemCert). "" if the PEM won't parse.
std::string signature_from_pem_cert(const std::string& pem_cert);

// Verify `signature` over `data` against the public key in `pem_cert` (SHA-256). The pairing
// flow's MITM check. False on any parse/verify failure.
bool verify_signature(const std::string& data, const std::string& signature,
                      const std::string& pem_cert);

// SHA-256-sign `message` with a PEM-encoded private key (NvPairingManager::signMessage). "" on
// failure.
std::string sign_message(const std::string& message, const std::string& pem_private_key);

}  // namespace ase::net
