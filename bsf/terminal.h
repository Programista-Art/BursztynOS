#pragma once
#include "runtime.h"
namespace BSF {
// Logical LTR grid policy, Unicode 15.1: W/F bases occupy two cells, ambiguous
// and other bases one; marks/ignorables have no independent cell. An EGC uses
// its maximum base width. This does not shape emoji, Indic or bidi text.
unsigned terminal_cells(const char*,size_t);
// Existing versioned GCB Extend/ZWJ/SpacingMark properties, including CCC=0.
bool terminal_attachment(uint32_t);
// Verify actual unrounded ASCII advances and cmap coverage, never filenames.
uint32_t terminal_face(const char* family=nullptr);
}
