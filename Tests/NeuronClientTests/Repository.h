// Tests/NeuronClientTests/Repository.h
//
// Where the repository is, for the tests that read GameData. Included after pch.h, which brings
// <windows.h>.
#pragma once

#include <filesystem>
#include <string>

namespace NeuronClientTests
{

/// An address inside this library, which is how the library finds itself.
inline constexpr int MODULE_ANCHOR = 0;

/// The repository, found from this library: the tests build into <repository>\<platform>\<configuration>.
inline std::filesystem::path Repository()
{
  HMODULE self = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&MODULE_ANCHOR), &self);
  std::wstring path(MAX_PATH, L'\0');
  const DWORD length = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
  path.resize(length);
  return std::filesystem::path(path).parent_path().parent_path().parent_path();
}

} // namespace NeuronClientTests
