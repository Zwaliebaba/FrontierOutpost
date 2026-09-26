// Tests/NeuronClientTests/TestDevice.h
//
// A GraphicsDevice on WARP with the debug layer, as the core's tests use it, and the check they end
// with. Included after pch.h, which brings the test framework.
#pragma once

#include "Check.h"
#include "GraphicsDevice.h"

#include <cstdint>
#include <string>
#include <vector>

namespace NeuronClientTests
{

/// A device on WARP with the debug layer, and the failures it reported.
struct TestDevice
{
  std::vector<std::string> failures;
  Neuron::GraphicsDevice device; // after failures, which its onFailure writes to
};

inline void Open(TestDevice& _test, std::uint32_t _uploadPageBytes = Neuron::GraphicsDevice::DEFAULT_UPLOAD_PAGE_BYTES,
                 std::uint32_t _shaderDescriptors = Neuron::GraphicsDevice::DEFAULT_SHADER_DESCRIPTORS)
{
  std::string error;
  const Neuron::GraphicsDevice::Desc desc{.warp = true,
                                          .debugLayer = true,
                                          .gpuValidation = false,
                                          .onFailure = [&_test](const std::string& _message) { _test.failures.push_back(_message); },
                                          .uploadPageBytes = _uploadPageBytes,
                                          .shaderDescriptors = _shaderDescriptors};
  Microsoft::VisualStudio::CppUnitTestFramework::Assert::IsTrue(Neuron::GraphicsDevice::Create(desc, _test.device, error),
                                                                Widen(error).c_str());
  // Direct3D 12 devices are singletons per adapter, so what the device logged before this test
  // belongs to the tests before it.
  static_cast<void>(_test.device.TakeDebugMessages());
}

/// No failure was reported, and the debug layer saw nothing wrong.
inline void ExpectClean(TestDevice& _test)
{
  std::wstring faults;
  for (const std::string& failure : _test.failures)
  {
    faults += Widen(failure) + L"; ";
  }
  for (const std::string& message : _test.device.TakeDebugMessages())
  {
    faults += Widen(message) + L"; ";
  }
  Microsoft::VisualStudio::CppUnitTestFramework::Assert::IsTrue(faults.empty(), faults.c_str());
}

} // namespace NeuronClientTests
