// Bursztyn OS - Ozone platform: naglowek.
//
// Platforma Ozone to warstwa miedzy ui::PlatformWindow a systemem okien.
// Ten backend mapuje okno Chromium 1:1 na warstwe BWS (jeden PID = jedna
// warstwa) i prezentuje gotowe ramki przez BWS98 (skalowany blit).

#ifndef UI_OZONE_PLATFORM_BURSZTYN_OZONE_PLATFORM_BURSZTYN_H_
#define UI_OZONE_PLATFORM_BURSZTYN_OZONE_PLATFORM_BURSZTYN_H_

#include "ui/ozone/public/ozone_platform.h"

namespace ui {

class OzonePlatformBursztyn : public OzonePlatform {
 public:
  OzonePlatformBursztyn();
  ~OzonePlatformBursztyn() override;

  // OzonePlatform:
  PlatformWindow* CreatePlatformWindow(
      PlatformWindowDelegate* delegate,
      const PlatformWindowInitProperties& properties,
      const gfx::Rect& bounds) override;
  PlatformEventSource* GetPlatformEventSource() override;
  SurfaceFactoryOzone* GetSurfaceFactoryOzone() override;
  std::unique_ptr<SystemInputInjector> CreateSystemInputInjector() override;
  bool IsWindowCompositingSupported() const override;
  std::string GetDefaultFontFamily() const override;

 private:
  std::unique_ptr<SurfaceFactoryOzone> surface_factory_;
  std::unique_ptr<PlatformEventSource> event_source_;

  OzonePlatformBursztyn(const OzonePlatformBursztyn&) = delete;
  OzonePlatformBursztyn& operator=(const OzonePlatformBursztyn&) = delete;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_BURSZTYN_OZONE_PLATFORM_BURSZTYN_H_
