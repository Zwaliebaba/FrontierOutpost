#include "Renderer.h"
#include "Bound.h"
#include "Color.h"
#include "CubeMap.h"
#include "Matrix.h"
#include "Mesh.h"
#include "ProgramLog.h"
#include "RendererCore.h"
#include "Shader.h"
#include "ShaderInstance.h"
#include "Stack.h"
#include "Texture2D.h"
#include "Transform.h"
#include "Tuple.h"
#include "Window.h"

#include <array>
#include <climits>
#include <cstdint>
#include <span>

/* liblt's renderer on NeuronClient's DrawContext (Design/Plan/NeuronClient-
 * migration.md, sections 5.4 and 5.5). It keeps the state GL kept, as GL kept
 * it: pushed and popped on stacks, and taken by each draw and clear as it is
 * then. The targets are the colour attachments of the stacks' tops, in the
 * order of their slots, or the frame texture when neither colour nor depth is
 * pushed, as GL drew into its default framebuffer. */

const bool kAllow16BitIndices = true;
const bool kCameraSpaceRendering = true;
const size_t kMaxColorAttachments = 4;

namespace {
  struct Attachment {
    uint64 id;
    uint layer;

    Attachment() {}
    Attachment(uint64 id, uint layer) :
      id(id),
      layer(layer)
      {}
  };

  struct Renderer {
    Matrix world;
    Matrix view;
    Matrix proj;
    Matrix worldIT;
    Matrix wvp;
    Transform viewTransform;
    int callCount;
    int callCountLast;
    int polyCount;
    int polyCountLast;

    Stack<Attachment> colorAttachment[kMaxColorAttachments];
    Stack<uint64> depthAttachment;

    Stack<BlendMode::Enum> blendMode;
    Stack<CullMode::Enum> cullMode;
    Stack< Tuple3<bool, V2, V2> > scissor;
    Stack< V4T<int> > viewport;
    Vector<int> zBuffer;
    Vector<int> zWritable;

    /* The culling GL was left with: the cull stack's, until a clear reset it
       to back faces, as Renderer_Clear did GL's. */
    CullMode::Enum cull;
    bool wireframe;
    /* The viewport GL was left with, which popping the last one did not
       change; until the first push, the whole target. */
    V4T<int> glViewport;
    bool glViewportSet;
    bool warnedBlend;
    bool warnedNoProgram;
    bool warnedIncomplete;
    bool warnedDepthSize;

    Renderer() :
      callCount(0),
      callCountLast(0),
      polyCount(0),
      polyCountLast(0),
      cull(CullMode::Backface),
      wireframe(false),
      glViewport(0, 0, 0, 0),
      glViewportSet(false),
      warnedBlend(false),
      warnedNoProgram(false),
      warnedIncomplete(false),
      warnedDepthSize(false)
      {}
  } renderer;

  /* What a draw's colour and depth go to, as GL's bound framebuffer had them.
     A texture attached that has gone, or that has no texels, left GL's
     framebuffer incomplete, and GL drew nothing into it. */
  struct Targets {
    Neuron::ColorTarget colors[kMaxColorAttachments];
    uint colorCount;
    Neuron::Texture* depth;
    bool incomplete;
  };

  Targets CurrentTargets() {
    Targets targets;
    targets.colorCount = 0;
    targets.depth = nullptr;
    targets.incomplete = false;
    for (size_t i = 0; i < kMaxColorAttachments; ++i) {
      if (!renderer.colorAttachment[i].size() || !renderer.colorAttachment[i].back().id)
        continue;
      Attachment const& attachment = renderer.colorAttachment[i].back();
      GpuTexture* texture = GpuTexture_Find(attachment.id);
      if (!texture || !texture->texture) {
        targets.incomplete = true;
        continue;
      }
      Neuron::ColorTarget& target = targets.colors[targets.colorCount++];
      target.texture = &texture->texture;
      target.mip = 0;
      target.layer = attachment.layer;
    }

    if (renderer.depthAttachment.size() && renderer.depthAttachment.back()) {
      GpuTexture* texture = GpuTexture_Find(renderer.depthAttachment.back());
      if (texture && texture->texture)
        targets.depth = &texture->texture;
      else
        targets.incomplete = true;
    }

    /* Neither colour nor depth: GL's default framebuffer. */
    if (!targets.colorCount && !targets.depth && !targets.incomplete) {
      Neuron::ColorTarget& target = targets.colors[targets.colorCount++];
      target.texture = &Renderer_GetFrame().texture;
      target.mip = 0;
      target.layer = 0;
    }
    return targets;
  }

