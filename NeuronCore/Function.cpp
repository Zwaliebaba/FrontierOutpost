#include "Function.h"
#include "Data.h"
#include "Map.h"
#include "Pointer.h"
#include "LteString.h"
#include "Vector.h"

#include <functional>
#include <string_view>
#include <unordered_map>

namespace {
  /* Hashes any string-like key, so a lookup never builds a String. */
  struct NameHash {
    typedef void is_transparent;

    size_t operator()(std::string_view name) const noexcept {
      return std::hash<std::string_view>()(name);
    }
  };

  /* Nothing iterates the name table, so its order is free. */
  typedef std::unordered_map<String, Vector<Function>, NameHash, std::equal_to<> > FunctionMapT;

  Vector<Function>& GetFunctionList() {
    static Vector<Function> v;
    return v;
  }

  FunctionMapT& GetFunctionMap() {
    static FunctionMapT m;
    return m;
  }
}

Function Function_Create(String const& name) {
  Function self = new FunctionT;

  self->name = name;
  self->call = 0;
  self->paramCount = 0;
  self->params = 0;
  
  GetFunctionList().push(self);
  GetFunctionMap()[name].push(self);

  return self.t;
}

FunctionT::~FunctionT() {
  delete[] params;
}

String FunctionT::GetSignature() const {
  Stringize s;
  s | returnType->GetAliasName() | " " | name | "(";
  for (uint i = 0; i < paramCount; ++i) {
    if (i) s | ", ";
    s | params[i].type->GetAliasName() | " " | params[i].name;
  }
  s | ")";
  return s;
}

void Function_AddAlias(String const& source, String const& alias) {
  GetFunctionMap()[alias].append(GetFunctionMap()[source]);
}

Vector<Function> const& Function_Find(String const& name) {
  static Vector<Function> const kNone;
  FunctionMapT::const_iterator found = GetFunctionMap().find(name);
  return found != GetFunctionMap().end() ? found->second : kNone;
}

Vector<Function> const& Function_GetList() {
  return GetFunctionList();
}
