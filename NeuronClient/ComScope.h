// NeuronClient/ComScope.h
//
// Internal: only NeuronClient's own .cpp files include it, after pch.h. It is not part of the API
// liblt sees (Design/ADR/ADR-005).
#pragma once

#include <objbase.h>

namespace Neuron
{

/// COM on the calling thread for as long as this lives. A thread that already has COM, in either
/// apartment, keeps it as it is. It is destroyed on the thread that created it.
class ComScope
{
public:
  ComScope() noexcept
    : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
  {
  }

  ~ComScope()
  {
    if (SUCCEEDED(m_result))
    {
      CoUninitialize();
    }
  }

  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

private:
  HRESULT m_result;
};

} // namespace Neuron
