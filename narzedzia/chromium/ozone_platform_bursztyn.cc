// Bursztyn OS - Ozone platform: implementacja.
//
// Decyzje projektowe:
//
//   - kompozycja NIE jest wspierana przez Ozone: Chromium rysuje do jednej
//     powierzchni, a kompozytor BWS sklada warstwy systemu. To upraszcza
//     backend i jest zgodne z modelem Bursztyn (PZB-2 kompozytor).
//   - font domyslny pochodzi z BSF; nazwa jest tylko podpowiedzia dla
//     Skia/FreeType, ktore i tak pytaja system o plik.
//   - input jest dostarczany przez platform_event_source_bursztyn.

#include "ui/ozone/platform/bursztyn/ozone_platform_bursztyn.h"

#include <memory>

#include "base/logging.h"
#include "ui/ozone/platform/bursztyn/platform_event_source_bursztyn.h"
#include "ui/ozone/platform/bursztyn/platform_window_bursztyn.h"
#include "ui/ozone/platform/bursztyn/surface_factory_bursztyn.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/ozone/public/system_input_injector.h"

namespace ui {

OzonePlatformBursztyn::OzonePlatformBursztyn() = default;
OzonePlatformBursztyn::~OzonePlatformBursztyn() = default;

PlatformWindow* OzonePlatformBursztyn::CreatePlatformWindow(
    PlatformWindowDelegate* delegate,
    const PlatformWindowInitProperties& properties,
    const gfx::Rect& bounds) {
  auto window = std::make_unique<PlatformWindowBursztyn>(delegate, properties);
  if (!window->Initialize(bounds))
    return nullptr;
  return window.release();
}

PlatformEventSource* OzonePlatformBursztyn::GetPlatformEventSource() {
  if (!event_source_)
    event_source_ = std::make_unique<PlatformEventSourceBursztyn>();
  return event_source_.get();
}

SurfaceFactoryOzone* OzonePlatformBursztyn::GetSurfaceFactoryOzone() {
  if (!surface_factory_)
    surface_factory_ = std::make_unique<SurfaceFactoryBursztyn>();
  return surface_factory_.get();
}

std::unique_ptr<SystemInputInjector>
OzonePlatformBursztyn::CreateSystemInputInjector() {
  // Wstrzykiwanie inputu (testy, automatyzacja) nie jest potrzebne w etapie 1.
  return nullptr;
}

bool OzonePlatformBursztyn::IsWindowCompositingSupported() const {
  // Kompozycje robi kompozytor BWS w jadrze, nie Chromium.
  return false;
}

std::string OzonePlatformBursztyn::GetDefaultFontFamily() const {
  return "Bursztyn Sans";
}

}  // namespace ui
