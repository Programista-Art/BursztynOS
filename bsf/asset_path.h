#pragma once
#include <stddef.h>
namespace BSF {
// Read-only public GUI assets. This never grants general filesystem access.
inline bool trusted_asset(const char *path, bool directory = false) {
  if (!path)
    return false;
  const char prefix[] = "/czcionki/ttf";
  for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
    if (!path[i] || path[i] != prefix[i])
      return false;
  if (directory)
    return path[13] == 0 || (path[13] == '/' && path[14] == 0);
  if (path[13] != '/')
    return false;
  size_t length = 0;
  for (; length < 120 && path[14 + length]; ++length) {
    unsigned c = (unsigned char)path[14 + length];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' ||
          c == ' '))
      return false;
  }
  if (length <= 4 || length == 120)
    return false;
  const char *end = path + 14 + length - 4;
  return end[0] == '.' && (end[1] == 't' || end[1] == 'T') &&
         (end[2] == 't' || end[2] == 'T') && (end[3] == 'f' || end[3] == 'F');
}
} // namespace BSF