  bool ScissorOn() {
    return renderer.scissor.size() && renderer.scissor.back().x;
  }

  /* GL refused a negative size and kept the rectangle it had; nothing is
     drawn in one here. */
  Neuron::PixelRect ScissorRect() {
    V2 const& origin = renderer.scissor.back().y;
    V2 const& size = renderer.scissor.back().z;
    Neuron::PixelRect rect;
    rect.xPixels = (int)origin.x;
    rect.yPixels = (int)origin.y;
    rect.widthPixels = Max((int)size.x, 0);
    rect.heightPixels = Max((int)size.y, 0);
    return rect;
  }

  Neuron::BlendMode ToNeuron(BlendMode::Enum mode) {
    switch (mode) {
    case BlendMode::Additive: return Neuron::BlendMode::Additive;
    case BlendMode::Alpha:    return Neuron::BlendMode::Alpha;
    case BlendMode::Disabled: return Neuron::BlendMode::Opaque;
    default:
      /* Complementary and Multiplicative: nothing sets them, and the core has
         neither (plan section 5.5). Were one set, it would show here. */
      if (!renderer.warnedBlend) {
        renderer.warnedBlend = true;
        Log_Error("Renderer: blend mode " + ToString((int)mode) +
          " is not in the Direct3D 12 core; drawing opaque");
      }
      return Neuron::BlendMode::Opaque;
    }
  }

  Neuron::CullMode ToNeuron(CullMode::Enum mode) {
    switch (mode) {
    case CullMode::Backface:  return Neuron::CullMode::Back;
    case CullMode::Frontface: return Neuron::CullMode::Front;
    default:                  return Neuron::CullMode::None;
    }
  }

  /* Sets the context's targets, viewport, scissor and state as GL's were. */
  Targets PrepareTargets(Neuron::DrawContext& context) {
    Targets targets = CurrentTargets();
    context.SetTargets(
      std::span<Neuron::ColorTarget const>(targets.colors, targets.colorCount),
      targets.depth);

    /* GL kept its viewport and scissor through a change of framebuffer. */
    if (renderer.glViewportSet)
      context.SetViewport(
        renderer.glViewport.x, renderer.glViewport.y,
        Max(renderer.glViewport.z, 0), Max(renderer.glViewport.w, 0));
    if (ScissorOn()) {
      Neuron::PixelRect const rect = ScissorRect();
      context.SetScissor(rect.xPixels, rect.yPixels, rect.widthPixels, rect.heightPixels);
    }

    Neuron::RenderState state;
    state.blend = ToNeuron(renderer.blendMode.back());
    state.cull = ToNeuron(renderer.cull);
    state.depthTest = renderer.zBuffer.back() != 0;
    state.depthWrite = renderer.zWritable.back() != 0;
    state.wireframe = renderer.wireframe;

    /* GL drew into colour and depth of different sizes where they overlapped;
       Direct3D 12 binds depth only under targets of its size, so such a draw
       goes without its depth test. */
    if (state.depthTest && targets.depth && targets.colorCount) {
      Neuron::ColorTarget const& first = targets.colors[0];
      if (first.texture->WidthPixels(first.mip) != targets.depth->WidthPixels() ||
          first.texture->HeightPixels(first.mip) != targets.depth->HeightPixels())
      {
        state.depthTest = false;
        targets.depth = nullptr;
        if (!renderer.warnedDepthSize) {
          renderer.warnedDepthSize = true;
          Log_Warning("Renderer: a depth-tested draw under targets of another "
            "size than its depth buffer; drawn without the depth test");
        }
      }
    }
    context.SetState(state);
    return targets;
  }

