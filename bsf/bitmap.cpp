#include "../bws_czcionka.h"
#include "../czcionki/extronic16Bunicode.h"
#include "runtime.h"
namespace BSF {
static_assert(sizeof(nowa_czcionka_16x16) / sizeof(nowa_czcionka_16x16[0]) ==
                  BWS_CZCIONKA_GLIFY,
              "Bitmap fallback ABI");
int bitmap_width(uint32_t cp) {
  if (cp >= BWS_CZCIONKA_GLIFY)
    cp = '?';
  int w = nowa_czcionka_szerokosci[cp];
  return w < 1 ? 1 : w > 16 ? 16 : w;
}
const uint16_t *bitmap(uint32_t cp) {
  return nowa_czcionka_16x16[cp < BWS_CZCIONKA_GLIFY ? cp : '?'];
}
} // namespace BSF
