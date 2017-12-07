//===----------------------------- typeinfo.cpp ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <typeinfo>

namespace std
{

// type_info

_CRTIMP type_info::~type_info()
{
}

// bad_cast

_CRTIMP bad_cast::bad_cast() _NOEXCEPT
{
}

_CRTIMP bad_cast::~bad_cast() _NOEXCEPT
{
}

_CRTIMP const char*
bad_cast::what() const _NOEXCEPT
{
  return "std::bad_cast";
}

// bad_typeid

_CRTIMP bad_typeid::bad_typeid() _NOEXCEPT
{
}

_CRTIMP bad_typeid::~bad_typeid() _NOEXCEPT
{
}

_CRTIMP const char*
bad_typeid::what() const _NOEXCEPT
{
  return "std::bad_typeid";
}

}  // std
