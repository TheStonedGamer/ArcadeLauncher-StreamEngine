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
std::vector<std::string> build_launch_args(const std::string& appsPath);

// Directory containing the running engine executable ("" if it can't be determined). Used to find the
// sunshine binary shipped alongside the engine.
std::string current_exe_dir();

// Locates and drives the bundled Sunshine fork binary as a managed child process.
class SunshineBackend {
 public:
  explicit SunshineBackend(std::string appsPath);
  SunshineBackend();
  ~SunshineBackend();

  SunshineBackend(const SunshineBackend&) = delete;
  SunshineBackend& operator=(const SunshineBackend&) = delete;

  // Path to the bundled sunshine binary, or "" if none was found.
  const std::string& binary_path() const { return binary_; }
  bool bundled() const { return !binary_.empty(); }

  // Whether the managed child is currently running (we started it and it hasn't exited).
  bool running();

  // Start the child (idempotent — true if already running). False if no binary is bundled or the
  // spawn failed (msg set).
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
