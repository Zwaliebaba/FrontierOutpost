#ifndef LTE_LocationT_h__
#define LTE_LocationT_h__

#include "BaseType.h"
#include "DeclareFunction.h"
#include "Reference.h"
#include "LteString.h"

namespace LTE {
  /* A file's size and last write time; valid only when both could be read. */
  struct FileStamp {
    uint64 size;
    int64 time;
    bool valid;

    FileStamp() :
      size(0),
      time(0),
      valid(false)
      {}

    friend bool operator==(FileStamp const& a, FileStamp const& b) {
      return a.valid == b.valid && a.size == b.size && a.time == b.time;
    }
  };

  struct LocationT : public RefCounted {
    BASE_TYPE(LocationT)

    LT_API HashT GetHash() const;
    LT_API String ReadAscii() const;

    virtual Location Clone() const = 0;
    virtual bool Exists() const = 0;
    virtual AutoPtr< Array<uchar> > Read() const = 0;
    virtual bool Write(Array<uchar> const& data) const = 0;

    /* The size and last write time, without opening the file; invalid when unknown. */
    virtual FileStamp GetStamp() const {
      return FileStamp();
    }

    FIELDS {}
  };

  DeclareFunction(Location_Cache, Location,
    String, name)

  DeclareFunction(Location_File, Location,
    String, file)

  LT_API Location Location_Memory(String const& str);

  LT_API Location Location_Memory(Array<uchar>* memory, bool ownsMemory = false);

  DeclareFunction(Location_Resource, Location,
    String, name)

  inline Location Location_Font(String const& name) {
    return Location_Resource("font/" + name);
  }

  inline Location Location_GameData(String const& name) {
    return Location_Resource("gamedata/" + name);
  }

  inline Location Location_Script(String const& name) {
    return Location_Resource("script/" + name);
  }

  inline Location Location_Texture(String const& name) {
    return Location_Resource("texture/" + name);
  }
}

#endif
