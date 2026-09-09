// DescriptorHeap.cpp -- see DescriptorHeap.h for why this allocates and never frees.

#include "pch.h"
#include "DescriptorHeap.h"

namespace Neuron
{

void DescriptorHeap::Create(ID3D12Device* _device, D3D12_DESCRIPTOR_HEAP_TYPE _type, std::uint32_t _capacity, bool _shaderVisible)
{
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.Type = _type;
  heapDesc.NumDescriptors = _capacity;
  heapDesc.Flags = _shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  winrt::check_hresult(_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_heap.put())));

  m_descriptorSize = _device->GetDescriptorHandleIncrementSize(_type);
  m_capacity = _capacity;
  m_used = 0;
  m_shaderVisible = _shaderVisible;
}

std::uint32_t DescriptorHeap::Allocate()
{
  ASSERT_TEXT(m_used < m_capacity, L"DescriptorHeap is full; raise the capacity where it is created.");
  return m_used++;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::CpuHandle(std::uint32_t _slot) const noexcept
{
  D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(_slot) * m_descriptorSize;
  return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GpuHandle(std::uint32_t _slot) const
{
  DEBUG_ASSERT(m_shaderVisible);
  D3D12_GPU_DESCRIPTOR_HANDLE handle = m_heap->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<UINT64>(_slot) * m_descriptorSize;
  return handle;
}

} // namespace Neuron
