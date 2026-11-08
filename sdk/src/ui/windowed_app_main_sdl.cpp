/**
 * @file        ui/windowed_app_main_sdl.cpp
 * @brief       Entry point for windowed applications (SDL3 windowing)
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/logging/api.h>
#include <rex/platform.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_sdl.h>

#if REX_PLATFORM_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif

#if REX_PLATFORM_ANDROID
// SDL's Java activity (SDLActivity) loads libmain.so and calls SDL_main on its own thread: SDL_main.h renames the
// main below.
#include <unistd.h>

#include <jni.h>

#include <chrono>
#include <thread>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>

#include <rex/main_android.h>
#include <rex/memory/utils.h>
#include <rex/thread.h>
#endif

namespace {

#if REX_PLATFORM_ANDROID
// The app's external files folder becomes the "executable folder" (rex/main_android.h): settings, logs, saves and the
// game data are read and written there. Must run before cvar::Init, which reads the settings file next to the
// executable.
bool InitializeAndroidProcess() {
  const char* folder = SDL_GetAndroidExternalStoragePath();
  if (!folder || !*folder) {
    SDL_Log("peggle2: no external files folder (%s)", SDL_GetError());
    return false;
  }
  // peggle2-android: landscape only. SDL asks Android for any orientation (FULL_USER) when the window is resizable
  // and this hint is empty, overriding the manifest's sensorLandscape.
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  // Touches are the game's touch controls (app/src/peggle2_touch.cpp), not a mouse for the keyboard-and-mouse pad.
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
  rex::filesystem::SetAndroidAppFolder(folder);
  if (chdir(folder) != 0) {
    SDL_Log("peggle2: chdir(%s) failed", folder);
  }
  rex::memory::AndroidInitialize();
  rex::thread::AndroidInitialize();
  SDL_Log("peggle2: app folder %s, API level %d", folder, int(rex::GetAndroidApiLevel()));
  // Diagnostics: every thread's stack in logcat at 20 and 40 s (to find where a start that shows nothing waits).
  if (access("stack_dumps", F_OK) == 0) {
    static const int kDumpSeconds[] = {20, 40};
    rex::StartAndroidStackDumps(kDumpSeconds, 2);
  }
  return true;
}
#endif

#if REX_PLATFORM_ANDROID
[[noreturn]] void AndroidExitToLauncher() {
  REXLOG_INFO("peggle2-android: the game ended; finishing the activity and the process");
  rex::FlushLogging();
  // peggle2-android (2026-11): this run ended normally, so the next start has nothing to report (Diagnostics.java
  // writes the marker and exports the logs of a run that did not get here).
  std::remove("logs/session.lock");
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env && activity) {
    jclass cls = env->GetObjectClass(activity);
    if (jmethodID finish = env->GetMethodID(cls, "finishAndRemoveTask", "()V"))
      env->CallVoidMethod(activity, finish);
    if (env->ExceptionCheck())
      env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // the task leaves the screen first
  }
  _exit(0);
}
#endif

int RunWindowedApp(int argc, char** argv) {
#if REX_PLATFORM_ANDROID
  if (!InitializeAndroidProcess()) {
    return EXIT_FAILURE;
  }
#endif
  auto remaining = rex::cvar::Init(argc, argv);
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

  int result;
  {
    rex::ui::SDLWindowedAppContext app_context;
    if (!app_context.Initialize()) {
      return EXIT_FAILURE;
    }

#if REX_PLATFORM_WIN32
    // Apartment-threaded COM for shell dialogs.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
      return EXIT_FAILURE;
    }
#endif

    std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(app_context);

    // Match remaining positional args to the app's expected options.
    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));

    result = app->OnInitialize() ? app_context.RunMainMessageLoop() : EXIT_FAILURE;
#if REX_PLATFORM_ANDROID
    // peggle2-android: the game ended (its Exit option ends the title) or the app is closing: out to the launcher
    // with the process gone. Teardown is skipped as on the PC's window close (a straggler thread can hold a lock it
    // needs), and a process that lived on would start the next launch on half-torn-down state.
    AndroidExitToLauncher();
#endif

    app->InvokeOnDestroy();
  }

#if REX_PLATFORM_WIN32
  CoUninitialize();
#endif

  return result;
}

#if REX_PLATFORM_WIN32
// Convert wide argv from CommandLineToArgvW to UTF-8 for cvar::Init.
std::vector<std::string> WideArgsToUtf8(int argc, wchar_t** wargv) {
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    std::wstring wide(wargv[i]);
    if (wide.empty()) {
      args.emplace_back();
      continue;
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr,
                                   0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), size,
                        nullptr, nullptr);
    args.push_back(std::move(utf8));
  }
  return args;
}
#endif

}  // namespace

#if REX_PLATFORM_WIN32

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hinstance_prev, LPWSTR command_line,
                    int show_cmd) {
  (void)hinstance;
  (void)hinstance_prev;
  (void)command_line;
  (void)show_cmd;

  int wargc = 0;
  wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  auto utf8_args = WideArgsToUtf8(wargc, wargv);
  LocalFree(wargv);

  std::vector<char*> argv_ptrs;
  argv_ptrs.reserve(utf8_args.size());
  for (auto& s : utf8_args) {
    argv_ptrs.push_back(s.data());
  }
  return RunWindowedApp(static_cast<int>(argv_ptrs.size()), argv_ptrs.data());
}

#else

int main(int argc, char* argv[]) {
  return RunWindowedApp(argc, argv);
}

#endif
