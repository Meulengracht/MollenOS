/**
 * Copyright 2023, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TESTBASE_H__
#define __TESTBASE_H__

#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <cmocka.h>

#define DEFINE_TEST_CONTEXT(_body) static struct __TestContext _body g_testContext

#define MOCK_STRUCT_INPUT(_type, _member) \
    _type Expected ## _member;            \
    bool  Check ## _member
#define MOCK_STRUCT_OUTPUT(_type, _member) \
    _type _member;                         \
    bool  _member ## Provided
#define MOCK_STRUCT_RETURN(_returnType) \
    _returnType ReturnValue;            \
    int         Calls

#define MOCK_STRUCT_FUNC(_func) struct __ ## _func _func

#endif //!__TESTBASE_H__