  /* Readies a draw: false when no program is current, as a draw GL gave to
     its fixed function, which Direct3D 12 does not have, and when the targets
     are incomplete. */
  bool PrepareDraw(Neuron::DrawContext& context) {
    if (CurrentTargets().incomplete) {
      if (!renderer.warnedIncomplete) {
        renderer.warnedIncomplete = true;
        Log_Warning("Renderer: a draw into a texture that has gone or has no "
          "texels; nothing was drawn");
      }
      return false;
    }
    Targets const targets = PrepareTargets(context);
    Neuron::Texture const* drawnInto[kMaxColorAttachments + 1];
    size_t count = 0;
    for (uint i = 0; i < targets.colorCount; ++i)
      drawnInto[count++] = targets.colors[i].texture;
    if (targets.depth && renderer.zBuffer.back())
      drawnInto[count++] = targets.depth;
    if (!Shader_BindActive(context, std::span<Neuron::Texture const* const>(drawnInto, count))) {
      if (!renderer.warnedNoProgram) {
        renderer.warnedNoProgram = true;
        Log_Error("Renderer: a draw with no shader set, which only GL's fixed "
          "function drew; nothing was drawn");
      }
      return false;
    }
    return true;
  }

  void Renderer_ResetGlobalState() {
    /* What Renderer_Clear did to GL behind the stacks' backs: no program, and
       back faces culled. */
    Shader_UseFixedFunction();
    renderer.cull = CullMode::Backface;
  }

  Neuron::VertexAttribute Attribute(uint index, Neuron::VertexFormat format, size_t offset) {
    Neuron::VertexAttribute attribute;
    attribute.semantic = "ATTRIB";
    attribute.index = index;
    attribute.format = format;
    attribute.offsetBytes = (uint32_t)offset;
    return attribute;
  }

  /* A mesh's vertex: position, normal and uv, which the shaders read as
     attributes 0, 1 and 2, as GL's bound them. */
  std::array<Neuron::VertexAttribute, 3> const& VertexAttributes() {
    static std::array<Neuron::VertexAttribute, 3> const attributes = {
      Attribute(0, Neuron::VertexFormat::Float3, offsetof(Vertex, p)),
      Attribute(1, Neuron::VertexFormat::Float3, offsetof(Vertex, n)),
      Attribute(2, Neuron::VertexFormat::Float2, offsetof(Vertex, u))};
    return attributes;
  }

  Neuron::VertexLayout VertexLayoutOf() {
    Neuron::VertexLayout layout;
    layout.attributes = VertexAttributes();
    layout.strideBytes = sizeof(Vertex);
    return layout;
  }

  MeshBuffers& PrepareMeshForDraw(MeshT const* mesh) {
    /* A mesh is uploaded when it is first drawn, and again once it has
       changed. The old buffers go once the GPU has finished with them. */
    if (mesh->gpu.buffers && mesh->bufferVersion == mesh->version)
      return *mesh->gpu.buffers;

    mesh->gpu.Reset();
    mesh->bufferVersion = mesh->version;
    MeshBuffers* buffers = new MeshBuffers;
    mesh->gpu.buffers = buffers;

    Neuron::GraphicsDevice& device = Renderer_Device();
    Neuron::DrawContext& context = device.Context();

    Neuron::Buffer::Desc vertexDesc;
    vertexDesc.sizeBytes = (uint32_t)(sizeof(Vertex) * mesh->vertices.size());
    vertexDesc.strideBytes = sizeof(Vertex);
    vertexDesc.name = "liblt mesh vertices";
    buffers->vertices = device.CreateBuffer(vertexDesc);
    context.UpdateBuffer(buffers->vertices, 0,
      std::as_bytes(std::span<Vertex const>(mesh->vertices.data(), mesh->vertices.size())));

    /* If the indices fit in 16 bits, they are stored that way to save space. */
    buffers->indexCount = (uint)mesh->indices.size();
    Neuron::Buffer::Desc indexDesc;
    indexDesc.name = "liblt mesh indices";
    if (kAllow16BitIndices && mesh->vertices.size() < USHRT_MAX) {
      static Vector<ushort> indices;
      indices.clear();
      for (uint i = 0; i < mesh->indices.size(); ++i)
        indices << (ushort)mesh->indices[i];
      buffers->indexFormat = Neuron::IndexFormat::UInt16;
      indexDesc.sizeBytes = (uint32_t)(sizeof(ushort) * indices.size());
      indexDesc.strideBytes = sizeof(ushort);
      buffers->indices = device.CreateBuffer(indexDesc);
      context.UpdateBuffer(buffers->indices, 0,
        std::as_bytes(std::span<ushort const>(indices.data(), indices.size())));
    } else {
      buffers->indexFormat = Neuron::IndexFormat::UInt32;
      indexDesc.sizeBytes = (uint32_t)(sizeof(uint) * mesh->indices.size());
      indexDesc.strideBytes = sizeof(uint);
      buffers->indices = device.CreateBuffer(indexDesc);
      context.UpdateBuffer(buffers->indices, 0,
        std::as_bytes(std::span<uint const>(mesh->indices.data(), mesh->indices.size())));
    }
    return *buffers;
  }

