#pragma once
/**
 * Android: the ANativeWindow SDL's Java activity hands the native side (SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER),
 * presented with VK_KHR_android_surface. vulkan_presenter.cpp already handles kTypeIndex_AndroidNativeWindow; this is
 * the surface class it expects (Xenia's, which the SDK did not carry).
 */

#include <rex/ui/surface.h>

struct ANativeWindow;

namespace rex {
namespace ui {

class AndroidNativeWindowSurface final : public Surface {
 public:
  explicit AndroidNativeWindowSurface(ANativeWindow* window) : window_(window) {}
  TypeIndex GetType() const override { return kTypeIndex_AndroidNativeWindow; }
  ANativeWindow* window() const { return window_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override;

 private:
  ANativeWindow* window_;
};

}  // namespace ui
}  // namespace rex
