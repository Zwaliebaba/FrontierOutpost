#ifndef LTE_GraphicsEnum_h__
#define LTE_GraphicsEnum_h__

#include <cstddef>

/* What the engine asks of the GPU, in its own names rather than a graphics
 * API's (Design/Plan/NeuronClient-migration.md, Phase 4 step 1). Only what the
 * engine uses: the seven colour formats of the plan's section 5.5, and depth. */
namespace LTE {
  namespace TextureFormat {
    enum Enum {
      R8,
      RG8,
      RGBA8,
      R16F,
      RGBA16F,
      R32F,
      RGBA32F,
      Depth32F
    };

    /* Bytes per texel. */
    inline size_t Size(Enum format) {
      switch (format) {
      case R8:       return 1;
      case RG8:      return 2;
      case RGBA8:    return 4;
      case R16F:     return 2;
      case RGBA16F:  return 8;
      case R32F:     return 4;
      case RGBA32F:  return 16;
      case Depth32F: return 4;
      }
      return 0;
    }
  }

  /* The channels of data given to a texture, or taken from one. */
  namespace PixelFormat {
    enum Enum {
      Red,
      RG,
      RGB,
      RGBA
    };
  }

  /* The type of each channel of that data. */
  namespace DataFormat {
    enum Enum {
      UnsignedByte,
      Half,
      Float
    };
  }

  namespace TextureFilter {
    enum Enum {
      Linear,
      Nearest
    };
  }

  namespace TextureFilterMip {
    enum Enum {
      Linear,
      LinearMipLinear,
      Nearest
    };
  }

  namespace TextureWrapMode {
    enum Enum {
      ClampToBorder,
      ClampToEdge,
      Repeat
    };
  }

  namespace IndexFormat {
    enum Enum {
      Byte,
      Short,
      Int
    };
  }
}

#endif
