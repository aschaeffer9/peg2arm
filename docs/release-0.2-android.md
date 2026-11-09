# 0.2 (Android) — it starts on Mali now

A point release for one bug, reported on an Amazon Fire Max 11: the game installed, launched, and sat on a black
screen. It was only invisible because the phone it was tested on was a Snapdragon.

## What was wrong

**The draw pipeline layout used 5 descriptor sets. The Vulkan minimum, and what every Mali reports, is 4.**

Adreno 830 reports `maxBoundDescriptorSets = 7`, so the first build packed the renderer's 8 sets down to 5 and called
it done. A Mali-G57 MC2 reports exactly 4 (`[android] limits: ... boundSets 4`), so `vkCreatePipelineLayout` was
invalid usage, the draw layout never existed, and nothing was drawn — no error on screen, just black.

Fixed by packing to **3** sets: the texture table, the samplers, and the three constant buffers as three dynamic
uniform buffer bindings of one set. The shaders' set and binding decorations are rewritten as they load. Everything
else — the Switch's deko3d path, the desktop builds — keeps the numbering it was built with; the Android build turns
the packed layout on at compile time.

**Also fixed: buffer device addresses on Vulkan 1.1.**

The renderer's shaders read their vertex data (and write memexport) through 64-bit buffer addresses, and the
renderer takes those addresses at start-up, before it builds the draw layout. On a Vulkan 1.2+ GPU that is core. On a
1.1 GPU, such as the Fire Max 11's, it is the `VK_KHR_buffer_device_address` extension, whose function is
`vkGetBufferDeviceAddressKHR`, and plume only ever loaded the core name, which a 1.1 driver need not provide. That is
a crash with nothing in the log, and on the Fire it would have looked exactly like the bug above. The port now uses
the extension's function on a 1.1 device, and tells the memory allocator the device's Vulkan version instead of the
instance's.

**New: the renderer checks what it needs before using it.** No buffer device addresses, no `shaderInt64`, no
descriptor indexing, or a GPU that cannot hold the 8192-entry texture table: the start fails with that reason, which
the next launch shows (below), instead of a black screen. Older Adreno (6xx) and Mali GPUs are where this matters.

## What else changed

- **A failed start now says so.** The reason is written to `logs/startup_error.txt` and shown, on the next launch, in
  a dialog with the issue tracker's address.
- **The logs come to you.** The app folder (`Android/data/...`) is invisible to file managers and to USB since
  Android 11, so a player had no way to send a log. A launch that finds the previous start-up never got going copies
  the text logs and a device report to **`Downloads/Peggle2-logs-<model>-<date>.zip`** — no adb, no root, attach it
  to an issue. Closing the app from Recents later does not count: only a start that never showed ~10 seconds of
  frames does.
- **Android 11 / Fire OS 8 installs out of the box.** The build's NDK platform and the APK's `minSdkVersion` are now
  one number (`-MinSdk`, default 30 = Android 11) instead of 31 in the toolchain and something else in the manifest.
- **`tools/android/update_android.ps1`**: rebuilds with the key your first build made, so installing over an existing
  copy keeps the 2.7 GB of game data and your save.

## Updating a build you made yourself

```
powershell -ExecutionPolicy Bypass -File tools\android\update_android.ps1 -Ndk C:\android\android-ndk-r28c
```

Then open `Downloads/peggle2.apk` on the device and tap **Install**. Do not uninstall the old app first: installing
over it is what keeps your data, and it only works with the same signing key (the one in `out/` from your first
build).

## If it still does not start

Launch it once more. It will show what failed and leave `Downloads/Peggle2-logs-*.zip` behind. Attach that to an
issue: https://github.com/aschaeffer9/peg2arm/issues

The file that decides almost everything is the top of `logs/plume_stderr.log`:

```
[android] <GPU>, API <x.y.z>, driver <...>
[android] VK_EXT_descriptor_indexing listed ...; features: ...
[android] limits: perStage sampledImages ... samplers ... resources ... boundSets 4; ...
[android] VK_KHR_buffer_device_address listed ...; bufferDeviceAddress ... shaderInt64 ... scalarBlockLayout ...
```

## Known limits

- **I cannot test every device.** This release fixes what a Vulkan 1.1 Mali device fails at as far as reading the
  code can show; the next problem will be something else, and the reports are what will find it.
- **Older Qualcomm drivers are not supported.** The renderer needs bindless textures (descriptor indexing) and 64-bit
  integers in shaders. A Galaxy S20 (Adreno 650, Vulkan 1.1.128 driver) has neither, and 0.2 says so on screen
  instead of starting. Loading Mesa's open-source Turnip driver for such phones is being looked at for a later
  release.
- **Amazon Fire TV is not supported.** Those devices run 32-bit userland (the APK is arm64-v8a) and have no
  touchscreen; Fire *tablets* (Fire OS 8, Android 11) are what this release targets.
- **`arm64-v8a`, Vulkan 1.1, Android 11** are the floor. A device below any of them will not run this.