  /* Direct3D has no 8-bit indices, so those are widened to 16 bits. */
  std::span<std::byte const> Indices(
    void const* data,
    uint count,
    IndexFormat::Enum format,
    Neuron::IndexFormat& outFormat,
    uint& outLargest)
  {
    static Vector<ushort> widened;
    outLargest = 0;
    switch (format) {
    case IndexFormat::Byte: {
      widened.clear();
      for (uint i = 0; i < count; ++i) {
        ushort const index = ((uchar const*)data)[i];
        widened << index;
        outLargest = Max(outLargest, (uint)index);
      }
      outFormat = Neuron::IndexFormat::UInt16;
      return std::as_bytes(std::span<ushort const>(widened.data(), widened.size()));
    }
    case IndexFormat::Short:
      for (uint i = 0; i < count; ++i)
        outLargest = Max(outLargest, (uint)((ushort const*)data)[i]);
      outFormat = Neuron::IndexFormat::UInt16;
      return std::as_bytes(std::span<ushort const>((ushort const*)data, count));
    case IndexFormat::Int:
      for (uint i = 0; i < count; ++i)
        outLargest = Max(outLargest, ((uint const*)data)[i]);
      outFormat = Neuron::IndexFormat::UInt32;
      return std::as_bytes(std::span<uint const>((uint const*)data, count));
    }
    outFormat = Neuron::IndexFormat::UInt32;
    return std::span<std::byte const>();
  }

  void StaticDrawVertices(
    Vertex const* vertexData,
    size_t vertexCount,
    void const* indexData,
    IndexFormat::Enum indexFormat,
    size_t indexCount)
  {
    if (!vertexCount || !indexCount)
      return;
    Neuron::DrawContext& context = Renderer_Context();
    if (!PrepareDraw(context))
      return;
    Neuron::IndexFormat format;
    uint largest;
    std::span<std::byte const> indices = Indices(indexData, (uint)indexCount, indexFormat, format, largest);
    context.DrawTransient(
      std::as_bytes(std::span<Vertex const>(vertexData, vertexCount)),
      VertexLayoutOf(), indices, format);
    renderer.callCount++;
    renderer.polyCount += (int)(indexCount / 3);
  }
}

namespace LTE {
  void Renderer_Initialize(bool warp, bool offscreen) {
    Renderer_InitializeCore(warp, offscreen);
    Renderer_ResetGlobalState();
    Renderer_ClearMatrices();

    Renderer_PushBlendMode(BlendMode::Disabled);
    Renderer_PushCullMode(CullMode::Backface);
    Renderer_PushZBuffer(true);
    Renderer_PushZWritable(true);
  }

// ----------------------------------------------------------------------------

  static void InjectMatrices(ShaderT& shader) {
    shader.BindMatrices(
      Renderer_GetWorldMatrix(),
      Renderer_GetViewMatrix(),
      Renderer_GetProjMatrix(),
      Renderer_GetWorldITMatrix(),
      Renderer_GetWorldViewProjMatrix());
  }

// ----------------------------------------------------------------------------

  void Renderer_Clear(V4 const& clearColor) {
    /* GL cleared every colour buffer drawn into, within the scissor when it
       was on. */
    Neuron::DrawContext& context = Renderer_Context();
    Targets const targets = CurrentTargets();
    if (targets.incomplete) {
      Renderer_ResetGlobalState();
      return;
    }
    std::array<float, 4> const color = {clearColor.x, clearColor.y, clearColor.z, clearColor.w};
    for (uint i = 0; i < targets.colorCount; ++i) {
      Neuron::ColorTarget const& target = targets.colors[i];
      if (ScissorOn())
        context.ClearColor(*target.texture, target.mip, target.layer, color, ScissorRect());
      else
        context.ClearColor(*target.texture, target.mip, target.layer, color);
    }
    Renderer_ResetGlobalState();
  }

