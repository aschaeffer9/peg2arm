#pragma once
/**
 * Android process glue (Xenia's main_android.h, which the SDK's Android code includes but did not carry).
 *
 * The app's files live in its external files folder (Android/data/<package>/files: SDL_GetAndroidExternalStoragePath),
 * which the SDL entry point registers before anything else runs. GetExecutableFolder() returns that folder on
 * Android, so everything the PC build keeps next to its executable (peggle2.toml, logs/, game_root.nxpack, the 1080p
 * pack, the shader library, saves) is kept there: a folder the user can reach over USB / adb without root.
 */

#include <cstdint>
#include <filesystem>

namespace rex {

// android_get_device_api_level(), cached.
int32_t GetAndroidApiLevel();

// Diagnostics (peggle2-android): at the given seconds after the call, every thread's native stack goes to logcat
// (tag peggle2-stacks, "libmain.so+0x..." frames to symbolize against the unstripped library). Release phones do
// not let adb's debuggerd dump an app's threads without root.
void StartAndroidStackDumps(const int* seconds, int count);

namespace filesystem {
// Called once by the SDL entry point, before cvars and logging start.
void SetAndroidAppFolder(const std::filesystem::path& folder);
// Empty until set.
const std::filesystem::path& GetAndroidAppFolder();
}  // namespace filesystem

}  // namespace rex
