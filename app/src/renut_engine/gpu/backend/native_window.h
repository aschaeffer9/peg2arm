/**
 * @file    gpu/backend/native_window.h
 * @brief   The game window, found from the platform rather than the SDK.
 */
#pragma once

#include <cstdint>

#include <plume_render_interface.h>

namespace renut::gpu {

// Windows: the HWND of this process's game window (the SDK's SDL window).
// Linux: the Xlib display + window of the SDL window (plume's Linux Vulkan
// surface is Xlib, so SDL is pinned to its X11 driver).
bool GetNativeRenderWindow(plume::RenderWindow &out);

#if defined(__ANDROID__)
// peggle2-android: SDL's current ANativeWindow, or null while the app is in the background (Android destroys the
// SurfaceView's surface when the app is left and makes a new one when it comes back). Quiet: no log.
plume::RenderWindow CurrentAndroidWindow();
#endif

// The game window's client area, in pixels.
bool GetRenderWindowPixelSize(uint32_t &width, uint32_t &height);

} // namespace renut::gpu
