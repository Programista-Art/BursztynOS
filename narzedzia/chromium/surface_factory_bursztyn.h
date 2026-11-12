// Bursztyn OS - Ozone SurfaceFactory: naglowek.
//
// SurfaceFactory tworzy powierzchnie, do ktorych Chromium rysuje. W naszym
// backendzie powierzchnia to bufor w pamieci procesu; prezentacja idzie
// przez BWS98 (skalowany blit) do warstwy BWS.

#ifndef UI_OZONE_PLATFORM_BURSZTYN_SURFACE_FACTORY_BURSZTYN_H_
#define UI_OZONE_PLATFORM_BURSZTYN_SURFACE_FACTORY_BURSZTYN_H_

#include <memory>
#include <vector>

#include "ui/ozone/public/surface_factory_ozone.h"

namespace ui {

class SurfaceFactoryBursztyn : public SurfaceFactoryOzone {
 public:
  SurfaceFactoryBursztyn();
  ~SurfaceFactoryBursztyn() override;

  // SurfaceFactoryOzone:
  std::vector<gl::GLImplementationParts> GetAllowedGLImplementations() override;
  GLOzone* GetGLOzone(const gl::GLImplementationParts& implementation) override;
  std::unique_ptr<SurfaceOzoneCanvas> CreateCanvasForWidget(
      gfx::AcceleratedWidget widget) override;
  scoped_refptr<gfx::NativePixmap> CreateNativePixmap(
      gfx::AcceleratedWidget widget,
      gpu::VulkanDeviceQueue* device_queue,
      gfx::Size size,
      gfx::BufferFormat format,
      gfx::BufferUsage usage,
      Microsoft::WRL::ComPtr<ID3D11Texture2D> d3d11_texture) override;

 private:
  SurfaceFactoryBursztyn(const SurfaceFactoryBursztyn&) = delete;
  SurfaceFactoryBursztyn& operator=(const SurfaceFactoryBursztyn&) = delete;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_BURSZTYN_SURFACE_FACTORY_BURSZTYN_H_
