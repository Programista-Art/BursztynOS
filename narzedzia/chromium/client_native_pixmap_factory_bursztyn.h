// Bursztyn OS - ClientNativePixmapFactory: naglowek.
//
// Pixmap native jest potrzebny tylko dla sciezki GPU/overlay. W etapie 1
// (Skia CPU) ta fabryka zwraca nullptr, ale musi istniec, bo Ozone wymaga
// zarejestrowanej fabryki dla platformy bez GPU.

#ifndef UI_OZONE_PLATFORM_BURSZTYN_CLIENT_NATIVE_PIXMAP_FACTORY_BURSZTYN_H_
#define UI_OZONE_PLATFORM_BURSZTYN_CLIENT_NATIVE_PIXMAP_FACTORY_BURSZTYN_H_

#include <memory>

#include "ui/ozone/public/client_native_pixmap_factory.h"

namespace ui {

std::unique_ptr<ClientNativePixmapFactory> CreateClientNativePixmapFactoryBursztyn();

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_BURSZTYN_CLIENT_NATIVE_PIXMAP_FACTORY_BURSZTYN_H_
