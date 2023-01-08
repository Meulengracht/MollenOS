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
 * Device Manager - Bootstrapper
 * Provides system bootstrap functionality, parses ramdisk for additional system
 * drivers and loads them if any matching device is present.
 */

//#define __TRACE

#include <errno.h>
#include <discover.h>
#include <ddk/utils.h>
#include <ds/list.h>
#include <yaml/yaml.h>

/**
 * driver configuration values
 * driver:
 *   type:
 *   - class: 0
 *   - subclass: 0
 *
 *   vendors:
 *   - 0x8086:
 *      - productid0
 *      - productid1
 *
 *   resources:
 *   - type: io
 *     base: 0x70
 *     length: 0x71
 *   - type: mmio
 *     base: 0xE0000000
 *     length: 0x1000
 *
 * stream-start-event (1)
 *  document-start-event (3)
 *    mapping-start-event (9)
 *      scalar-event (6) = {value="driver", length=6}
 *      mapping-start-event (9)
 *        scalar-event (6) = {value="type", length=4}
 *        sequence-start-event (7)
 *          mapping-start-event (9)
 *            scalar-event (6) = {value="class", length=5}
 *            scalar-event (6) = {value="0", length=1}
 *          mapping-end-event (10)
 *          mapping-start-event (9)
 *            scalar-event (6) = {value="subclass", length=8}
 *            scalar-event (6) = {value="0", length=1}
 *          mapping-end-event (10)
 *        sequence-end-event (8)
 *        scalar-event (6) = {value="vendors", length=7}
 *        sequence-start-event (7)
 *          mapping-start-event (9)
 *            scalar-event (6) = {value="0x8086", length=6}
 *            sequence-start-event (7)
 *              scalar-event (6) = {value="productid0", length=10}
 *              scalar-event (6) = {value="productid1", length=10}
 *            sequence-end-event (8)
 *          mapping-end-event (10)
 *        sequence-end-event (8)
 *        scalar-event (6) = {value="resources", length=9}
 *        sequence-start-event (7)
 *          mapping-start-event (9)
 *            scalar-event (6) = {value="type", length=4}
 *            scalar-event (6) = {value="io", length=2}
 *            scalar-event (6) = {value="base", length=4}
 *            scalar-event (6) = {value="0x70", length=4}
 *            scalar-event (6) = {value="length", length=6}
 *            scalar-event (6) = {value="0x71", length=4}
 *          mapping-end-event (10)
 *          mapping-start-event (9)
 *            scalar-event (6) = {value="type", length=4}
 *            scalar-event (6) = {value="mmio", length=4}
 *            scalar-event (6) = {value="base", length=4}
 *            scalar-event (6) = {value="0xE0000000", length=10}
 *            scalar-event (6) = {value="length", length=6}
 *            scalar-event (6) = {value="0x1000", length=6}
 *          mapping-end-event (10)
 *        sequence-end-event (8)
 *      mapping-end-event (10)
 *    mapping-end-event (10)
 *  document-end-event (4)
 * stream-end-event (2)
 */
enum state {
    STATE_START,    /* start state */
    STATE_STREAM,   /* start/end stream */
    STATE_DOCUMENT, /* start/end document */
    STATE_SECTION,  /* top level */

    STATE_DRIVER,          // 4, driver object discovered
    STATE_DRIVERKEY,       // driver key discovered, followed by scalar
    STATE_DRIVERTYPE,      // driver type mapping
    STATE_DRIVERVENDORS,   // driver vendors mapping
    STATE_DRIVERRESOURCES, // 8, driver resource mappings

    STATE_TYPECLASS,       // inside driver.type.class
    STATE_TYPESUBCLASS,    // inside driver.type.subclass

    STATE_VENDOR,          // 11, found key/value pair inside driver.vendors
    STATE_VENDORPRODUCT,   // 12, vendor product list

