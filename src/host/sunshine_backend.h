// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Host-mode Sunshine driver — locates and runs the bundled Sunshine fork as a managed child process.
//
// The engine does NOT link Sunshine into its own binary: Sunshine is a full server (its own
// main()/nvhttp/rtsp threads + main loop), and on Windows it builds only under MinGW/UCRT64 while the
// engine builds under MSVC — you cannot link a MinGW C++ archive into an MSVC binary. Driving the
// vendored fork's `sunshine[.exe]` as a child process is the portable, single-toolchain-agnostic way
// to embed it: the engine ships the binary beside itself, points it at the engine-managed apps.json
// (see host_apps.h), and controls its lifecycle (start/stop/running) over host.* IPC.
#pragma once
#include <string>
#include <vector>

namespace ase::host {

// Resolve the bundled Sunshine binary. Order: `envOverride` (if it names an existing file), then
// `<engineDir>/sunshine` (`.exe` on Windows). Returns "" if neither exists — `exists` is injected so
// this is unit-testable without touching the filesystem.
std::string resolve_sunshine_binary(const std::string& engineDir, const std::string& envOverride,
                                    bool (*exists)(const std::string&));

// Build the child argv (after the binary) to launch Sunshine against an apps file. Sunshine accepts
// `key=value` config overrides on the command line; `file_apps=<path>` makes it read our catalog.
// We also pin Sunshine's state file and its server cert/key to fixed paths in the same dir
// (see host_state_path/host_cert_path) so the engine can (a) seed trusted client certs into the
// state file for zero-PIN auto-pairing and (b) read back the server cert for host.deviceInfo.
std::vector<std::string> build_launch_args(const std::string& appsPath);

// Engine-pinned Sunshine paths, all siblings of `appsPath` (the launcher-controlled config dir).
// These MUST match what build_launch_args passes so the trust/deviceInfo handlers read & write the
// exact files Sunshine uses.
std::string host_state_path(const std::string& appsPath);  // file_state — trusted clients live here
std::string host_cert_path(const std::string& appsPath);   // cert — Sunshine's server cert PEM
std::string host_pkey_path(const std::string& appsPath);   // pkey — Sunshine's server private key

// Directory containing the running engine executable ("" if it can't be determined). Used to find the
// sunshine binary shipped alongside the engine.
std::string current_exe_dir();

// The working directory Sunshine must run from: the directory containing its binary. Sunshine
// resolves its `assets/` (notably `assets/shaders/directx/*.hlsl`) RELATIVE to the current working
// directory, so if we spawn it with the engine's CWD inherited, shader compilation fails
// (0x80070003 PATH_NOT_FOUND) → "Platform failed to initialize" → Sunshine aborts before minting
// its server cert or serving GameStream. Spawning with this as the child CWD makes the bundled
// portable layout (sunshine.exe beside its assets/) resolve correctly, exactly as double-clicking
// sunshine.exe from its folder would. "" when `binary` has no parent (don't override the CWD then).
std::string sunshine_work_dir(const std::string& binary);

// Locates and drives the bundled Sunshine fork binary as a managed child process.
class SunshineBackend {
 public:
  explicit SunshineBackend(std::string appsPath);
  SunshineBackend();
  ~SunshineBackend();

  SunshineBackend(const SunshineBackend&) = delete;
  SunshineBackend& operator=(const SunshineBackend&) = delete;

  // Path to the resolved engine-managed sunshine binary (env-override sidecar OR the copy shipped
  // beside the engine), "" if none found. Never a system-installed Sunshine.
  const std::string& binary_path() const { return binary_; }
  // True when a startable engine-managed Sunshine binary is available (so the launcher needn't
  // download its sidecar).
  bool bundled() const { return !binary_.empty(); }

  // Whether the managed child is currently running (we started it and it hasn't exited). This is
  // the "managed" axis — true only for a Sunshine WE spawned, which is the only one we may stop.
  bool running();

  // Whether a Sunshine host is active on this machine: our managed child only. We do NOT adopt a
  // foreign Sunshine that merely happens to be listening — it would serve a cert the client never
  // pinned. Equivalent to running(); kept as a named seam for the host.* handlers.
  bool host_active();

  // Start hosting (idempotent). If our child already runs, succeeds without spawning. Otherwise
  // spawns the resolved engine-managed binary; if a foreign Sunshine already holds the GameStream
  // ports the spawn fails to bind (honest error) rather than silently adopting it. False if no
  // binary is available to start (msg set).
  bool start(std::string& msg);
  // Stop the managed child (idempotent — true once it is not running).
  bool stop(std::string& msg);

  const std::string& apps_path() const { return appsPath_; }

 private:
  std::string appsPath_;
  std::string binary_;
#ifdef _WIN32
  void* procHandle_ = nullptr;  // HANDLE to the child (nullptr = not started)
#else
  int pid_ = 0;  // child pid (0 = not started)
#endif
};

}  // namespace ase::host
