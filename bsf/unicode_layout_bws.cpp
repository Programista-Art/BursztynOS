// Ring 3 Unicode layout blob provider. One bounded copy-out from the kernel
// (BWS 51, command 2) into process heap; validated before first use. Never
// retried in a tight loop: a failure disables complex layout for the process.
#include "unicode_layout.h"
#include "bursztyn_gui.h"
namespace BSF {
namespace {
uint8_t *blob{};
bool attempted{};
} // namespace
const uint8_t *unicode_layout_blob() {
  if (blob || attempted)
    return blob;
  attempted = true;
  auto *p = new uint8_t[UnicodeBlob::Size];
  if (!p)
    return nullptr;
  if (bws_wywolaj(51, 2, reinterpret_cast<uint64_t>(p), UnicodeBlob::Size, 0) != 1 ||
      !unicode_layout_valid(p, UnicodeBlob::Size)) {
    delete[] p;
    wypisz("[BSF] Unicode layout tables unavailable; complex layout disabled.\n");
    return nullptr;
  }
  blob = p;
  return blob;
}
} // namespace BSF