    STATE_RESOURCE,       // new resource entry found
    STATE_RESOURCETYPE,   // resource.type found
    STATE_RESOURCEBASE,   // resource.base found
    STATE_RESOURCELENGTH, // resource.length found

    STATE_STOP
};

struct parser_state {
    enum state                 state;
    struct DriverConfiguration driver;
    struct DriverVendor*       vendor;
    struct DriverResource*     resource;
};

static oserr_t
__AddProduct(
        _In_ struct DriverVendor* vendor,
        _In_ uint32_t             productId)
{
    struct DriverProduct* product;

    product = malloc(sizeof(struct DriverProduct));
    if (!product) {
        ERROR("__AddProduct out of memory for product!");
        return OS_EOOM;
    }

    ELEMENT_INIT(&product->ListHeader, 0, product);
    product->Id = productId;
    list_append(&vendor->Products, &product->ListHeader);
    return OS_EOK;
}

static int
__ParseBoolean(
        _In_  const char* string,
        _Out_ int*        value)
{
    char*  t[] = {"y", "Y", "yes", "Yes", "YES", "true", "True", "TRUE", "on", "On", "ON", NULL};
    char*  f[] = {"n", "N", "no", "No", "NO", "false", "False", "FALSE", "off", "Off", "OFF", NULL};
    char** p;

    for (p = t; *p; p++) {
        if (strcmp(string, *p) == 0) {
            *value = 1;
            return 0;
        }
    }
    for (p = f; *p; p++) {
        if (strcmp(string, *p) == 0) {
            *value = 0;
            return 0;
        }
    }
    return EINVAL;
}

