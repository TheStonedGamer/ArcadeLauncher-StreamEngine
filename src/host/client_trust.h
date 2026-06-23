// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Cert pre-authorization for host mode: seed a trusted GameStream client into Sunshine's state file
// WITHOUT the PIN handshake. Sunshine validates streaming clients against the certs it loads from
// `root.named_devices` in its state file (vendor/sunshine/src/nvhttp.cpp load_state); writing a
// client's cert there is exactly what a successful PIN pairing would persist. The launcher fetches
// the account's registered client certs and seeds them here before starting Sunshine, so a PC
// signed into the same account streams with no PIN (brokered zero-PIN auto-pairing, fix "A").
//
// These are PURE string→string transforms over the state JSON (no IO) so they unit-test cleanly;
// the caller does the file read/write and supplies the entry uuid.
#pragma once
#include <string>

namespace ase::host {

// The updated Sunshine state JSON with `certPem` present under root.named_devices. Idempotent: if
// the cert is already trusted the tree is returned structurally unchanged (only reserialized).
// `currentJson` may be empty or invalid — a fresh `{"root":{"named_devices":[...]}}` is produced,
// preserving any other existing root fields (e.g. root.uniqueid) otherwise. `name` is the friendly
// label Sunshine shows; `uuid` is the entry id the caller mints (e.g. net::generate_unique_id()).
std::string add_trusted_client(const std::string& currentJson, const std::string& name,
                               const std::string& certPem, const std::string& uuid);

// Whether `certPem` is already trusted (present in root.named_devices) in `currentJson`.
bool has_trusted_client(const std::string& currentJson, const std::string& certPem);

}  // namespace ase::host
