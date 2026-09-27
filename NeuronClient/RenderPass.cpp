#include "RenderPass.h"

#include "Profiler.h"
#include "Renderer.h"
#include "RendererCore.h"
#include "StackFrame.h"

#include "ModuleSettings.h"

#include "DrawContext.h"

/* Each pass is a region in PIX, named as the profiler names it
   (Design/ADR/ADR-018). */
void RenderPassT::Render(DrawState* state) {
  if (Settings_Bool((String)"Graphics/" + ToString(), true)()) {
    FRAME(GetName()) {
      Neuron::DrawContext& context = Renderer_Context();
      context.BeginEvent(GetName());
      OnRender(state);
      Profiler_Flush();
      context.EndEvent();
    }
  }
}
