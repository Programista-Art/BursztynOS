// Bursztyn OS - Ozone PlatformWindow: naglowek.

#ifndef UI_OZONE_PLATFORM_BURSZTYN_PLATFORM_WINDOW_BURSZTYN_H_
#define UI_OZONE_PLATFORM_BURSZTYN_PLATFORM_WINDOW_BURSZTYN_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/platform_window/platform_window.h"
#include "ui/platform_window/platform_window_init_properties.h"

namespace ui {

class PlatformWindowBursztyn : public PlatformWindow {
 public:
  PlatformWindowBursztyn(PlatformWindowDelegate* delegate,
                         const PlatformWindowInitProperties& properties);
  ~PlatformWindowBursztyn() override;

  // Tworzy warstwe BWS i ustawia geometrie. Zwraca false, gdy BWS odmowi
  // (brak prawa GUI, brak slotu warstwy, rozmiar spoza zakresu).
  bool Initialize(const gfx::Rect& bounds);

  // Czy warstwa zostala utworzona i nadal istnieje.
  bool IsAlive() const { return layer_id_ >= 0; }

  // IdentYfikator warstwy BWS (>=0) albo -1.
  int layer_id() const { return layer_id_; }

  // PlatformWindow:
  void Show(bool inactive) override;
  void Hide() override;
  void Close() override;
  void PrepareForShutdown() override;
  void SetBoundsInPixels(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInPixels() const override;
  void SetTitle(const std::u16string& title) override;
  void SetCapture() override;
  void ReleaseCapture() override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  void SetFullscreen(bool fullscreen, int64_t target_display_id) override;
  void SetAspectRatio(float aspect_ratio) override;
  void SetShape(std::unique_ptr<Shape> shape) override;
  void SetOpacity(float opacity) override;

 private:
  PlatformWindowDelegate* const delegate_;
  const PlatformWindowInitProperties properties_;
  gfx::Rect bounds_;
  int layer_id_ = -1;
  bool visible_ = false;
  bool capture_ = false;

  base::WeakPtrFactory<PlatformWindowBursztyn> weak_factory_{this};
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_BURSZTYN_PLATFORM_WINDOW_BURSZTYN_H_
