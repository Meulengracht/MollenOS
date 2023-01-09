/**
 * Copyright 2022, Philip Meulengracht
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
 *
 */

#include <ds/mstring.h>
#include <ddk/utils.h>
#include <vfs/vfs.h>
#include "private.h"
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

int TestSetup(void** state)
{
    (void)state;
    return 0;
}

int TestTeardown(void** state)
{
    (void)state;
    return 0;
}

// Build the following filesystem
// /boot
// /boot/bootloader.bin
// /system
// /system/myfolder
// /system/myfolder/myuser/
// /system/myfolder/myuser/myfile.txt
// /system/myfolder/myuser/myfile2.txt
static mstring_t g_boot_root = mstr_const("/boot");
static mstring_t g_boot_bootloader_bin = mstr_const("/boot/bootloader.bin");
static mstring_t g_system_root = mstr_const("/system");
static mstring_t g_system_myfolder = mstr_const("/system/myfolder");
static mstring_t g_system_myfolder_myuser = mstr_const("/system/myfolder/myuser");
static mstring_t g_system_myfolder_myuser_myfile = mstr_const("/system/myfolder/myuser/myfile.txt");
static mstring_t g_system_myfolder_myuser_myfile2 = mstr_const("/system/myfolder/myuser/myfile2.txt");

static struct VFS* __MockVirtualFileSystem(void)
{
    struct VFS* vfs  = NULL;
    guid_t      guid = GUID_EMPTY;
    oserr_t     err;

    err = VFSNew(UUID_INVALID, &guid, NULL, NULL, &vfs);
    assert_int_equal(err, OS_EOK);
    assert_non_null(vfs);

    struct VFSNode* bootDirectory = NULL;
    err = VFSNodeNewDirectory(vfs, &g_boot_root, 0, &bootDirectory);
    assert_int_equal(err, OS_EOK);
    assert_non_null(bootDirectory);
}

void Testmstr_path_new_u8(void** state)
{
    (void)state;

    mstring_t* path = mstr_path_new_u8("///");
    assert_string_equal(mstr_u8(path), "/");
    mstr_delete(path);

    path = mstr_path_new_u8("\\path/\\/path/what/the/test");
    assert_string_equal(mstr_u8(path), "/path/path/what/the/test");
    mstr_delete(path);

    path = mstr_path_new_u8("");
    assert_string_equal(mstr_u8(path), "");
    mstr_delete(path);
}

void TestVFSNodeEnsureLoaded(void** state)
{
    (void)state;

}

int main(void)
{
    const struct CMUnitTest tests [] = {
        cmocka_unit_test(Testmstr_path_new_u8),
    };
    return cmocka_run_group_tests(tests, TestSetup, TestTeardown);
}

// mocks
oserr_t OSHandleCreate(uuid_t* handleOut) { }
void usched_mtx_init(struct usched_mtx* mutex, int type) { }
void usched_mtx_lock(struct usched_mtx* mtx) { }
void usched_mtx_unlock(struct usched_mtx* mtx) { }
void usched_cnd_init(struct usched_cnd* condition) { }
void usched_cnd_notify_one(struct usched_cnd* cnd) { }
void usched_cnd_wait(struct usched_cnd* condition, struct usched_mtx* mutex) { }
