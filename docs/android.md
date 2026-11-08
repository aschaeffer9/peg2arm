# peg2arm: Peggle 2 on Android

peg2arm builds peggle2-nx for 64-bit ARM Android phones, with touch controls. It was developed on a Galaxy S25+
(Snapdragon 8 Elite, Adreno 830, Android 16) and needs a device with **Vulkan 1.1, arm64-v8a and Android 11 (API 30)
or newer** — that includes Amazon Fire OS 8 tablets (a Fire Max 11's Mali-G57 MC2 reports Vulkan 1.1.131). It uses the
same recompiled game and the same Renut renderer as the Switch and PC builds. Graphics go through Vulkan, through
SDL3's Android activity.

One warning about 1.1: it is the *minimum* the port is built for, not a formality. A Vulkan 1.1 driver may allow only
4 bound descriptor sets, and buffer device addresses (which the renderer's shaders read their data through) are an
extension there, not core. 0.1 got both wrong, and on such a GPU it showed a black screen, not an error message. Since
0.2 the renderer also checks, at start-up, the features it cannot run without (`VK_KHR_buffer_device_address`,
`shaderInt64`, descriptor indexing, and room for its 8192-texture table) and names the missing one instead.

As with the other builds, nothing from the game is in this repository. You make everything from your own copy of
the game.

## Controls

| In a level | |
|---|---|
| Drag | The cannon follows your finger, along the ball's arc, so the shot lands under your finger |
| Hold a finger still | Aims where it rests (fine aim without moving) |
| Tap | Fires along the current aim (a tap does not move the aim) |
| Two fingers held | Speeds the ball up |
| Pause button (top left, in the bar beside the picture) | The game's pause screen: Resume, Restart, Help & Options, Quit |
| Back (button or gesture) | Pause |

| In menus | |
|---|---|
| Tap | Presses the button under your finger, otherwise A |
| Swipe sideways | The adventure map's previous / next area |
| Back | Back one screen |

The sound button under the pause button mutes and unmutes, and the setting is kept. Leaving the app pauses a level.
Coming back puts you on the game's pause screen, on a new drawing surface. The game's own Exit, on its main menu,
closes the app.

There is no pinch zoom. Peggle 2 has no zoom the player controls; its camera zooms by itself near the last orange
peg.

The aim, pointer and tutorial handling follows [peggle2-mac](https://github.com/macprotips/peggle2-mac)'s game hooks
(MIT). It uses the same game functions and offsets, with the mouse replaced by touches. See
`app/src/peggle2_touch.cpp`.

## Build

1. **The PC build first.** Follow [building.md](building.md) up to a PC build that starts. The Android build reuses
   its generated code and its shader cache (`out/pc`). Run the PC build once through its intro and menus, so it can
   capture the shaders the game builds at run time.
2. **Tools.**
   - Android NDK r28c.
   - Android SDK: build-tools and the `android-36` platform; the command-line tools or Android Studio's SDK manager
     will do.
   - A JDK 17 or 21.
   - CMake and Ninja. The VS 2022 Build Tools carry both.
   - Python 3.12 with the packages from building.md.
3. **Build.**

   ```
   powershell -ExecutionPolicy Bypass -File tools\android\build_android.ps1 -Ndk C:\android\android-ndk-r28c
   ```

   This writes `out\android\peggle2.apk`, with a summary in `out\android_report.txt`. Add `-Device <serial or ip:port>`
   to install it over adb as well. The script:

   1. Converts the PC shader cache to SPIR-V.
   2. Makes sure every hooked game function is called through its hook (`tools/direct_calls.py --redo`).
   3. Builds `libmain.so` with the NDK's CMake toolchain (`android-30`, arm64-v8a, static libc++). The NDK platform
      is the APK's `minSdkVersion`, from the script's `-MinSdk` (default 30): one number for both, so the library's
      API level and the manifest's cannot disagree.
   4. Packages and signs the APK without Gradle (`tools/android/package_apk.py`), with a debug key made in `out/` the
      first time.

## Game data on the phone

The app reads the same files the Switch build does:

- `game_root.nxpack` and `game_root.nxidx`
- `peggle2_hd.pack`
- `peggle2_shaders.mesp` and `peggle2_shaders.mesp.idx`

It reads them from its own folder, `Android/data/com.ari.peggle2/files/`. Make them with the
[installer page](https://chanseyisthebest.github.io/peggle2-nx-installer/) from your own copy (README "How to
install"). Then install the APK and copy them over:

```
powershell -ExecutionPolicy Bypass -File tools\android\push_data.ps1 -Data C:\path\peggle2-nx.zip -Device <serial or ip:port>
```

The data is 2.7 GB. Over Wi-Fi adb it takes a while; USB is faster.

The app's other files are in the same folder:

| Path | What it is |
|---|---|
| `peggle2/` | saves and the profile |
| `logs/` | logs, including `plume_stderr.log` |
| `cache/` | the pipeline cache |
| `peggle2.toml` | settings, read after the built-in Android defaults (`app/peggle2_android_defaults.toml`) |

Uninstalling the app deletes this folder, saves included. Copy `peggle2/` somewhere first to keep your progress.

## What the Android build changes

- **Platform layer.** `sdk/src/core/android_platform.cpp` and `sdk/src/ui/surface_android.cpp`, plus fixes for
  bionic and libc++:
  - fibers in AArch64 assembly
  - no `std::jthread`
  - no float `from_chars`
  - no `std::atomic_ref` (`sdk/include/rex/atomic_ref.h`)
  - no robust mutexes
- **One shared library.** `libmain.so` holds the SDK, the game and SDL3, linked statically. SDL's Java activity
  loads it, subclassed in `android/java/com/ari/peggle2/Peggle2Activity.java`. That subclass adds the pause and
  sound buttons and handles the back gesture.
- **Renderer changes.**
  - The draw layout packs its 8 descriptor sets into **3**: the texture table (the four views share one set), the
    samplers, and the three constant buffers as three dynamic uniform buffer bindings of one set. The shaders' set
    and binding numbers are rewritten as they load (`PackDrawSets` in
    `app/src/renut_engine/gpu/render/pipeline.cpp`). 3 matters: the Vulkan minimum for `maxBoundDescriptorSets` is 4,
    and Arm Mali reports exactly 4 (a Fire Max 11's Mali-G57 MC2 does), so anything above 4 fails to create the draw
    pipeline layout on Mali and the game comes up on a black screen. Adreno reports 7, which is why the first build's
    5 was enough to hide the problem. The layout is chosen at compile time (`PackedDrawSets()`): the unpacked 8-set
    one stays on the Switch (deko3d has no such limit) and on the desktop builds.
  - On a Vulkan 1.1 device, buffer device addresses come from `vkGetBufferDeviceAddressKHR` (plume loaded only the
    core 1.2 name, which a 1.1 driver need not have), and the memory allocator is told the device's Vulkan version,
    not the instance's (`app/thirdparty/plume/plume_vulkan.cpp`).
  - The renderer's shaders are SPIR-V 1.0 (the translator's DXC call names no target environment), which every
    Vulkan 1.1 driver accepts; a module newer than 1.3 is reported in the log.
  - The swapchain is RGBA8, because Android has no BGRA8 surfaces.
  - The swapchain is rebuilt whenever Android gives the app a new surface, which happens on coming back from the
    background.
  - The picture is shown whole at 16:9 with bars at the sides. Touch coordinates go through the same fit.
- **Offline sockets.** `net_offline_sockets` is on by default. The app has no network permission, and the game's
  network start-up must not fail: on the Switch that failure freed its network object and hung the boot later
  (building.md, "The console boot crash").
- **Diagnostics.** If a file named `stack_dumps` exists in the app folder, every thread's native stack is logged to
  logcat (tag `peggle2-stacks`) 20 and 40 seconds after the start. Release phones don't let adb dump an app's
  threads.

## When it does not start

The app folder is invisible to file managers and to USB transfers (Android 11 and later), so the port hands its logs
to the player instead of expecting them to find them:

- A launch writes `logs/session.lock`, and the native side removes it once about 10 seconds of frames have been
  shown, or on the game's own Exit. A launch that finds the marker knows the last start-up never got that far — a
  GPU fault kills the process without running any of our code — and copies the
  text logs and a device report into **`Downloads/Peggle2-logs-<model>-<date>.zip`** before the new run truncates
  the log (`android/java/com/ari/peggle2/Diagnostics.java`).
- If the renderer could not start, `logs/startup_error.txt` is written with the reason (for example the Vulkan
  feature the GPU lacks), and the next launch shows it in a dialog with the issue tracker's address
  (`app_integration.cpp`, `NoteStartupFailure`).
- The decisive lines are at the top of `logs/plume_stderr.log`: the `[android]` block prints the GPU's name, driver,
  API version, descriptor-indexing features and descriptor limits, including `maxBoundDescriptorSets`, and whether it
  has buffer device addresses and `shaderInt64`. Everything the
  renderer refuses to do is printed next to it.

To read them on the machine instead: `adb pull /sdcard/Android/data/com.ari.peggle2/files/logs/`.

**Updating a build you made yourself:** `tools/android/update_android.ps1` rebuilds with the key your first build
made, so installing over the old app keeps the 2.7 GB of game data and the save. Do not uninstall first.

## The launcher (optional)

`launcher/` is a separate small app that shows the Peggle games installed on the phone as a row of covers. It finds
peg2arm, Peggle Blast and [Peggle Enhanced](https://github.com/aidrum/peggle-enhanced), whose Android port covers
Peggle, Extreme, Nights, Dual Shot and WoW. It comes without art; you add your own. See
[launcher/README.md](../launcher/README.md).
