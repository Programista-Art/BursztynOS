// Bursztyn OS - ClientNativePixmapFactory: implementacja.
//
// Zwraca nullptr: nie ma sciezki GPU, wiec zadna powierzchnia nie jest
// importowana jako native pixmap. Chromium obsluguje nullptr poprawnie
// (fallback do software). Implementacja istniejepo, bo linkowanie Ozone
// wymaga symbolu.

#include "ui/ozone/platform/bursztyn/client_native_pixmap_factory_bursztyn.h"

namespace ui {

std::unique_ptr<ClientNativePixmapFactory>
CreateClientNativePixmapFactoryBursztyn() {
  return nullptr;
}

}  // namespace ui
