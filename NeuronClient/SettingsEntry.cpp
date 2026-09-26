#include "SettingsEntry.h"

#include "Color.h"
#include "Generic.h"

namespace {
  AutoClassDerived(SettingsBool, SettingsEntry,
    String, name,
    bool, value)
    GenericBool function;

    DERIVED_TYPE_EX(SettingsBool)

    SettingsBool() {}

    void GetValue(void* buffer) {
      if (!function)
        function = ConstantPointer(&value);
      *((GenericBool*)buffer) = function;
    }
  };

  DERIVED_IMPLEMENT(SettingsBool)

  AutoClassDerived(SettingsColor, SettingsEntry,
    String, name,
    Color, value)
    GenericColor function;

    DERIVED_TYPE_EX(SettingsColor)

    SettingsColor() {}

    void GetValue(void* buffer) {
      if (!function)
        function = ConstantPointer(&value);
      *((GenericColor*)buffer) = function;
    }
  };

  DERIVED_IMPLEMENT(SettingsColor)

  AutoClassDerived(SettingsFloat, SettingsEntry,
    String, name,
    float, value,
    float, minimum,
    float, maximum)
    GenericFloat function;

    DERIVED_TYPE_EX(SettingsFloat)

    SettingsFloat() {}

    void GetValue(void* buffer) {
      if (!function)
        function = ConstantPointer(&value);
      *((GenericFloat*)buffer) = function;
    }
  };

  DERIVED_IMPLEMENT(SettingsFloat)
}

SettingsEntry* SettingsEntry_Bool(String const& name, bool defValue) {
  return new SettingsBool(name, defValue);
}

SettingsEntry* SettingsEntry_Color(String const& name, Color const& defValue) {
  return new SettingsColor(name, defValue);
}

SettingsEntry* SettingsEntry_Float(
  String const& name,
  float defValue,
  float minimum,
  float maximum)
{
  return new SettingsFloat(name, defValue, minimum, maximum);
}
