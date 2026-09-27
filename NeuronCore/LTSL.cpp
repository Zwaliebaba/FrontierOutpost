#include "LTSL.h"
#include "StringList.h"

namespace {
  void RewriteDot(StringList& list) {
    String const& value = list->GetValue();
    if ( value.contains('.') &&
        !value.contains('"') &&
        !String_IsNumeric(value))
    {
      Vector<String> parts;
      String_Split(parts, value, '.');
      StringList newList = new StringListAtom(parts[0]);
      for (size_t i = 1; i < parts.size(); ++i)
        newList = new StringListList(Vector<StringList>(
          new StringListAtom(parts[i]),
          newList));

      list = newList;
    }
  }

  /* True when the element is an atom spelled like one of the 18 operators of RewriteList. */
  bool IsOperatorAtom(StringList const& list) {
    if (!list->IsAtom())
      return false;
    String const& v = list->GetValue();
    if (v.size() == 1) {
      char c = v[0];
      return c == '^' || c == '*' || c == '/' || c == '+' || c == '-' ||
             c == '<' || c == '>' || c == '=';
    }
    if (v.size() == 2) {
      char a = v[0];
      char b = v[1];
      if (b == '=')
        return a == '<' || a == '>' || a == '=' || a == '!' ||
               a == '+' || a == '-' || a == '*' || a == '/';
      return (a == '&' && b == '&') || (a == '|' && b == '|');
    }
    return false;
  }

  bool IsBinaryOp(StringList const& list, Vector<String> const& ops) {
    String const& value = list->GetValue();
    for (size_t i = 0; i < ops.size(); ++i)
      if (value == ops[i])
        return true;
    return false;
  }

  void RewriteBinaryOp(StringList& list, Vector<String> const& ops) {
    StringListList* l = (StringListList*)list.t;
    for (int i = 0; i + 2 < (int)l->elements.size(); ++i) {
      if (IsBinaryOp(l->elements[i + 1], ops)) {
        l->elements[i] = new StringListList(Vector<StringList>(
          l->elements[i + 1],
          l->elements[i],
          l->elements[i + 2]));
        l->elements.eraseIndex(i + 1);
        l->elements.eraseIndex(i + 1);
        i--;
      }
    }
  }

  void RewriteAtom(StringList& list) {
    RewriteDot(list);
  }

  void RewriteList(StringList& list) {
    static Vector<String> precedence[] = {
      Vector<String>() << "^",
      Vector<String>() << "*" << "/",
      Vector<String>() << "+" << "-",
      Vector<String>() << "<" << ">" << "<=" << ">=",
      Vector<String>() << "==" << "!=",
      Vector<String>() << "&&",
      Vector<String>() << "||",
      Vector<String>() << "=" << "+=" << "-=" << "*=" << "/="
    };

    /* An operator is only rewritten at positions 1 to n - 2, so without one there no pass
       changes anything. */
    StringListList* l = (StringListList*)list.t;
    bool any = false;
    for (size_t e = 1; e + 1 < l->elements.size() && !any; ++e)
      any = IsOperatorAtom(l->elements[e]);
    if (!any)
      return;

    for (uint i = 0; i < sizeof(precedence) / sizeof(*precedence); ++i)
      RewriteBinaryOp(list, precedence[i]);
  }

  void Rewrite(StringList& list) {
    StringListList* l = (StringListList*)list.t;
    for (size_t i = 0; i < l->elements.size(); ++i) {
      if (l->elements[i]->IsAtom())
        RewriteAtom(l->elements[i]);
      else
        Rewrite(l->elements[i]);
    }
    RewriteList(list);
  }
}

StringList LTSL_ApplyRewrites(StringList const& list) {
  StringList newList = list;
  Rewrite(newList);
  return newList;
}
