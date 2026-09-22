#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <string.h>
#include "hid.h"

void SystemDebug(int level, const char* format, ...)
{
    (void)level;
    (void)format;
}

static HidDevice_t create_device(void)
{
    HidDevice_t device;
    memset(&device, 0, sizeof(device));
    return device;
}

static void destroy_collection(HidDevice_t* device)
{
    if (device->Collection != NULL) {
        HidCollectionCleanup(device);
        device->Collection = NULL;
    }
}

static void test_mouse_descriptor(void** state)
{
    HidDevice_t device = create_device();
    const uint8_t descriptor[] = {
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
        0x09, 0x01, 0xA1, 0x00, 0x05, 0x09,
        0x19, 0x01, 0x29, 0x03, 0x15, 0x00,
        0x25, 0x01, 0x95, 0x03, 0x75, 0x01,
        0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
        0x81, 0x01, 0x05, 0x01, 0x09, 0x30,
        0x09, 0x31, 0x15, 0x81, 0x25, 0x7F,
        0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
        0xC0, 0xC0
    };
    (void)state;

    assert_int_equal(HidParseReportDescriptor(&device, descriptor, sizeof(descriptor)), 3);
    assert_non_null(device.Collection);
    destroy_collection(&device);
}

static void test_truncated_descriptor_is_rejected(void** state)
{
    HidDevice_t device = create_device();
    const uint8_t descriptor[] = {
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
        0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0xC0,
        0x09
    };
    (void)state;

    assert_int_equal(HidParseReportDescriptor(&device, descriptor, sizeof(descriptor)), 0);
    assert_null(device.Collection);
    destroy_collection(&device);
}

static void test_long_item_does_not_desynchronize_parser(void** state)
{
    HidDevice_t device = create_device();
    const uint8_t descriptor[] = {
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
        0xFE, 0x02, 0x99, 0xAA, 0xBB,
        0x09, 0x30, 0x15, 0x00, 0x25, 0x7F,
        0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xC0
    };
    (void)state;

    assert_int_equal(HidParseReportDescriptor(&device, descriptor, sizeof(descriptor)), 1);
    assert_non_null(device.Collection);
    destroy_collection(&device);
}

static void test_report_id_offsets_are_independent(void** state)
{
    HidDevice_t device = create_device();
    const uint8_t descriptor[] = {
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
        0x85, 0x01, 0x09, 0x30, 0x15, 0x00, 0x25, 0x7F,
        0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
        0x85, 0x02, 0x09, 0x31, 0x75, 0x08, 0x95, 0x01,
        0x81, 0x02, 0xC0
    };
    (void)state;

    assert_int_equal(HidParseReportDescriptor(&device, descriptor, sizeof(descriptor)), 2);
    assert_non_null(device.Collection);
    destroy_collection(&device);
}

static void test_oversized_report_is_bounded(void** state)
{
    HidDevice_t device = create_device();
    const uint8_t descriptor[] = {
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
        0x75, 0x20, 0x95, 0xFF, 0x81, 0x02, 0xC0
    };
    (void)state;

    assert_true(HidParseReportDescriptor(&device, descriptor, sizeof(descriptor)) <= 0x400);
    destroy_collection(&device);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mouse_descriptor),
        cmocka_unit_test(test_truncated_descriptor_is_rejected),
        cmocka_unit_test(test_long_item_does_not_desynchronize_parser),
        cmocka_unit_test(test_report_id_offsets_are_independent),
        cmocka_unit_test(test_oversized_report_is_bounded)
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
