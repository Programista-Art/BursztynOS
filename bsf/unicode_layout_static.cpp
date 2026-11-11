// Static Unicode layout blob provider: host builds and kernel (BWS 51/2).
#include "unicode_layout.h"
namespace BSF {
namespace {
#include "unicode_layout_data.inc"
static_assert(sizeof(bsf_unicode_layout_blob) == UnicodeBlob::Size, "blob/index mismatch");
} // namespace
const uint8_t *unicode_layout_blob() { return bsf_unicode_layout_blob; }
} // namespace BSF
