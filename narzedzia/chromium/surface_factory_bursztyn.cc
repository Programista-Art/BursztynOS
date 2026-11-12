// Bursztyn OS - Ozone SurfaceFactory: implementacja.
//
// Etap 1: rasteryzacja wylacznie software'owa (Skia CPU). Nie ma GPU API
// (brak GL/Vulkan), wiec GetAllowedGLImplementations zwraca pusta liste -
// Chromium wybierze software raster.

#include "ui/ozone/platform/bursztyn/surface_factory_bursztyn.h"

#include "base/logging.h"
#include "ui/ozone/platform/bursztyn/surface_ozone_bursztyn.h"

namespace ui {

SurfaceFactoryBursztyn::SurfaceFactoryBursztyn() = default;
SurfaceFactoryBursztyn::~SurfaceFactoryBursztyn() = default;

std::vector<gl::GLImplementationParts>
SurfaceFactoryBursztyn::GetAllowedGLImplementations() {
  // Brak GL: Bursztyn nie ma sterownika OpenGL ani virgl (swiadomie 2D only).
  return {};
}

GLOzone* SurfaceFactoryBursztyn::GetGLOzone(
    const gl::GLImplementationParts& implementation) {
  return nullptr;
}

std::unique_ptr<SurfaceOzoneCanvas> SurfaceFactoryBursztyn::CreateCanvasForWidget(
    gfx::AcceleratedWidget widget) {
  return std::make_unique<SurfaceOzoneBursztyn>(widget);
}

scoped_refptr<gfx::NativePixmap> SurfaceFactoryBursztyn::CreateNativePixmap(
    gfx::AcceleratedWidget widget,
    gpu::VulkanDeviceQueue* device_queue,
    gfx::Size size,
    gfx::BufferFormat format,
    gfx::BufferUsage usage,
    Microsoft::WRL::ComPtr<ID3D11Texture2D> d3d11_texture) {
  // NativePixmap jest potrzebny tylko dla sciezki GPU/overlay.
  return nullptr;
}

}  // namespace ui
