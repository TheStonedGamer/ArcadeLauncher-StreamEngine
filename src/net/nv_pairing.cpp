// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "net/nv_pairing.h"

#include "net/gamestream_xml.h"
#include "net/http_client.h"

namespace ase::net {

namespace {

// moonlight sends this fixed devicename on every pair request.
constexpr const char* kPairPrefix = "devicename=roth&updateState=1&";

// True if the response is a well-formed status-200 GameStream reply with <paired>1</paired>.
bool paired_ok(const std::string& xml) {
  return !xml.empty() && xml_attr(xml, "root", "status_code") == "200" &&
         xml_text(xml, "paired") == "1";
}

// SHA-256 (gen 7+) or SHA-1 (older), matching the server generation.
std::string gen_hash(const std::string& data, bool gen7plus) {
  return gen7plus ? sha256(data) : sha1(data);
}

// Best-effort cancel — tell the server to drop the half-finished pairing.
void unpair(const PairTransport& transport) { transport(false, "unpair", ""); }

}  // namespace

PairResult nv_pair(const PairTransport& transport, const ClientIdentity& identity,
                   const std::string& pin, int serverMajorVersion,
                   const std::function<void(const std::string&)>& setPinnedCert,
                   const std::function<std::string(int)>& randomFn) {
  const bool gen7 = serverMajorVersion >= 7;
  const size_t hashLen = gen7 ? 32u : 20u;

  const std::string salt = randomFn(16);
  const std::string aesKey = derive_aes_key(salt, pin, gen7);
  if (aesKey.empty()) return {PairState::Failed, ""};

  // --- Stage 1: getservercert (HTTP) ---------------------------------------------------------
  const std::string getCert = transport(
      false, "pair",
      std::string(kPairPrefix) + "phrase=getservercert&salt=" + to_hex(salt) +
          "&clientcert=" + to_hex(identity.certPem));
  if (!paired_ok(getCert)) return {PairState::Failed, ""};

  const std::string serverCertPem = xml_text_hex(getCert, "plaincert");
  if (serverCertPem.empty()) {  // server is already pairing with someone else
    unpair(transport);
    return {PairState::AlreadyInProgress, ""};
  }
  setPinnedCert(serverCertPem);  // pin for the stage-5 HTTPS request

  // --- Stage 2: clientchallenge (HTTP) -------------------------------------------------------
  const std::string randomChallenge = randomFn(16);
  const std::string encChallenge = aes_ecb_encrypt(randomChallenge, aesKey);
  const std::string challengeXml = transport(
      false, "pair", std::string(kPairPrefix) + "clientchallenge=" + to_hex(encChallenge));
  if (!paired_ok(challengeXml)) { unpair(transport); return {PairState::Failed, ""}; }

  const std::string challengeResp =
      aes_ecb_decrypt(xml_text_hex(challengeXml, "challengeresponse"), aesKey);
  if (challengeResp.size() < hashLen + 16) { unpair(transport); return {PairState::Failed, ""}; }

  // --- Stage 3: serverchallengeresp (HTTP) ---------------------------------------------------
  const std::string serverResponse = challengeResp.substr(0, hashLen);
  const std::string clientSecret = randomFn(16);

  std::string toHash = challengeResp.substr(hashLen, 16);  // server's challenge
  toHash += signature_from_pem_cert(identity.certPem);
  toHash += clientSecret;
  std::string paddedHash = gen_hash(toHash, gen7);
  paddedHash.resize(32, '\0');  // pad SHA-1's 20 bytes up to a 2-block AES payload

  const std::string respXml = transport(
      false, "pair",
      std::string(kPairPrefix) + "serverchallengeresp=" + to_hex(aes_ecb_encrypt(paddedHash, aesKey)));
  if (!paired_ok(respXml)) { unpair(transport); return {PairState::Failed, ""}; }

  const std::string pairingSecret = xml_text_hex(respXml, "pairingsecret");
  if (pairingSecret.size() <= 16) { unpair(transport); return {PairState::Failed, ""}; }
  const std::string serverSecret = pairingSecret.substr(0, 16);
  const std::string serverSignature = pairingSecret.substr(16);

  // The server must have signed its secret with the cert it presented (MITM check)...
  if (!verify_signature(serverSecret, serverSignature, serverCertPem)) {
    unpair(transport);
    return {PairState::Failed, ""};
  }
  // ...and its hashed response must prove it derived the same AES key from the PIN.
  std::string expected = randomChallenge + signature_from_pem_cert(serverCertPem) + serverSecret;
  if (gen_hash(expected, gen7) != serverResponse) {
    unpair(transport);
    return {PairState::PinWrong, ""};
  }

  // --- Stage 4: clientpairingsecret (HTTP) ---------------------------------------------------
  const std::string clientPairingSecret = clientSecret + sign_message(clientSecret, identity.keyPem);
  const std::string secretXml = transport(
      false, "pair", std::string(kPairPrefix) + "clientpairingsecret=" + to_hex(clientPairingSecret));
  if (!paired_ok(secretXml)) { unpair(transport); return {PairState::Failed, ""}; }

  // --- Stage 5: pairchallenge (HTTPS — proves the pinned cert + client cert now form a TLS pair) -
  const std::string challengeFinal =
      transport(true, "pair", std::string(kPairPrefix) + "phrase=pairchallenge");
  if (!paired_ok(challengeFinal)) { unpair(transport); return {PairState::Failed, ""}; }

  return {PairState::Paired, serverCertPem};
}

PairResult pair_with_client(NvHttpClient& client, const ClientIdentity& identity,
                            const std::string& pin, int serverMajorVersion) {
  PairTransport transport = [&client](bool https, const std::string& command,
                                      const std::string& args) -> std::string {
    constexpr int kTimeoutMs = 5000;
    return https ? client.get_https(command, args, kTimeoutMs)
                 : client.get_http(command, args, kTimeoutMs);
  };
  auto setPinned = [&client](const std::string& pem) { client.set_pinned_server_cert(pem); };
  return nv_pair(transport, identity, pin, serverMajorVersion, setPinned);
}

}  // namespace ase::net
