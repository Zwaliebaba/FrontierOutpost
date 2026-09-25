// Tests/NeuronClientTests/Check.h
//
// What every test here does with an HRESULT. Included after pch.h, which brings <windows.h> and the
// test framework.
#pragma once

#include <format>
#include <string>

namespace NeuronClientTests
{

/// Fails the test when _result is a failure, naming the call and the code.
inline void Check(HRESULT _result, const wchar_t* _call)
{
  if (FAILED(_result))
  {
    const std::wstring message = std::format(L"{} failed with 0x{:08X}", _call, static_cast<unsigned long>(_result));
    Microsoft::VisualStudio::CppUnitTestFramework::Assert::Fail(message.c_str());
  }
}

} // namespace NeuronClientTests
