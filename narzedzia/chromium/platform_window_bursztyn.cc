// Bursztyn OS - Ozone PlatformWindow: implementacja.
//
// Mapowanie: okno Chromium == jedna warstwa BWS. Nie ma hierarchii okien
// (popupy/child windows sa oknami warstwy - kompozytor BWS obsluguje
// system overlay i popup aplikacji osobnymi BWS, ale nie tworzy nowych
// warstw). To upraszcza model i jest zgodne z limitem 16 warstw.

#include "ui/ozone/platform/bursztyn/platform_window_bursztyn.h"

#include <algorithm>

#include "base/logging.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/platform_window/platform_window_delegate.h"
#include "third_party/bursztyn/posix_bws.h"

namespace ui {

namespace {

// Minimalny i maksymalny rozmiar okna, zgodny z limitami BWS.
constexpr int kMinDimension = 1;
constexpr int kMaxDimension = 8192;

// Wyrownanie do skali UI: kompozytor skaluje metryki, a nadmiar pikseli
// powoduje rozmycie. Zaokraglamy do 4 px, zeby uniknac ciaglego
// przeliczania przy kazdym ruchu okna.
int Zaokraglij(int v) { return std::max(kMinDimension, (v + 2) & ~3); }

}  // namespace

PlatformWindowBursztyn::PlatformWindowBursztyn(
    PlatformWindowDelegate* delegate,
    const PlatformWindowInitProperties& properties)
    : delegate_(delegate), properties_(properties) {}

PlatformWindowBursztyn::~PlatformWindowBursztyn() {
  // BWS zwalnia warstwe przy zakonczeniu procesu; nie ma osobnego BWS
  // "zniszcz warstwe", wiec nic tu nie wolamy.
}

bool PlatformWindowBursztyn::Initialize(const gfx::Rect& bounds) {
  bounds_ = gfx::Rect(bounds.x(), bounds.y(),
                      std::clamp(bounds.width(), kMinDimension, kMaxDimension),
                      std::clamp(bounds.height(), kMinDimension, kMaxDimension));
  bounds_.set_width(Zaokraglij(bounds_.width()));
  bounds_.set_height(Zaokraglij(bounds_.height()));

  // Z-order 1: nad pulpitem (0), pod system overlayem. Chromium nie jest
  // menedzerem okien, wiec nie dostaje z_order 0.
  layer_id_ = bws_utworz_warstwe(bounds_.x(), bounds_.y(), bounds_.width(),
                                 bounds_.height(), /*z_order=*/1);
  if (layer_id_ < 0) {
    LOG(ERROR) << "BWS: nie udalo sie utworzyc warstwy dla okna Chromium";
    return false;
  }
  return true;
}

void PlatformWindowBursztyn::Show(bool inactive) {
  if (layer_id_ < 0) return;
  visible_ = true;
  bws_ustaw_geometrie_okna(bounds_.x(), bounds_.y(), bounds_.width(),
                           bounds_.height(), /*stan=*/0);
  bws_odswiez();
}

void PlatformWindowBursztyn::Hide() {
  if (layer_id_ < 0) return;
  visible_ = false;
  bws_minimalizuj_okno();
}

void PlatformWindowBursztyn::Close() {
  // Delegate decyduje o cyklu zycia; my tylko zwalniamy capture.
  ReleaseCapture();
  visible_ = false;
}

void PlatformWindowBursztyn::PrepareForShutdown() { ReleaseCapture(); }

void PlatformWindowBursztyn::SetBoundsInPixels(const gfx::Rect& bounds) {
  if (bounds == bounds_) return;

  bounds_ = gfx::Rect(bounds.x(), bounds.y(),
                      std::clamp(bounds.width(), kMinDimension, kMaxDimension),
                      std::clamp(bounds.height(), kMinDimension, kMaxDimension));
  bounds_.set_width(Zaokraglij(bounds_.width()));
  bounds_.set_height(Zaokraglij(bounds_.height()));

  if (layer_id_ < 0) return;
  bws_ustaw_geometrie_okna(bounds_.x(), bounds_.y(), bounds_.width(),
                           bounds_.height(), /*stan=*/0);
  // Delegate musi przeliczyc powierzchnie - inaczej nastepna klatka
  // zostanie narysowana w starym rozmiarze.
  delegate_->OnBoundsChanged(bounds_);
}

gfx::Rect PlatformWindowBursztyn::GetBoundsInPixels() const { return bounds_; }

void PlatformWindowBursztyn::SetTitle(const std::u16string& title) {
  // BWS przyjmuje UTF-8; konwersja jest w surface/present path, bo title
  // nie jest krytyczny dla renderowania.
  (void)title;
}

void PlatformWindowBursztyn::SetCapture() {
  if (capture_) return;
  capture_ = true;
  bws_ustaw_capture_myszy(true);
}

void PlatformWindowBursztyn::ReleaseCapture() {
  if (!capture_) return;
  capture_ = false;
  bws_ustaw_capture_myszy(false);
}

void PlatformWindowBursztyn::Maximize() {
  if (layer_id_ < 0) return;
  bws_ustaw_geometrie_okna(bounds_.x(), bounds_.y(), bounds_.width(),
                           bounds_.height(), /*stan=*/2);
}

void PlatformWindowBursztyn::Minimize() {
  if (layer_id_ < 0) return;
  bws_minimalizuj_okno();
}

void PlatformWindowBursztyn::Restore() {
  if (layer_id_ < 0) return;
  bws_ustaw_geometrie_okna(bounds_.x(), bounds_.y(), bounds_.width(),
                           bounds_.height(), /*stan=*/0);
}

void PlatformWindowBursztyn::SetFullscreen(bool fullscreen,
                                           int64_t target_display_id) {
  (void)target_display_id;
  if (layer_id_ < 0) return;
  // Pelny ekran = tryb zmaksymalizowany; kompozytor BWS rysuje dekoracje
  // tylko dla stanu normalnego, wiec to wystarcza wizualnie.
  bws_ustaw_geometrie_okna(bounds_.x(), bounds_.y(), bounds_.width(),
                           bounds_.height(), fullscreen ? 2 : 0);
}

void PlatformWindowBursztyn::SetAspectRatio(float aspect_ratio) {
  (void)aspect_ratio;
}

void PlatformWindowBursztyn::SetShape(std::unique_ptr<Shape> shape) {
  (void)shape;
}

void PlatformWindowBursztyn::SetOpacity(float opacity) { (void)opacity; }

}  // namespace ui
