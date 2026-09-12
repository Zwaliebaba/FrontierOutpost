#pragma once

#include <string>
#include <string_view>

namespace Neuron
{

/// UTF-8 in, UTF-16 out, through the one conversion the Windows SDK provides.
///
/// Every `std::string` in this tree is UTF-8 and every Windows API that takes a path, a window
/// title or a module name takes UTF-16. This is the seam between the two. A narrowing that replaces
/// anything outside ASCII with a question mark is how a store beside an executable in a folder with
/// a diacritic in its name never gets written.
[[nodiscard]] inline std::wstring Utf8ToWide(std::string_view _utf8)
{
  if (_utf8.empty())
  {
    return {};
  }

  const int needed = MultiByteToWideChar(CP_UTF8, 0, _utf8.data(), static_cast<int>(_utf8.size()), nullptr, 0);
  if (needed <= 0)
  {
    return {};
  }

  std::wstring wide(static_cast<std::size_t>(needed), L'\0');
  (void)MultiByteToWideChar(CP_UTF8, 0, _utf8.data(), static_cast<int>(_utf8.size()), wide.data(), needed);
  return wide;
}

[[nodiscard]] inline std::string WideToUtf8(std::wstring_view _wide)
{
  if (_wide.empty())
  {
    return {};
  }

  const int needed = WideCharToMultiByte(CP_UTF8, 0, _wide.data(), static_cast<int>(_wide.size()), nullptr, 0, nullptr, nullptr);
  if (needed <= 0)
  {
    return {};
  }

  std::string utf8(static_cast<std::size_t>(needed), '\0');
  (void)WideCharToMultiByte(CP_UTF8, 0, _wide.data(), static_cast<int>(_wide.size()), utf8.data(), needed, nullptr, nullptr);
  return utf8;
}

} // namespace Neuron
