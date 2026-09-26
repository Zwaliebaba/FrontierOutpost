#ifndef Module_SettingsEntry_h__
#define Module_SettingsEntry_h__

#include "BaseType.h"
#include "LteString.h"
#include "UiCommon.h"
#include "Widget.h"

struct SettingsEntry {
  BASE_TYPE(SettingsEntry)

  virtual void GetValue(void* buffer) = 0;

  FIELDS {}
};

LT_API SettingsEntry* SettingsEntry_Bool(String const& name, bool defValue);

LT_API SettingsEntry* SettingsEntry_Color(String const& name, Color const& defValue);

LT_API SettingsEntry* SettingsEntry_Float(
  String const& name,
  float defValue,
  float minimum,
  float maximum);

#endif
