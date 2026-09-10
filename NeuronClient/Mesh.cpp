// Mesh.cpp -- the vertex and index buffers for one authored mesh.

#include "pch.h"
#include "Mesh.h"

#include "D3D12Defaults.h"

namespace Neuron
{

void Mesh::Create(ID3D12Device* _device, std::span<const MeshVertex> _vertices, std::span<const std::uint16_t> _indices)
{
  ASSERT_TEXT(!_vertices.empty() && !_indices.empty(), L"A mesh with no triangles is an authoring mistake, not a valid mesh.");

  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);

  // An upload heap for meshes of a few dozen triangles. A default-heap copy would be the right
  // answer for real scenery; for a couple of kilobytes read once a frame it is machinery with
  // nothing to buy.
  const auto vertexBytes = static_cast<std::uint64_t>(_vertices.size_bytes());
  const D3D12_RESOURCE_DESC vertexDesc = BufferDesc(vertexBytes);
  winrt::check_hresult(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(m_vertices.put())));

  const auto indexBytes = static_cast<std::uint64_t>(_indices.size_bytes());
  const D3D12_RESOURCE_DESC indexDesc = BufferDesc(indexBytes);
  winrt::check_hresult(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &indexDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(m_indices.put())));

  const D3D12_RANGE readNothing = {0, 0};
  void* mapped = nullptr;

  winrt::check_hresult(m_vertices->Map(0, &readNothing, &mapped));
  std::memcpy(mapped, _vertices.data(), _vertices.size_bytes());
  m_vertices->Unmap(0, nullptr);

  winrt::check_hresult(m_indices->Map(0, &readNothing, &mapped));
  std::memcpy(mapped, _indices.data(), _indices.size_bytes());
  m_indices->Unmap(0, nullptr);

  m_vertexView.BufferLocation = m_vertices->GetGPUVirtualAddress();
  m_vertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
  m_vertexView.StrideInBytes = sizeof(MeshVertex);

  m_indexView.BufferLocation = m_indices->GetGPUVirtualAddress();
  m_indexView.SizeInBytes = static_cast<UINT>(indexBytes);
  m_indexView.Format = DXGI_FORMAT_R16_UINT;

  m_indexCount = static_cast<std::uint32_t>(_indices.size());
}

} // namespace Neuron
