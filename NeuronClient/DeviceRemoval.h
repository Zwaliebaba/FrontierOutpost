// NeuronClient/DeviceRemoval.h
//
// What the core reports when the device is removed, from what DRED recorded (plan §5.3). The
// core's own, and its tests'; no public header includes it. Include it after pch.h.
#pragma once

#include <d3d12.h>

#include <string>

namespace Neuron
{

/// The report of a removal for _reason: each command list DRED says the GPU had not finished, with
/// the operation it stopped at, then the address of a page fault and the allocations there, and
/// those freed from there not long before. Either may be null, where DRED had nothing to give.
[[nodiscard]] std::string DescribeRemoval(HRESULT _reason, const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT* _breadcrumbs,
                                          const D3D12_DRED_PAGE_FAULT_OUTPUT* _pageFault);

} // namespace Neuron
