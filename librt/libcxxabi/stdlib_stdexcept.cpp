//===------------------------ stdexcept.cpp -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "include/refstring.h"
#include "stdexcept"
#include "new"
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstddef>

static_assert(sizeof(std::__libcpp_refstring) == sizeof(const char *), "");

namespace std  // purposefully not using versioning namespace
{

_CRTIMP logic_error::~logic_error() _NOEXCEPT {}

_CRTIMP const char*
logic_error::what() const _NOEXCEPT
{
    return __imp_.c_str();
}

_CRTIMP runtime_error::~runtime_error() _NOEXCEPT {}

_CRTIMP const char*
runtime_error::what() const _NOEXCEPT
{
    return __imp_.c_str();
}

_CRTIMP domain_error::~domain_error() _NOEXCEPT {}
_CRTIMP invalid_argument::~invalid_argument() _NOEXCEPT {}
_CRTIMP length_error::~length_error() _NOEXCEPT {}
_CRTIMP out_of_range::~out_of_range() _NOEXCEPT {}

_CRTIMP range_error::~range_error() _NOEXCEPT {}
_CRTIMP overflow_error::~overflow_error() _NOEXCEPT {}
_CRTIMP underflow_error::~underflow_error() _NOEXCEPT {}

}  // std