static int
__ConsumeEvent(
        _In_ struct parser_state* s,
        _In_ yaml_event_t*        event)
{
    char *value;
    TRACE("__ConsumeEvent(state=%d event=%d)", s->state, event->type);

    switch (s->state) {
        case STATE_START:
            switch (event->type) {
                case YAML_STREAM_START_EVENT:
                    s->state = STATE_STREAM;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_STREAM:
            switch (event->type) {
                case YAML_DOCUMENT_START_EVENT:
                    s->state = STATE_DOCUMENT;
                    break;
                case YAML_STREAM_END_EVENT:
                    s->state = STATE_STOP;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_DOCUMENT:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    s->state = STATE_SECTION;
                    break;
                case YAML_DOCUMENT_END_EVENT:
                    s->state = STATE_STREAM;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_SECTION:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "driver") == 0) {
                        s->state = STATE_DRIVER;
                    } else {
                        ERROR("__ConsumeEvent Unexpected scalar: %s, expected 'driver'", value);
                        return -1;
                    } break;
                case YAML_DOCUMENT_END_EVENT:
                    s->state = STATE_STREAM;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_DRIVER:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    s->state = STATE_DRIVERKEY;
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_SECTION;
                    break;
                default:
                    ERROR("Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_DRIVERKEY:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    TRACE("__ConsumeEvent STATE_DRIVERKEY/%s", value);
                    if (strcmp(value, "type") == 0) {
                        s->state = STATE_DRIVERTYPE;
                    } else if (strcmp(value, "vendors") == 0) {
                        s->state = STATE_DRIVERVENDORS;
                    } else if (strcmp(value, "resources") == 0) {
                        s->state = STATE_DRIVERRESOURCES;
                    } else {
                        ERROR("__ConsumeEvent Unexpected key: %s", value);
                        return -1;
                    } break;
                case YAML_MAPPING_END_EVENT:
                    // end of driver structure
                    s->state = STATE_DRIVER;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        // Handle events when inside driver.type
        case STATE_DRIVERTYPE:
            switch (event->type) {
                case YAML_SEQUENCE_START_EVENT:
                case YAML_MAPPING_START_EVENT:
                    // The type will start with a sequence event, then a mapping event
                    break;

                case YAML_SEQUENCE_END_EVENT:
                    // we are done parsing driver.type
                    s->state = STATE_DRIVERKEY;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    TRACE("__ConsumeEvent STATE_DRIVERTYPE/%s", value);
                    if (strcmp(value, "class") == 0) {
                        s->state = STATE_TYPECLASS;
                    } else if (strcmp(value, "subclass") == 0) {
                        s->state = STATE_TYPESUBCLASS;
                    } else {
                        ERROR("__ConsumeEvent Unexpected key: %s", value);
                        return -1;
                    } break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_TYPECLASS:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    TRACE("__ConsumeEvent STATE_TYPECLASS/%s", (const char*)event->data.scalar.value);
                    s->driver.Class = (uint32_t)strtol((const char*)event->data.scalar.value, NULL, 0);
                    break;

                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_DRIVERTYPE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_TYPESUBCLASS:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    TRACE("__ConsumeEvent STATE_TYPESUBCLASS/%s", (const char*)event->data.scalar.value);
                    s->driver.Subclass = (uint32_t)strtol((const char*)event->data.scalar.value, NULL, 0);
                    break;

                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_DRIVERTYPE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_DRIVERVENDORS:
            switch (event->type) {
                case YAML_SEQUENCE_START_EVENT:
                    // list start of vendors
                    break;

                case YAML_SEQUENCE_END_EVENT:
                    // list end
                    s->state = STATE_DRIVERKEY;
                    break;

                case YAML_MAPPING_START_EVENT:
                    // new list entry.
                    s->state = STATE_VENDOR;
                    break;

                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_VENDOR:
            switch (event->type) {
                case YAML_MAPPING_END_EVENT:
                    list_append(&s->driver.Vendors, &s->vendor->ListHeader);
                    s->vendor = NULL;
                    s->state = STATE_DRIVERVENDORS;
                    break;

                case YAML_SEQUENCE_START_EVENT:
                    // start of products
                    s->state = STATE_VENDORPRODUCT;
                    break;

                case YAML_SCALAR_EVENT:
                    // Occurs on a new vendor entry
                    s->vendor = malloc(sizeof(struct DriverVendor));
                    if (!s->vendor) {
                        ERROR("__ConsumeEvent out of memory allocating yaml vendor");
                        return -1;
                    }
                    ELEMENT_INIT(&s->vendor->ListHeader, 0, s->vendor);
                    list_construct(&s->vendor->Products);
                    TRACE("__ConsumeEvent STATE_VENDOR/%s", (const char*)event->data.scalar.value);
                    s->vendor->Id = (uint32_t)strtol((const char*)event->data.scalar.value, NULL, 0);
                    break;

                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_VENDORPRODUCT:
            switch (event->type) {
                case YAML_SEQUENCE_END_EVENT:
                    // end of products, go back to vendor
                    s->state = STATE_VENDOR;
                    break;

                case YAML_SCALAR_EVENT:
                    TRACE("__ConsumeEvent STATE_VENDORPRODUCT/%s", (const char*)event->data.scalar.value);
                    uint32_t productId = (uint32_t)strtol((const char*)event->data.scalar.value, NULL, 0);
                    if (!productId) {
                        WARNING("__ConsumeEvent failed to parse product id: %s",
                                (const char*)event->data.scalar.value);
                    }
                    else {
                        if (__AddProduct(s->vendor, productId) != OS_EOK) {
                            return -1;
                        }
                    }
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_DRIVERRESOURCES:
            switch (event->type) {
                case YAML_SEQUENCE_START_EVENT:
                    // start of list
                    break;

                case YAML_MAPPING_START_EVENT:
                    // Occurs on a new resource entry
                    s->resource = malloc(sizeof(struct DriverResource));
                    if (!s->resource) {
                        ERROR("__ConsumeEvent out of memory allocating yaml resource");
                        return -1;
                    }
                    memset(s->resource, 0, sizeof(struct DriverResource));
                    ELEMENT_INIT(&s->resource->ListHeader, 0, s->resource);
                    s->state = STATE_RESOURCE;
                    break;

                case YAML_SEQUENCE_END_EVENT:
                    // end of resource list
                    s->state = STATE_DRIVERKEY;
                    break;

                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_RESOURCE:
            switch (event->type) {
                case YAML_MAPPING_END_EVENT:
                    // end of resource
                    list_append(&s->driver.Resources, &s->resource->ListHeader);
                    s->resource = NULL;
                    s->state = STATE_DRIVERRESOURCES;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "type") == 0) {
                        s->state = STATE_RESOURCETYPE;
                    } else if (strcmp(value, "base") == 0) {
                        s->state = STATE_RESOURCEBASE;
                    } else if (strcmp(value, "length") == 0) {
                        s->state = STATE_RESOURCELENGTH;
                    } else {
                        ERROR("__ConsumeEvent Unexpected key: %s", value);
                        return -1;
                    } break;

                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.\n", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_RESOURCETYPE:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    s->resource->Type = (int)strtol((const char*)event->data.scalar.value, NULL, 0);
                    s->state = STATE_RESOURCE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_RESOURCEBASE:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    s->resource->Base = (uintptr_t)strtoll((const char*)event->data.scalar.value, NULL, 0);
                    s->state = STATE_RESOURCE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_RESOURCELENGTH:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    s->resource->Length = (size_t)strtoll((const char*)event->data.scalar.value, NULL, 0);
                    s->state = STATE_RESOURCE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_STOP:
            break;
    }
    return 0;
}

static void
__CleanupProduct(
        _In_ element_t* item,
        _In_ void*      context)
{
    _CRT_UNUSED(context);
    free(item);
}

static void
__CleanupVendor(
        _In_ element_t* item,
        _In_ void*      context)
{
    struct DriverVendor* vendor = item->value;
    _CRT_UNUSED(context);
    list_clear(&vendor->Products, __CleanupProduct, NULL);
    free(vendor);
}

static void
__CleanupResource(
        _In_ element_t* item,
        _In_ void*      context)
{
    struct DriverResource* resource = item->value;
    _CRT_UNUSED(context);
    free(resource);
}

static void
__CleanupDriverConfiguration(
        _In_ struct DriverConfiguration* driverConfig)
{
    list_clear(&driverConfig->Vendors, __CleanupVendor, NULL);
    list_clear(&driverConfig->Resources, __CleanupResource, NULL);
}

oserr_t
DmDriverConfigParseYaml(
        _In_  const uint8_t*              yaml,
        _In_  size_t                      length,
        _Out_ struct DriverConfiguration* driverConfig)
{
    yaml_parser_t       parser;
    yaml_event_t        event;
    struct parser_state state;
    int                 status;

    TRACE("DmDriverConfigParseYaml()");
    if (!yaml || !length || !driverConfig) {
        return OS_EINVALPARAMS;
    }

    memset(&state, 0, sizeof(state));
    state.state = STATE_START;
    list_construct(&state.driver.Vendors);
    list_construct(&state.driver.Resources);

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, yaml, length);
    do {
        status = yaml_parser_parse(&parser, &event);
        if (status == 0) {
            ERROR("DmDriverConfigParseYaml failed to parse driver configuration");
            __CleanupDriverConfiguration(&state.driver);
            return OS_EUNKNOWN;
        }

        status = __ConsumeEvent(&state, &event);
        if (status) {
            ERROR("DmDriverConfigParseYaml failed to parse driver configuration");
            __CleanupDriverConfiguration(&state.driver);
            return OS_EUNKNOWN;
        }
        yaml_event_delete(&event);
    } while (state.state != STATE_STOP);

    yaml_parser_delete(&parser);

    memcpy(driverConfig, &state.driver, sizeof(struct DriverConfiguration));
    return OS_EOK;
}

void
DmDriverConfigDestroy(
        _In_ struct DriverConfiguration* driverConfig)
{
    if (!driverConfig) {
        return;
    }

    __CleanupDriverConfiguration(driverConfig);
    free(driverConfig);
}
