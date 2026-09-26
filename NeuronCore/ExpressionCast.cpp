#include "Expressions.h"

#include "Environment.h"
#include "Script.h"
#include "StringList.h"

#include "Debug.h"

namespace LTE {
  Expression Expression_Cast(
    StringList const& list,
    CompileEnvironment& env)
  {
    if (list->GetSize() != 3)
      return nullptr;

    Type type = env.script->ResolveType(list->Get(1));
    if (!type)
      return nullptr;

    Expression e = Expression_Compile(list->Get(2), env);
    if (!e)
      return nullptr;

    return Expression_Conversion(e, type);
  }
}
