#pragma once

namespace Neuron
{

/// A descriptor heap that hands out slots in order and never gives one back.
///
/// Linear allocation with no free list is the right shape for what this client does: every
/// descriptor it creates -- the index target's SRV, the font texture's SRV -- is made once at
/// startup and lives until the process ends. A general allocator here would be machinery serving
/// a case that does not exist (AGENTS.md R15).
///
/// Only one shader-visible CBV/SRV/UAV heap can be bound at a time, so the client owns exactly
/// one and every renderer allocates out of it. That is the reason this is a type rather than
/// three private heaps in three classes.
class DescriptorHeap
{
public:
  void Create(ID3D12Device* _device, D3D12_DESCRIPTOR_HEAP_TYPE _type, std::uint32_t _capacity, bool _shaderVisible);

  /// The index of the next free slot. Fails through Debug.h rather than overrunning: a heap that
  /// is one descriptor short writes over somebody else's view and shows up as the wrong texture
  /// three passes later.
  [[nodiscard]] std::uint32_t Allocate();

  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(std::uint32_t _slot) const noexcept;
  /// Not noexcept, unlike CpuHandle: asking a CPU-only heap for a GPU handle is a broken
  /// invariant, and a broken invariant throws in this tree (Debug.h).
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(std::uint32_t _slot) const;
  [[nodiscard]] ID3D12DescriptorHeap* Handle() const noexcept
  {
    return m_heap.get();
  }

private:
  winrt::com_ptr<ID3D12DescriptorHeap> m_heap;
  std::uint32_t m_descriptorSize = 0;
  std::uint32_t m_capacity = 0;
  std::uint32_t m_used = 0;
  bool m_shaderVisible = false;
};

} // namespace Neuron
