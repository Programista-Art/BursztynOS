#pragma once
namespace BSF {
// GUI new[] adds a 32-byte aligned block header. BWS35 permits a 16 MiB
// raw allocation; reserve that header instead of expanding kernel limits.
constexpr unsigned MaxFile=16*1024*1024-32;
constexpr unsigned MaxLoadedBytes=16*1024*1024;
constexpr unsigned MaxFiles=128,MaxExaminedFiles=1024,DirectoryBytes=64*1024;
}
