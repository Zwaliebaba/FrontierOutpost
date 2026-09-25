// NeuronClient/Unicode.cpp
#include "pch.h"

#include "Unicode.h"

#include <climits>

namespace Neuron
{

std::wstring Utf8ToUtf16(std::string_view _utf8)
{
  if (_utf8.empty() || _utf8.size() > INT_MAX)
  {
    return {};
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, _utf8.data(), static_cast<int>(_utf8.size()), nullptr, 0);
  std::wstring utf16(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, _utf8.data(), static_cast<int>(_utf8.size()), utf16.data(), length);
  return utf16;
}

std::string Utf16ToUtf8(std::wstring_view _utf16)
{
  if (_utf16.empty() || _utf16.size() > INT_MAX)
  {
    return {};
  }
  const int length = WideCharToMultiByte(CP_UTF8, 0, _utf16.data(), static_cast<int>(_utf16.size()), nullptr, 0, nullptr, nullptr);
  std::string utf8(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, _utf16.data(), static_cast<int>(_utf16.size()), utf8.data(), length, nullptr, nullptr);
  return utf8;
}

} // namespace Neuron