  void Renderer_ClearDepth(float depth) {
    /* GL's depth mask held for clears as for draws, and its depth range is
       [0, 1]. */
    if (!renderer.zWritable.back())
      return;
    Targets const targets = CurrentTargets();
    if (targets.incomplete || !targets.depth)
      return;
    float const clamped = Clamp(depth, 0.0f, 1.0f);
    if (ScissorOn())
      Renderer_Context().ClearDepth(*targets.depth, clamped, ScissorRect());
    else
      Renderer_Context().ClearDepth(*targets.depth, clamped);
  }

  void Renderer_ClearMatrices() {
    renderer.world =
    renderer.view =
    renderer.proj =
    renderer.worldIT =
    renderer.wvp =
    Matrix::Identity();
  }

  void Renderer_DrawFSQ() {
    Renderer_PushBlendMode(BlendMode::Disabled);
    Renderer_PushZBuffer(false);
    Renderer_DrawQuad();
    Renderer_PopZBuffer();
    Renderer_PopBlendMode();
    renderer.callCount++;
  }

  void Renderer_DrawFSQInParts(uint width, uint height, uint parts) {
    uint x = 0;
    uint w = width / parts;

    for (size_t i = 0; i < parts - 1; ++i) {
      Renderer_PushScissorOn(V2((float)x, 0), V2((float)w, (float)height));
      Renderer_DrawQuad();
      Renderer_PopScissor();
      Renderer_Finish();
      x += w;
    }

    Renderer_PushScissorOn(V2((float)x, 0), V2((float)(width - x), (float)height));
    Renderer_DrawQuad();
    Renderer_PopScissor();
  }

  void Renderer_DrawFSQPart(uint x, uint y, uint width, uint height) {
    Renderer_PushScissorOn(V2((float)x, (float)y), V2((float)width, (float)height));
    Renderer_DrawQuad();
    Renderer_PopScissor();
  }

  void Renderer_DrawMesh(MeshT const* mesh) {
    if (!mesh->vertices.size() || !mesh->indices.size())
      return;
    MeshBuffers& buffers = PrepareMeshForDraw(mesh);
    Neuron::DrawContext& context = Renderer_Context();
    if (!PrepareDraw(context))
      return;
    context.DrawIndexed(
      buffers.vertices, VertexLayoutOf(), buffers.indices, buffers.indexFormat,
      0, buffers.indexCount);

    renderer.callCount++;
    renderer.polyCount += (int)(buffers.indexCount / 3);
  }

  void Renderer_DrawQuad(
    V2 const& p1,
    V2 const& p2,
    V2 const& t1,
    V2 const& t2,
    float depth)
  {
    struct VertexPT {
      V3 p;
      V2 t;

      VertexPT(V3 const& p, V2 const& t) :
        p(p),
        t(t)
        {}
    };

    const VertexPT vertices[] = {
      VertexPT(V3(p1.x, p1.y, depth), V2(t1.x, t1.y)),
      VertexPT(V3(p2.x, p1.y, depth), V2(t2.x, t1.y)),
      VertexPT(V3(p2.x, p2.y, depth), V2(t2.x, t2.y)),
      VertexPT(V3(p1.x, p2.y, depth), V2(t1.x, t2.y)),
    };

    /* 16-bit, since Direct3D has no 8-bit indices. */
    const ushort indices[] = {
      0, 1, 2, 0, 2, 3,
    };

    /* Position and uv; the shaders' normal reads GL's (0, 0, 0, 1), as the
       attribute array GL had disabled did. */
    static std::array<Neuron::VertexAttribute, 2> const attributes = {
      Attribute(0, Neuron::VertexFormat::Float3, offsetof(VertexPT, p)),
      Attribute(2, Neuron::VertexFormat::Float2, offsetof(VertexPT, t))};
    Neuron::VertexLayout layout;
    layout.attributes = attributes;
    layout.strideBytes = sizeof(VertexPT);

    Neuron::DrawContext& context = Renderer_Context();
    if (!PrepareDraw(context))
      return;
    context.DrawTransient(
      std::as_bytes(std::span<VertexPT const>(vertices)), layout,
      std::as_bytes(std::span<ushort const>(indices)), Neuron::IndexFormat::UInt16);
    renderer.callCount++;
  }

