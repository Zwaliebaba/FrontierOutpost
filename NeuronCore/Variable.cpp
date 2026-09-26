#include "Expressions.h"

#include "AutoClass.h"
#include "Environment.h"
#include "Pool.h"
#include "ProgramLog.h"
#include "StringList.h"

namespace {
  AutoClassDerived(ExpressionVariable, ExpressionT,
    uint, index,
    uint, offset,
    Type, type,
    String, name)
    DERIVED_TYPE_EX(ExpressionVariable)
    POOLED_TYPE

    ExpressionVariable() {}

    String Emit(Vector<String>& context) const {
      return name;
    }

    void Evaluate(void* returnValue, Environment& env) const {
      type->Assign((char*)env.registers[env.base + index] + offset, returnValue);
    }

    void* GetLValue(Environment& env) const {
      return (char*)env.registers[env.base + index] + offset;
    }

    Type GetType() const {
      return type;
    }

    bool IsConstant(CompileEnvironment& env) const {
      return env.Get(name).constant;
    }

    bool IsLValue() const {
      return true;
    }
  };
}

namespace LTE {
  Expression Expression_Variable(
    uint index,
    Type const& type,
    String const& name)
  {
    return new ExpressionVariable(index, 0, type, name);
  }

  Expression Expression_Variable(
    StringList const& list,
    CompileEnvironment& env)
  {
    String const& name = list->GetValue();
    if (env.Contains(name)) {
      Variable const& var = env.Get(name);
      if (var.reference)
        return nullptr;

      return var.offset < 0
        ? Expression_Variable(var.registerIndex, var.type, var.name)
        : new ExpressionVariable(var.registerIndex, var.offset, var.type, var.name);
    }

    if (env.detail)
      Log_Message(Stringize() | "variable -- variable name '"
        | name | "' not found");
    return nullptr;
  }
}
