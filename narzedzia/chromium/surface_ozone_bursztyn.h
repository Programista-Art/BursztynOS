// Bursztyn OS - Ozone SurfaceOzoneCanvas: naglowek.
//
// SurfaceOzoneCanvas to jedyna sciezka prezentacji software'owej w Ozone.
// Chromium rysuje do GetCanvas(), a my wysylamy gotowy bufor do warstwy BWS
// przez BWS98.

#ifndef UI_OZONE_PLATFORM_BURSZTYN_SURFACE_OZONE_BURSZTYN_H_
#define UI_OZONE_PLATFORM_BURSZTYN_SURFACE_OZONE_BURSZTYN_H_

#include <memory>
#include <vector>

#include "base/memory/ref_counted.h"
#include "ui/gfx/geometry/size.h"
#include "ui/ozone/public/surface_ozone_canvas.h"

namespace gfx {
class VSyncProvider;
}

namespace ui {

class SurfaceOzoneBursztyn : public SurfaceOzoneCanvas {
 public:
  explicit SurfaceOzoneBursztyn(gfx::AcceleratedWidget widget);
  ~SurfaceOzoneBursztyn() override;

  // SurfaceOzoneCanvas:
  void ResizeCanvas(const gfx::Size& viewport_size, float scale) override;
  skia::ScopedPlatformPaint GetCanvas() override;
  void PresentCanvas(const gfx::Rect& damage) override;
  std::unique_ptr<gfx::VSyncProvider> CreateVSyncProvider() override;

  // Rozmiar bufora w pikselach fizycznych.
  gfx::Size pixel_size() const { return pixel_size_; }

 private:
  gfx::AcceleratedWidget widget_;
  gfx::Size pixel_size_;
  float scale_ = 1.0f;

  // Bufor ARGB premultiplied, ktory Skia traktuje jako canvas.
  std::unique_ptr<SkBitmap> bitmap_;

  SurfaceOzoneBursztyn(const SurfaceOzoneBursztyn&) = delete;
  SurfaceOzoneBursztyn& operator=(const SurfaceOzoneBursztyn&) = delete;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_BURSZTYN_SURFACE_OZONE_BURSZTYN_H_
