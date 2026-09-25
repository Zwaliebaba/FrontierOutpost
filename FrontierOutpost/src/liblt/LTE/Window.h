#ifndef LTE_Window_h__
#define LTE_Window_h__

#include "Reference.h"
#include "V2.h"

struct WindowT : public RefCounted {
  virtual ~WindowT() {}

  float GetAspect() const {
    V2U size = GetSize();
    return (float)size.x / (float)size.y;
  }

  virtual void Close() = 0;
  virtual void Display() = 0;

  virtual void* GetImplData() = 0;
  virtual V2U GetSize() const = 0;
  virtual bool HasFocus() const = 0;
  virtual bool IsOpen() const = 0;

  virtual void SetSync(bool sync) = 0;

  virtual void Update() = 0;
};

LT_API Window Window_Create(
  String const& title,
  V2U const& size,
  bool border,
  bool fullscreen);

LT_API Window Window_Get();
LT_API void Window_Pop();
LT_API void Window_Push(Window const& window);

#endif
