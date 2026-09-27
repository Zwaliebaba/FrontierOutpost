#ifndef LTE_Function_Pointer_h__
#define LTE_Function_Pointer_h__

#include "Wrapper.h"

namespace LTE {
  template <class T>
  struct ConstantPointerT {
    typedef T ReturnType;

    T const* t;

    ConstantPointerT(T const* t) :
      t(t)
      {}

    T operator()() {
      return *t;
    }
  };

  template <class T>
  WrapperT<ConstantPointerT<T>, typename ConstantPointerT<T>::ReturnType>
  ConstantPointer(T const* t) {
    return Wrapper(ConstantPointerT<T>(t));
  }
}

#endif
