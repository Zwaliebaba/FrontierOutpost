// NeuronClient/DeviceRemoval.cpp
#include "pch.h"

#include "DeviceRemoval.h"
#include "Unicode.h"

#include <format>
#include <span>
#include <string_view>

namespace Neuron
{

namespace
{

/// An operation, as the command list's method that records it is named.
std::string OperationName(D3D12_AUTO_BREADCRUMB_OP _operation)
{
  switch (_operation)
  {
  case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:
    return "SetMarker";
  case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT:
    return "BeginEvent";
  case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT:
    return "EndEvent";
  case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:
    return "DrawInstanced";
  case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:
    return "DrawIndexedInstanced";
  case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:
    return "Dispatch";
  case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:
    return "CopyBufferRegion";
  case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:
    return "CopyTextureRegion";
  case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:
    return "CopyResource";
  case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:
    return "ClearRenderTargetView";
  case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW:
    return "ClearUnorderedAccessView";
  case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:
    return "ClearDepthStencilView";
  case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:
    return "ResourceBarrier";
  case D3D12_AUTO_BREADCRUMB_OP_PRESENT:
    return "Present";
  case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION:
    return "the start of its submission";
  case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION:
    return "the end of its submission";
  default:
    return std::format("operation {}", static_cast<int>(_operation));
  }
}

/// A kind of allocation, as a page fault's report names it.
std::string AllocationTypeName(D3D12_DRED_ALLOCATION_TYPE _type)
{
  switch (_type)
  {
  case D3D12_DRED_ALLOCATION_TYPE_RESOURCE:
    return "a resource";
  case D3D12_DRED_ALLOCATION_TYPE_HEAP:
    return "a heap";
  case D3D12_DRED_ALLOCATION_TYPE_DESCRIPTOR_HEAP:
    return "a descriptor heap";
  case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_STATE:
    return "a pipeline state";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_LIST:
    return "a command list";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_ALLOCATOR:
    return "a command allocator";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE:
    return "a command queue";
  case D3D12_DRED_ALLOCATION_TYPE_FENCE:
    return "a fence";
  default:
    return std::format("an allocation of type {}", static_cast<unsigned>(_type));
  }
}

/// An object's debug name as DRED gives it, wide or narrow, in quotes; empty when it has none.
std::string DebugName(const wchar_t* _wide, const char* _narrow)
{
  if (_wide != nullptr)
  {
    return std::format("\"{}\"", Utf16ToUtf8(_wide));
  }
  if (_narrow != nullptr)
  {
    return std::format("\"{}\"", _narrow);
  }
  return {};
}

/// An object of kind _kind by its name: queue "Scene", or an unnamed queue.
std::string Named(std::string_view _kind, const std::string& _name)
{
  return _name.empty() ? std::format("an unnamed {}", _kind) : std::format("{} {}", _kind, _name);
}

/// The command lists the GPU had not finished: how far each got, and the operation it stopped at.
void DescribeBreadcrumbs(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT& _breadcrumbs, std::string& _report)
{
  bool unfinished = false;
  for (const D3D12_AUTO_BREADCRUMB_NODE* node = _breadcrumbs.pHeadAutoBreadcrumbNode; node != nullptr; node = node->pNext)
  {
    // The breadcrumb counts the operations the GPU had finished.
    const UINT32 finished = node->pLastBreadcrumbValue != nullptr ? *node->pLastBreadcrumbValue : 0;
    if (finished >= node->BreadcrumbCount)
    {
      continue;
    }
    unfinished = true;
    _report +=
      std::format("\nDRED: {} on {} had finished {} of its {} operations",
                  Named("command list", DebugName(node->pCommandListDebugNameW, node->pCommandListDebugNameA)),
                  Named("queue", DebugName(node->pCommandQueueDebugNameW, node->pCommandQueueDebugNameA)), finished, node->BreadcrumbCount);
    if (node->pCommandHistory != nullptr)
    {
      const std::span<const D3D12_AUTO_BREADCRUMB_OP> history(node->pCommandHistory, node->BreadcrumbCount);
      if (finished > 0)
      {
        _report += std::format(", up to {}", OperationName(history[finished - 1]));
      }
      _report += std::format("; the next was {}", OperationName(history[finished]));
    }
  }
  if (!unfinished)
  {
    _report += "\nDRED: the GPU had finished every command list it was given";
  }
}

/// The allocations of one of DRED's lists, each by name and kind.
std::string AllocationsOf(const D3D12_DRED_ALLOCATION_NODE* _head)
{
  std::string allocations;
  for (const D3D12_DRED_ALLOCATION_NODE* node = _head; node != nullptr; node = node->pNext)
  {
    const std::string name = DebugName(node->ObjectNameW, node->ObjectNameA);
    const std::string type = AllocationTypeName(node->AllocationType);
    allocations += std::format("{}{}", allocations.empty() ? "" : "; ",
                               name.empty() ? std::format("{} with no name", type) : std::format("{}, {}", name, type));
  }
  return allocations;
}

/// Where the GPU faulted, and what was allocated there, or had been until not long before.
void DescribePageFault(const D3D12_DRED_PAGE_FAULT_OUTPUT& _pageFault, std::string& _report)
{
  const std::string existing = AllocationsOf(_pageFault.pHeadExistingAllocationNode);
  const std::string freed = AllocationsOf(_pageFault.pHeadRecentFreedAllocationNode);
  if (_pageFault.PageFaultVA == 0 && existing.empty() && freed.empty())
  {
    _report += "\nDRED: no page fault was recorded";
    return;
  }
  _report += std::format("\nDRED: a page fault at GPU address 0x{:016x}", _pageFault.PageFaultVA);
  _report += std::format("\nDRED: allocated there: {}", existing.empty() ? std::string("nothing") : existing);
  if (!freed.empty())
  {
    _report += std::format("\nDRED: freed from there not long before: {}", freed);
  }
}

} // namespace

std::string DescribeRemoval(HRESULT _reason, const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT* _breadcrumbs,
                            const D3D12_DRED_PAGE_FAULT_OUTPUT* _pageFault)
{
  std::string report = std::format("Direct3D 12: the device was removed (0x{:08x})", static_cast<unsigned long>(_reason));
  if (_breadcrumbs == nullptr && _pageFault == nullptr)
  {
    report += "\nDRED: nothing was recorded, which needs Windows 10 1903 or later";
    return report;
  }
  if (_breadcrumbs != nullptr)
  {
    DescribeBreadcrumbs(*_breadcrumbs, report);
  }
  if (_pageFault != nullptr)
  {
    DescribePageFault(*_pageFault, report);
  }
  return report;
}

} // namespace Neuron
