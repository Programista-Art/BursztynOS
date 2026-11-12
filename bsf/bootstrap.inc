// Generic opaque Multiboot asset import. The kernel never opens font tables;
// the destination directory is chosen from the file extension only
// (bsf/asset_path.h), and the bytes are copied verbatim.
#include "limits.h"
#include "asset_path.h"
void bsf_import_fonts() {
  if (!g_multiboot_info_ptr)
    return;
  if (!zapewnij_katalog_startowy("/czcionki"))
    return;
  for (unsigned d = 0; d < BSF::FontDirectoryCount; ++d)
    if (!zapewnij_katalog_startowy(BSF::FontDirectories[d].path))
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
        // .otf/.ttc/.otc go to their own directory; everything else (TTF
        // fonts and license notices) keeps the BSF 2.1 location.
        int directory = safe ? BSF::font_directory_for(path + 14, n) : -1;
        if (directory > 0)
          for (unsigned k = 0; k < 13; ++k)
            path[k] = BSF::FontDirectories[directory].path[k];
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
