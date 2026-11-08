// Generic opaque Multiboot asset import. The kernel never opens TTF tables.
#include "limits.h"
void bsf_import_fonts() {
  if (!g_multiboot_info_ptr)
    return;
  if (!zapewnij_katalog_startowy("/czcionki") ||
      !zapewnij_katalog_startowy("/czcionki/ttf"))
    return;
  uint64_t mbi = g_multiboot_info_ptr;
  uint32_t total = *reinterpret_cast<const uint32_t *>(mbi);
  if (total < 16 || total > 1024 * 1024)
    return;
  unsigned count = 0;
  uint64_t bytes = 0;
  for (uint64_t off = 8; off + 8 <= total && count < BSF::MaxFiles;) {
    const auto *tag = reinterpret_cast<const WpisTaguMB2 *>(mbi + off);
    if (tag->rozmiar < 8 || tag->rozmiar > total - off)
      break;
    if (tag->typ == 3 && tag->rozmiar > 20) {
      uint32_t start = *reinterpret_cast<const uint32_t *>(mbi + off + 8),
               end = *reinterpret_cast<const uint32_t *>(mbi + off + 12);
      const char *label = reinterpret_cast<const char *>(mbi + off + 16);
      unsigned cap = tag->rozmiar - 16;
      if (label[0]=='b' && label[1]=='s' && label[2]=='f' && label[3]=='/' &&
          end>start && end-start>BSF::MaxFile)
        wypisz_log("[BSF] Boot asset exceeds bounded font-file payload; skipped.");
      if (label[0] == 'b' && label[1] == 's' && label[2] == 'f' &&
          label[3] == '/' && end > start && end - start <= BSF::MaxFile) {
        char path[128] = "/czcionki/ttf/";
        unsigned n = 0;
        bool safe = true;
        while (n + 4 < cap && label[n + 4] && n < 100) {
          unsigned c = uint8_t(label[n + 4]);
          if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            safe = false;
          path[14 + n] = char(c);
          ++n;
        }
        if (n + 4 >= cap || label[n + 4] || n < 5)
          safe = false;
        path[14 + n] = 0;
        if (safe && bytes + end - start <= BSF::MaxLoadedBytes) {
          if (zapisz_plik_od_nowa(
              path, reinterpret_cast<const char *>(uintptr_t(start)),
              end - start)) { bytes += end - start; ++count; }
          else wypisz_log("[BSF] Boot asset write failed; fallback retained.");
        } else if (safe)
          wypisz_log("[BSF] Boot asset budget reached; skipped.");
      }
    }
    if (!tag->typ)
      break;
    off = (off + tag->rozmiar + 7) & ~uint64_t(7);
  }
}
