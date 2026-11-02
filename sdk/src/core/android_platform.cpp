/**
 * Android process glue: API level, the app's files folder, and the SDK's Android declarations that had no definition
 * (content:// URIs are not used by this port: the game data is a plain file in the app's folder).
 */
#include <rex/main_android.h>

#include <android/api-level.h>
#include <android/log.h>
#include <dirent.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unwind.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include <rex/filesystem.h>
#include <rex/system.h>

namespace rex {

int32_t GetAndroidApiLevel() {
  static const int32_t level = android_get_device_api_level();
  return level;
}

namespace filesystem {

namespace {
std::filesystem::path& AppFolder() {
  static std::filesystem::path folder;
  return folder;
}
}  // namespace

void SetAndroidAppFolder(const std::filesystem::path& folder) { AppFolder() = folder; }
const std::filesystem::path& GetAndroidAppFolder() { return AppFolder(); }

void AndroidInitialize() {}
void AndroidShutdown() {}
bool IsAndroidContentUri(const std::string_view source) { return source.starts_with("content://"); }
int OpenAndroidContentFileDescriptor(const std::string_view, const char*) { return -1; }

}  // namespace filesystem

namespace {

// One thread at a time: the dumper signals it and waits until its handler has logged the stack.
constexpr int kMaxFrames = 64;
std::atomic<int> g_dump_done{0};
uintptr_t g_frames[kMaxFrames];
int g_frame_count = 0;

_Unwind_Reason_Code CollectFrame(_Unwind_Context* context, void*) {
  const uintptr_t pc = _Unwind_GetIP(context);
  if (pc && g_frame_count < kMaxFrames)
    g_frames[g_frame_count++] = pc;
  return g_frame_count < kMaxFrames ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void DumpSignalHandler(int, siginfo_t*, void*) {
  g_frame_count = 0;
  _Unwind_Backtrace(CollectFrame, nullptr);
  g_dump_done.store(1, std::memory_order_release);
}

void DumpThreads(int round) {
  const pid_t self = gettid();
  std::vector<pid_t> tids;
  if (DIR* dir = opendir("/proc/self/task")) {
    while (dirent* entry = readdir(dir)) {
      const int tid = atoi(entry->d_name);
      if (tid > 0 && tid != self)
        tids.push_back(tid);
    }
    closedir(dir);
  }
  __android_log_print(ANDROID_LOG_INFO, "peggle2-stacks", "round %d: %zu threads", round, tids.size());
  for (pid_t tid : tids) {
    char path[64], name[64] = "?";
    snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
    if (FILE* f = fopen(path, "r")) {
      if (fgets(name, sizeof(name), f))
        name[strcspn(name, "\n")] = 0;
      fclose(f);
    }
    g_dump_done.store(0, std::memory_order_release);
    if (syscall(SYS_tgkill, getpid(), tid, SIGRTMIN + 10) != 0)
      continue;
    bool done = false;
    for (int i = 0; i < 200 && !(done = g_dump_done.load(std::memory_order_acquire)); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!done) {
      __android_log_print(ANDROID_LOG_INFO, "peggle2-stacks", "r%d tid %d (%s): no answer (signal blocked)", round, tid,
                          name);
      continue;
    }
    for (int i = 0; i < g_frame_count; ++i) {
      Dl_info info{};
      const uintptr_t pc = g_frames[i];
      if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
        const char* lib = strrchr(info.dli_fname, '/');
        __android_log_print(ANDROID_LOG_INFO, "peggle2-stacks", "r%d tid %d (%s) #%02d %s+0x%zx %s", round, tid, name,
                            i, lib ? lib + 1 : info.dli_fname, size_t(pc - uintptr_t(info.dli_fbase)),
                            info.dli_sname ? info.dli_sname : "");
      } else {
        __android_log_print(ANDROID_LOG_INFO, "peggle2-stacks", "r%d tid %d (%s) #%02d 0x%zx", round, tid, name, i,
                            size_t(pc));
      }
    }
  }
}

}  // namespace

void StartAndroidStackDumps(const int* seconds, int count) {
  struct sigaction action {};
  action.sa_sigaction = DumpSignalHandler;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigaction(SIGRTMIN + 10, &action, nullptr);
  std::vector<int> times(seconds, seconds + count);
  std::thread([times] {
    int elapsed = 0, round = 0;
    for (int at : times) {
      std::this_thread::sleep_for(std::chrono::seconds(at - elapsed));
      elapsed = at;
      DumpThreads(++round);
    }
  }).detach();
}

bool InitializeAndroidSystemForApplicationContext() { return true; }
void ShutdownAndroidSystem() {}

}  // namespace rex