  void Renderer_DrawVertices(
    Vector<Vertex> const& vertices,
    Vector<uint> const& indices)
  {
    StaticDrawVertices(
      vertices.data(),
      vertices.size(),
      indices.data(),
      IndexFormat::Int,
      indices.size());
  }

  void Renderer_DrawVertices(
    Vector<Vertex> const& vertices,
    Vector<ushort> const& indices)
  {
    StaticDrawVertices(
      vertices.data(),
      vertices.size(),
      indices.data(),
      IndexFormat::Short,
      indices.size());
  }

  void Renderer_DrawVertices(
    void const* vertexData,
    Type const& vertexFormat,
    void const* indexData,
    uint indices,
    IndexFormat::Enum indexFormat)
  {
    if (!indices)
      return;

    /* Each field of the vertex is an attribute, in the order of its fields,
       as GL's attribute arrays were. */
    static Vector<Neuron::VertexAttribute> attributes;
    attributes.clear();
    uint attribs = vertexFormat->GetFieldCount((void*)vertexData);
    for (uint i = 0; i < attribs; ++i) {
      FieldType field = vertexFormat->GetField((void*)vertexData, i);

      Neuron::VertexFormat format = Neuron::VertexFormat::Float1;
      if (field.type == Type_Get<float>())
        format = Neuron::VertexFormat::Float1;
      else if (field.type == Type_Get<V2>())
        format = Neuron::VertexFormat::Float2;
      else if (field.type == Type_Get<V3>())
        format = Neuron::VertexFormat::Float3;
      else if (field.type == Type_Get<V4>())
        format = Neuron::VertexFormat::Float4;
      else
        error("Vertex format must contain only float, vec2, vec3, or vec4 types");

      size_t const offset = (char const*)field.address - (char const*)vertexData;
      attributes.push(Attribute(i, format, offset));
    }

    Neuron::DrawContext& context = Renderer_Context();
    if (!PrepareDraw(context))
      return;

    /* GL read the vertices in client memory that the indices named; the
       upload takes them up to the largest. */
    Neuron::IndexFormat format;
    uint largest;
    std::span<std::byte const> indexBytes = Indices(indexData, indices, indexFormat, format, largest);
    size_t const vertexBytes = (size_t)(largest + 1) * vertexFormat->size;

    Neuron::VertexLayout layout;
    layout.attributes = std::span<Neuron::VertexAttribute const>(attributes.data(), attributes.size());
    layout.strideBytes = (uint32_t)vertexFormat->size;
    context.DrawTransient(
      std::span<std::byte const>((std::byte const*)vertexData, vertexBytes),
      layout, indexBytes, format);

    renderer.callCount++;
    renderer.polyCount += (int)(indices / 3);
  }

  void Renderer_Flush() {
    Renderer_Context().Flush();
  }

  int Renderer_GetDrawCallCount() {
    return renderer.callCountLast;
  }

  int Renderer_GetPolyCount() {
    return renderer.polyCountLast;
  }

  void Renderer_ResetCounters() {
    renderer.callCountLast = renderer.callCount;
    renderer.polyCountLast = renderer.polyCount;
    renderer.callCount = 0;
    renderer.polyCount = 0;
  }

  void Renderer_SetShader(ShaderT& shader) {
    shader.Use();
    InjectMatrices(shader);
  }

  void Renderer_SetViewport(V2 const& origin, V2 const& size) {
    renderer.glViewport = V4T<int>((int)origin.x, (int)origin.y, (int)size.x, (int)size.y);
    renderer.glViewportSet = true;
  }

  void Renderer_SetWireframe(bool wireframe) {
    renderer.wireframe = wireframe;
  }

  void Renderer_PushBlendMode(BlendMode::Enum mode) {
    renderer.blendMode.push(mode);
  }

  void Renderer_PopBlendMode() {
    renderer.blendMode.pop();
  }

  void Renderer_PushCullMode(CullMode::Enum mode) {
    renderer.cullMode.push(mode);
    renderer.cull = mode;
  }

