#pragma once
namespace BSF {
// GUI new[] adds a 32-byte aligned block header. BWS35 permits a 16 MiB
// raw allocation; reserve that header instead of expanding kernel limits.
constexpr unsigned MaxFile=16*1024*1024-32;
constexpr unsigned MaxLoadedBytes=16*1024*1024;
constexpr unsigned MaxFiles=128,MaxExaminedFiles=1024,DirectoryBytes=64*1024;
// BSF 2.2: font files are read face-by-face (table ranges) from Ring 3, so a
// collection may exceed one allocation. Bounded by realistic CJK collections
// (Noto Serif CJK TTC is 26.3 MB). Each table still fits one MaxFile buffer.
constexpr unsigned MaxFontFile=64u*1024*1024;
// Plain sfnt files up to this size keep the BSF 2.1 whole-file load path.
constexpr unsigned SmallFontFile=4u*1024*1024;
// Per-process resident font table bytes (one CJK face is ~16 MB of tables).
constexpr unsigned MaxResidentFontBytes=40u*1024*1024;
}
