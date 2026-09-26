#ifndef Module_Settings_h__
#define Module_Settings_h__

#include "LteCommon.h"
#include "DeclareFunction.h"
#include "Generic.h"
#include "LteString.h"
#include "UiCommon.h"
#include "Widget.h"

LT_API GenericBool Settings_Bool(
  String const& name,
  bool defValue);

LT_API GenericColor Settings_Color(
  String const& name,
  Color const& defValue);

LT_API GenericFloat Settings_Float(
  String const& name,
  float minimum,
  float maximum,
  float defValue);

LT_API GenericColor Settings_PrimaryColor();

LT_API GenericColor Settings_SecondaryColor();

#endif
