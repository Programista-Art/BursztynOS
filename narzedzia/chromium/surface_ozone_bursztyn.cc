// Bursztyn OS - Ozone SurfaceOzoneCanvas: implementacja.
//
// Przeplyw klatki:
//
//   Skia rysuje -> bitmap_ (ARGB premultiplied)
//     -> PresentCanvas(damage) -> BWS98 (skalowany blit, filtr bilinear)
//       -> warstwa BWS -> kompozytor jadra -> virtio-gpu transfer+flush
//
// To jest wlasciwe miejsce "akceleracji 2D": arytmetyka skalowania dzieje sie
// raz, w jadrze, wprost do powierzchni warstwy, wiec Ring 3 nie placi za
// skalowanie i nie ma drugiego bufora docelowego.

#include "ui/ozone/platform/bursztyn/surface_ozone_bursztyn.h"

#include <algorithm>

#include "base/logging.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/bursztyn/posix_bws.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/gfx/vsync_provider.h"

namespace ui {

namespace {

// Filtr dla BWS98. Bilinear jest domyslnym wyborem dla tresci HTML: tekst
// i krzywe wymagaja interpolacji, a nearest daje widoczne schodki przy
// kazdym zoomie. Skalowanie >=2x i tak degradouje sie do nearest w jadrze.
constexpr uint32_t kFiltrBilinear = 1;  // SKALOWANIE_BILINEAR
constexpr uint32_t kFiltrNearest = 0;   // SKALOWANIE_NEAREST
constexpr uint32_t kTrybRozciagnij = 0;

}  // namespace

SurfaceOzoneBursztyn::SurfaceOzoneBursztyn(gfx::AcceleratedWidget widget)
    : widget_(widget) {}

SurfaceOzoneBursztyn::~SurfaceOzoneBursztyn() = default;

void SurfaceOzoneBursztyn::ResizeCanvas(const gfx::Size& viewport_size,
                                        float scale) {
  if (viewport_size.IsEmpty()) return;

  scale_ = scale > 0.0f ? scale : 1.0f;
  const gfx::Size nowy(
      std::max(1, static_cast<int>(viewport_size.width() * scale_)),
      std::max(1, static_cast<int>(viewport_size.height() * scale_)));
  if (bitmap_ && pixel_size_ == nowy) return;

  bitmap_ = std::make_unique<SkBitmap>();
  bitmap_->allocN32Pixels(nowy.width(), nowy.height(), /*isOpaque=*/false);
  bitmap_->eraseARGB(0, 0, 0, 0);
  pixel_size_ = nowy;

  // Deklarujemy kompozytorowi skale powierzchni, zeby skalowal bufor
  // w okno bez udzialu Ring 3 przy kazdej klatce.
  bws_ustaw_skale_warstwy(nowy.width(), nowy.height(), kTrybRozciagnij);
}

skia::ScopedPlatformPaint SurfaceOzoneBursztyn::GetCanvas() {
  if (!bitmap_ || bitmap_->isNull()) {
    // Chromium moze zapytac o canvas przed ResizeCanvas; zwracamy pusty
    // paint, ktory Skia obsluzy jako brak powierzchni.
    return skia::ScopedPlatformPaint(nullptr, false);
  }
  SkCanvas* canvas = skia::CreatePlatformCanvas(*bitmap_);
  return skia::ScopedPlatformPaint(canvas, true);
}

void SurfaceOzoneBursztyn::PresentCanvas(const gfx::Rect& damage) {
  if (!bitmap_ || bitmap_->isNull() || pixel_size_.IsEmpty()) return;

  // Cala klatka idzie jednym wywolaniem. BWS98 sam przytnie do warstwy,
  // wiec damage jest tylko optymalizacja - przy braku informacji o
  // dokladnym prostokacie wysylamy calosc (bezpieczniej niz zgadywac).
  const uint32_t* piksele =
      reinterpret_cast<const uint32_t*>(bitmap_->getPixels());
  if (!piksele) return;

  const uint32_t filtr =
      (scale_ > 1.0f) ? kFiltrBilinear : kFiltrNearest;

  bws_blit_skalowany(/*dst_x=*/0, /*dst_y=*/0,
                     pixel_size_.width(), pixel_size_.height(),
                     piksele,
                     static_cast<uint32_t>(pixel_size_.width()),
                     static_cast<uint32_t>(pixel_size_.height()),
                     filtr,
                     /*flagi=*/0,  // domyslnie: puste piksele pomijane
                     kTrybRozciagnij);
  bws_odswiez();
}

std::unique_ptr<gfx::VSyncProvider>
SurfaceOzoneBursztyn::CreateVSyncProvider() {
  // Kompozytor BWS prezentuje z wlasnym pacingiem (60..240 Hz), wiec
  // Chromium nie musi dostawac osobnego zrodla VSync.
  return nullptr;
}

}  // namespace ui
