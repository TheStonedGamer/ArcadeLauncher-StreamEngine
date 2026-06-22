// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "host/sunshine_backend.h"

#include <cstdlib>
#include <filesystem>

#include "host/host_apps.h"

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <signal.h>
  #include <unistd.h>
#endif

namespace ase::host {

namespace {
#ifdef _WIN32
constexpr char kSep = '\\';
constexpr const char* kSunshineName = "sunshine.exe";
#else
constexpr char kSep = '/';
constexpr const char* kSunshineName = "sunshine";
#endif

bool fs_exists(const std::string& p) {
  std::error_code ec;
  return std::filesystem::exists(p, ec);
}
}  // namespace

std::string resolve_sunshine_binary(const std::string& engineDir, const std::string& envOverride,
                                    bool (*exists)(const std::string&)) {
  if (!envOverride.empty() && exists(envOverride)) return envOverride;
  if (!engineDir.empty()) {
    std::string candidate = engineDir;
    if (candidate.back() != kSep) candidate += kSep;
    candidate += kSunshineName;
    if (exists(candidate)) return candidate;
  }
  return "";
}

std::vector<std::string> build_launch_args(const std::string& appsPath) {
  std::vector<std::string> args;
  if (!appsPath.empty()) args.push_back("file_apps=" + appsPath);
  return args;
}

std::string current_exe_dir() {
#ifdef _WIN32
  char buf[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return "";
  std::string p(buf, n);
#else
  std::error_code ec;
  std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (ec) return "";
  std::string p = self.string();
#endif
  auto pos = p.find_last_of("/\\");
  return pos == std::string::npos ? "" : p.substr(0, pos);
}

SunshineBackend::SunshineBackend(std::string appsPath) : appsPath_(std::move(appsPath)) {
  const char* env = std::getenv("ARCADE_SUNSHINE");
  binary_ = resolve_sunshine_binary(current_exe_dir(), env ? env : "", fs_exists);
}

SunshineBackend::SunshineBackend() : SunshineBackend(default_apps_path()) {}

SunshineBackend::~SunshineBackend() {
  std::string msg;
  stop(msg);
}

bool SunshineBackend::running() {
#ifdef _WIN32
  if (!procHandle_) return false;
  DWORD r = WaitForSingleObject(static_cast<HANDLE>(procHandle_), 0);
  if (r == WAIT_TIMEOUT) return true;
  CloseHandle(static_cast<HANDLE>(procHandle_));
  procHandle_ = nullptr;
  return false;
#else
  if (pid_ <= 0) return false;
  int status = 0;
  pid_t r = waitpid(pid_, &status, WNOHANG);
  if (r == 0) return true;  // still running
  pid_ = 0;                 // reaped or gone
  return false;
#endif
}

bool SunshineBackend::start(std::string& msg) {
  if (running()) return true;
  if (binary_.empty()) {
    msg = "no bundled Sunshine binary found (set ARCADE_SUNSHINE or ship sunshine beside the engine)";
    return false;
  }
  const std::vector<std::string> args = build_launch_args(appsPath_);
#ifdef _WIN32
  std::string cmd = "\"" + binary_ + "\"";
  for (const auto& a : args) cmd += " " + a;
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<char> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back('\0');
  if (!CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                      &pi)) {
    msg = "CreateProcess failed (err " + std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(pi.hThread);
  procHandle_ = pi.hProcess;
  return true;
#else
  pid_t pid = fork();
  if (pid < 0) {
    msg = "fork failed";
    return false;
  }
  if (pid == 0) {
    // Child: exec the bundled sunshine.
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(binary_.c_str()));
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    execv(binary_.c_str(), argv.data());
    _exit(127);  // exec failed
  }
  pid_ = pid;
  return true;
#endif
}

bool SunshineBackend::stop(std::string& msg) {
  (void)msg;
  if (!running()) return true;
#ifdef _WIN32
  TerminateProcess(static_cast<HANDLE>(procHandle_), 0);
  WaitForSingleObject(static_cast<HANDLE>(procHandle_), 5000);
  CloseHandle(static_cast<HANDLE>(procHandle_));
  procHandle_ = nullptr;
  return true;
#else
  kill(pid_, SIGTERM);
  for (int i = 0; i < 50 && running(); ++i) {  // up to ~5s for graceful exit
    usleep(100 * 1000);
  }
  if (pid_ > 0) {  // still alive — force it
    kill(pid_, SIGKILL);
    int status = 0;
    waitpid(pid_, &status, 0);
    pid_ = 0;
  }
  return true;
#endif
}

}  // namespace ase::host