  void Renderer_PopCullMode() {
    renderer.cullMode.pop();
    renderer.cull = renderer.cullMode.back();
  }

  void Renderer_PopAllBuffers() {
    for (size_t i = 0; i < kMaxColorAttachments; ++i)
      Renderer_PopColorBuffer((uint)i);
    Renderer_PopDepthBuffer();
  }

  void Renderer_PushAllBuffers() {
    for (size_t i = 0; i < kMaxColorAttachments; ++i)
      Renderer_PushColorBuffer((uint)i, 0);
    Renderer_PushDepthBuffer(Texture2D());
  }

  void Renderer_PopColorBuffers() {
    for (size_t i = 0; i < kMaxColorAttachments; ++i)
      Renderer_PopColorBuffer((uint)i);
  }

  void Renderer_PushColorBuffers() {
    for (size_t i = 0; i < kMaxColorAttachments; ++i)
      Renderer_PushColorBuffer((uint)i, 0);
  }

  void Renderer_PushColorBuffer(uint index, uint64 id, uint layer) {
    LTE_ASSERT(index < kMaxColorAttachments);
    renderer.colorAttachment[index].push(Attachment(id, layer));
  }

  void Renderer_PopColorBuffer(uint index) {
    LTE_ASSERT(index < kMaxColorAttachments);
    renderer.colorAttachment[index].pop();
  }

  void Renderer_PushDepthBuffer(Texture2D const& texture) {
    GpuTexture* gpu = texture ? Texture2D_GetGpu(*texture) : nullptr;
    renderer.depthAttachment.push(gpu ? gpu->id : 0);
  }

  void Renderer_PopDepthBuffer() {
    renderer.depthAttachment.pop();
  }

  void Renderer_PushScissorOff() {
    renderer.scissor.push(Tuple(false, V2(0), V2(0)));
  }

  void Renderer_PushScissorOn(V2 const& origin, V2 const& size) {
    renderer.scissor.push(Tuple(true, origin, size));
  }

  void Renderer_PopScissor() {
    renderer.scissor.pop();
  }

  void Renderer_PushViewport(int x, int y, int w, int h) {
    renderer.viewport.push(V4T<int>(x, y, w, h));
    renderer.glViewport = renderer.viewport.back();
    renderer.glViewportSet = true;
  }

  void Renderer_PopViewport() {
    renderer.viewport.pop();
    if (renderer.viewport.size())
      renderer.glViewport = renderer.viewport.back();
  }

  void Renderer_PushZBuffer(bool useZBuffer) {
    renderer.zBuffer.push(useZBuffer);
  }

  void Renderer_PopZBuffer() {
    renderer.zBuffer.pop();
  }

  void Renderer_PopZWritable() {
    renderer.zWritable.pop();
  }

  void Renderer_PushZWritable(bool zWritable) {
    renderer.zWritable.push(zWritable);
  }

  void Renderer_SetWorldTransform(Transform const& world) {
    Transform newTransform = world;
    if (kCameraSpaceRendering)
      newTransform.pos -= renderer.viewTransform.pos;
    renderer.world = newTransform.GetMatrix();
    renderer.worldIT = renderer.world.Inverse().Transpose();
    renderer.wvp = renderer.proj * (renderer.view * renderer.world);
  }

  void Renderer_SetViewTransform(Transform const& view) {
    Transform newTransform = view;
    if (kCameraSpaceRendering)
      newTransform.pos = 0;
    renderer.viewTransform = view;
    renderer.view = newTransform.GetMatrix().Inverse();
    renderer.wvp = renderer.proj * (renderer.view * renderer.world);
  }

  void Renderer_SetProjMatrix(Matrix const& proj) {
    renderer.proj = proj;
    renderer.wvp = renderer.proj * (renderer.view * renderer.world);
  }

  Matrix const& Renderer_GetWorldMatrix() {
    return renderer.world;
  }

  Matrix const& Renderer_GetWorldITMatrix() {
    return renderer.worldIT;
  }

  Matrix const& Renderer_GetWorldViewProjMatrix() {
    return renderer.wvp;
  }

  Matrix const& Renderer_GetViewMatrix() {
    return renderer.view;
  }

  Matrix const& Renderer_GetProjMatrix() {
    return renderer.proj;
  }
}
