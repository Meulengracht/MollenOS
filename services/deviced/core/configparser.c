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
 * Device Manager - Driver Configuration
 * Parses driver matching data, resources and capabilities from YAML.
 */

//#define __TRACE

#include <errno.h>
#include <discover.h>
#include <ddk/utils.h>
#include <ddk/capabilities.h>
#include <ddk/devices/platform.h>
#include <ds/list.h>
#include <yaml/yaml.h>

/**
 * Driver configuration format. Each type entry contains one class or subclass
 * field. Vendor keys and product IDs are integers. Resource types are numeric.
 *
 * driver:
 *   capabilities:
 *   - deviceio-access
 *   - interrupt-control
 *
 *   compatibles:
 *   - brcm,bcm2711-genet-v5
 *   - brcm,genet-v5
 * 
 *   pci:
 *     class: 0
 *     subclass: 0
 *     vendors:
 *     - 0x8086:
 *        - productid0
 *        - productid1
 *
 *   resources:
 *   - type: io
 *     base: 0x70
 *     length: 0x71
 *   - type: mmio
 *     base: 0xE0000000
 *     length: 0x1000
 *
 */
enum state {
    STATE_START,    // Before the stream starts.
    STATE_STREAM,   // Between documents in the stream.
    STATE_DOCUMENT, // Before or after the top-level mapping.
    STATE_SECTION,  // Top-level mapping keys.

    STATE_DRIVER,          // Before or after the driver mapping.
    STATE_DRIVERKEY,       // Keys within the driver mapping.
    STATE_DRIVERTYPE,      // Sequence of class and subclass mappings.
    STATE_DRIVERVENDORS,   // Sequence of vendor mappings.
    STATE_DRIVERRESOURCES, // Sequence of resource mappings.

    STATE_TYPECLASS,       // Class value and its mapping end.
    STATE_TYPESUBCLASS,    // Subclass value and its mapping end.

    STATE_VENDOR,          // Vendor ID and product sequence boundaries.
    STATE_VENDORPRODUCT,   // Product IDs for the current vendor.

    STATE_RESOURCE,       // Keys within a resource mapping.
    STATE_RESOURCETYPE,   // Resource type value.
    STATE_RESOURCEBASE,   // Resource base address value.
    STATE_RESOURCELENGTH, // Resource length value.

    STATE_COMPATIBLES_LIST,
    STATE_COMPATIBLES,

    STATE_CAPABILITIES_LIST,
    STATE_CAPABILITIES,

    STATE_STOP
};

/**
 * @brief YAML parsing position and the configuration being assembled.
 * Vendor and Resource point to entries under construction. Completed entries
 * are added to Driver's lists when their YAML mappings end.
 */
struct __ParserState {
    enum state                 State;
    struct DriverConfiguration Driver;
    struct DriverVendor*       Vendor;
    struct DriverResource*     Resource;
};

static oserr_t
__AddProduct(
    _InOut_ struct DriverVendor* vendor,
    _In_    uint32_t             productId)
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
__AddCompatible(
    _InOut_ struct __ParserState* state,
    _In_    const yaml_event_t*   event)
{
    struct DriverCompatible* compatible;
    size_t                   length = event->data.scalar.length;
    const char*              value = (const char*)event->data.scalar.value;

    if (!length || length >= PLATFORM_DEVICE_MAX_COMPATIBLES) {
        WARNING("__AddCompatible: invalid compatible length %zu", length);
        return -1;
    }

    compatible = calloc(1, sizeof(*compatible));
    if (compatible == NULL) {
        ERROR("__AddCompatible: failed to allocate compatible entry");
        return -1;
    }

    ELEMENT_INIT(&compatible->ListHeader, 0, compatible);
    compatible->Name = strdup(value);
    if (compatible->Name == NULL) {
        ERROR("__AddCompatible: failed to copy compatible %s", value);
        free(compatible);
        return -1;
    }

    list_append(&state->Driver.Compatibles, &compatible->ListHeader);
    return 0;
}

static int
__ParseBoolean(
    _In_  const char* string,
    _Out_ int*        value)
{
    char*  trueValues[] = {"y", "Y", "yes", "Yes", "YES", "true", "True", "TRUE", "on", "On", "ON", NULL};
    char*  falseValues[] = {"n", "N", "no", "No", "NO", "false", "False", "FALSE", "off", "Off", "OFF", NULL};
    char** current;

    for (current = trueValues; *current; current++) {
        if (strcmp(string, *current) == 0) {
            *value = 1;
            return 0;
        }
    }
    for (current = falseValues; *current; current++) {
        if (strcmp(string, *current) == 0) {
            *value = 0;
            return 0;
        }
    }
    return EINVAL;
}

static int
__ConsumeEvent(
    _InOut_ struct __ParserState* s,
    _In_    yaml_event_t*         event)
{
    char*          value;
    char*          end;
    Capabilities_t capability;
    uint32_t       productId;
    oserr_t        osStatus;
    int            status;

    switch (s->State) {
        case STATE_START:
            switch (event->type) {
                case YAML_STREAM_START_EVENT:
                    s->State = STATE_STREAM;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_STREAM:
            switch (event->type) {
                case YAML_DOCUMENT_START_EVENT:
                    s->State = STATE_DOCUMENT;
                    break;
                case YAML_STREAM_END_EVENT:
                    s->State = STATE_STOP;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_DOCUMENT:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    s->State = STATE_SECTION;
                    break;
                case YAML_DOCUMENT_END_EVENT:
                    s->State = STATE_STREAM;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_SECTION:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    if (strcmp(value, "driver") == 0) {
                        s->State = STATE_DRIVER;
                    } else {
                        ERROR("__ConsumeEvent Unexpected scalar: %s, expected 'driver'", value);
                        return -1;
                    }
                    break;
                case YAML_DOCUMENT_END_EVENT:
                    s->State = STATE_STREAM;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_DRIVER:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    s->State = STATE_DRIVERKEY;
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->State = STATE_SECTION;
                    break;
                default:
                    ERROR("Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_DRIVERKEY:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    if (strcmp(value, "capabilities") == 0) {
                        s->State = STATE_CAPABILITIES_LIST;
                    } else if (strcmp(value, "compatibles") == 0) {
                        s->State = STATE_COMPATIBLES_LIST;
                    } else if (strcmp(value, "type") == 0) {
                        s->State = STATE_DRIVERTYPE;
                    } else if (strcmp(value, "vendors") == 0) {
                        s->State = STATE_DRIVERVENDORS;
                    } else if (strcmp(value, "resources") == 0) {
                        s->State = STATE_DRIVERRESOURCES;
                    } else {
                        ERROR("__ConsumeEvent Unexpected key: %s", value);
                        return -1;
                    }
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->State = STATE_DRIVER;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_DRIVERTYPE:
            switch (event->type) {
                case YAML_SEQUENCE_START_EVENT:
                case YAML_MAPPING_START_EVENT:
                    // Each class or subclass field has its own mapping in the sequence.
                    break;

                case YAML_SEQUENCE_END_EVENT:
                    s->State = STATE_DRIVERKEY;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    if (strcmp(value, "class") == 0) {
                        s->State = STATE_TYPECLASS;
                    } else if (strcmp(value, "subclass") == 0) {
                        s->State = STATE_TYPESUBCLASS;
                    } else {
                        ERROR("__ConsumeEvent Unexpected key: %s", value);
                        return -1;
                    }
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_TYPECLASS:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    s->Driver.Class = (uint32_t)strtol(value, &end, 0);
                    if (end == value || *end != '\0') {
                        WARNING("__ConsumeEvent: invalid class value %s; keeping conversion result", value);
                    }
                    break;

                case YAML_MAPPING_END_EVENT:
                    s->State = STATE_DRIVERTYPE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_TYPESUBCLASS:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    s->Driver.Subclass = (uint32_t)strtol(value, &end, 0);
                    if (end == value || *end != '\0') {
                        WARNING("__ConsumeEvent: invalid subclass value %s; keeping conversion result", value);
                    }
                    break;

                case YAML_MAPPING_END_EVENT:
                    s->State = STATE_DRIVERTYPE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_DRIVERVENDORS:
            switch (event->type) {
                case YAML_SEQUENCE_START_EVENT:
                    break;

                case YAML_SEQUENCE_END_EVENT:
                    s->State = STATE_DRIVERKEY;
                    break;

                case YAML_MAPPING_START_EVENT:
                    s->State = STATE_VENDOR;
                    break;

                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_VENDOR:
            switch (event->type) {
                case YAML_MAPPING_END_EVENT:
                    // The vendor and its products now belong to the configuration.
                    list_append(&s->Driver.Vendors, &s->Vendor->ListHeader);
                    s->Vendor = NULL;
                    s->State = STATE_DRIVERVENDORS;
                    break;

                case YAML_SEQUENCE_START_EVENT:
                    s->State = STATE_VENDORPRODUCT;
                    break;

                case YAML_SCALAR_EVENT:
                    // The scalar key supplies the vendor ID for the product sequence.
                    s->Vendor = malloc(sizeof(struct DriverVendor));
                    if (!s->Vendor) {
                        ERROR("__ConsumeEvent out of memory allocating yaml vendor");
                        return -1;
                    }
                    ELEMENT_INIT(&s->Vendor->ListHeader, 0, s->Vendor);
                    list_construct(&s->Vendor->Products);
                    value = (char*)event->data.scalar.value;
                    s->Vendor->Id = (uint32_t)strtol(value, &end, 0);
                    if (end == value || *end != '\0') {
                        WARNING("__ConsumeEvent: invalid vendor value %s; keeping conversion result", value);
                    }
                    break;

                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_VENDORPRODUCT:
            switch (event->type) {
                case YAML_SEQUENCE_END_EVENT:
                    s->State = STATE_VENDOR;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    productId = (uint32_t)strtol(value, &end, 0);
                    if (end == value || *end != '\0') {
                        WARNING("__ConsumeEvent: invalid product value %s; keeping conversion result", value);
                    }
                    if (!productId) {
                        WARNING("__ConsumeEvent failed to parse product id: %s",
                                (const char*)event->data.scalar.value);
                    } else {
                        osStatus = __AddProduct(s->Vendor, productId);
                        if (osStatus != OS_EOK) {
                            ERROR("__ConsumeEvent: adding product 0x%x to vendor 0x%x failed: %u",
                                  productId, s->Vendor->Id, osStatus);
                            return -1;
                        }
                    }
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_DRIVERRESOURCES:
            switch (event->type) {
                case YAML_SEQUENCE_START_EVENT:
                    break;

                case YAML_MAPPING_START_EVENT:
                    // Keep the entry separate until its mapping has been parsed.
                    s->Resource = malloc(sizeof(struct DriverResource));
                    if (!s->Resource) {
                        ERROR("__ConsumeEvent out of memory allocating yaml resource");
                        return -1;
                    }
                    memset(s->Resource, 0, sizeof(struct DriverResource));
                    ELEMENT_INIT(&s->Resource->ListHeader, 0, s->Resource);
                    s->State = STATE_RESOURCE;
                    break;

                case YAML_SEQUENCE_END_EVENT:
                    s->State = STATE_DRIVERKEY;
                    break;

                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.\n", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_RESOURCE:
            switch (event->type) {
                case YAML_MAPPING_END_EVENT:
                    // The completed resource now belongs to the configuration.
                    list_append(&s->Driver.Resources, &s->Resource->ListHeader);
                    s->Resource = NULL;
                    s->State = STATE_DRIVERRESOURCES;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    if (strcmp(value, "type") == 0) {
                        s->State = STATE_RESOURCETYPE;
                    } else if (strcmp(value, "base") == 0) {
                        s->State = STATE_RESOURCEBASE;
                    } else if (strcmp(value, "length") == 0) {
                        s->State = STATE_RESOURCELENGTH;
                    } else {
                        ERROR("__ConsumeEvent Unexpected key: %s", value);
                        return -1;
                    }
                    break;

                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.\n", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_RESOURCETYPE:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    s->Resource->Type = (int)strtol(value, &end, 0);
                    if (end == value || *end != '\0') {
                        WARNING("__ConsumeEvent: invalid resource type %s; keeping conversion result", value);
                    }
                    s->State = STATE_RESOURCE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_RESOURCEBASE:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    s->Resource->Base = (uintptr_t)strtoll(value, &end, 0);
                    if (end == value || *end != '\0') {
                        WARNING("__ConsumeEvent: invalid resource base %s; keeping conversion result", value);
                    }
                    s->State = STATE_RESOURCE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_RESOURCELENGTH:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    s->Resource->Length = (size_t)strtoll(value, &end, 0);
                    if (end == value || *end != '\0') {
                        WARNING("__ConsumeEvent: invalid resource length %s; keeping conversion result", value);
                    }
                    s->State = STATE_RESOURCE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_COMPATIBLES_LIST:
            switch (event->type) {
                case YAML_SEQUENCE_START_EVENT:
                    s->State = STATE_COMPATIBLES;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_COMPATIBLES:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    if (event->data.scalar.value != NULL) {
                        status = __AddCompatible(s, event);
                        if (status != 0) {
                            ERROR("__ConsumeEvent: adding compatible failed: %i", status);
                            return status;
                        }
                        return 0;
                    }
                    break;

                case YAML_SEQUENCE_END_EVENT:
                    s->State = STATE_DRIVERKEY;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->State);
                    return -1;
            }
            break;

        case STATE_CAPABILITIES_LIST:
            if (event->type != YAML_SEQUENCE_START_EVENT) {
                ERROR("Driver capabilities must be a sequence");
                return -1;
            }
            s->State = STATE_CAPABILITIES;
            break;

        case STATE_CAPABILITIES:
            if (event->type == YAML_SEQUENCE_END_EVENT) {
                s->State = STATE_DRIVERKEY;
                break;
            }

            if (event->type != YAML_SCALAR_EVENT) {
                ERROR("Driver capability must be a name");
                return -1;
            }

            capability = CapabilityParse((const char*)event->data.scalar.value);
            if (capability == 0) {
                ERROR("Unknown driver capability: %s", event->data.scalar.value);
                return -1;
            }

            s->Driver.Capabilities |= capability;
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
__CleanupCompatible(
    _In_ element_t* item,
    _In_ void*      context)
{
    struct DriverCompatible* compatible = item->value;

    (void)context;
    free(compatible->Name);
    free(compatible);
}

static void
__CleanupDriverConfiguration(
    _InOut_ struct DriverConfiguration* driverConfig)
{
    list_clear(&driverConfig->Compatibles, __CleanupCompatible, NULL);
    list_clear(&driverConfig->Vendors, __CleanupVendor, NULL);
    list_clear(&driverConfig->Resources, __CleanupResource, NULL);
}

oserr_t
DmDriverConfigParseYaml(
    _In_  const uint8_t*              yaml,
    _In_  size_t                      length,
    _Out_ struct DriverConfiguration* driverConfig)
{
    yaml_parser_t        parser;
    yaml_event_t         event;
    struct __ParserState state;
    int                  status;

    TRACE("DmDriverConfigParseYaml(length=%zu)", length);
    if (!yaml || !length || !driverConfig) {
        WARNING("DmDriverConfigParseYaml: missing input or output, length %zu", length);
        return OS_EINVALPARAMS;
    }

    memset(&state, 0, sizeof(state));
    state.State = STATE_START;
    list_construct(&state.Driver.Vendors);
    list_construct(&state.Driver.Compatibles);
    list_construct(&state.Driver.Resources);

    status = yaml_parser_initialize(&parser);
    if (!status) {
        ERROR("DmDriverConfigParseYaml: failed to initialize YAML parser");
        return OS_EOOM;
    }
    yaml_parser_set_input_string(&parser, yaml, length);
    do {
        status = yaml_parser_parse(&parser, &event);
        if (status == 0) {
            ERROR("DmDriverConfigParseYaml: YAML parser failed in state %i: %s",
                  state.State, parser.problem != NULL ? parser.problem : "unknown error");
            __CleanupDriverConfiguration(&state.Driver);
            yaml_parser_delete(&parser);
            return OS_EUNKNOWN;
        }

        status = __ConsumeEvent(&state, &event);
        if (status) {
            ERROR("DmDriverConfigParseYaml: event %i rejected in state %i: %i",
                  event.type, state.State, status);
            __CleanupDriverConfiguration(&state.Driver);
            yaml_event_delete(&event);
            yaml_parser_delete(&parser);
            return OS_EUNKNOWN;
        }
        yaml_event_delete(&event);
    } while (state.State != STATE_STOP);

    yaml_parser_delete(&parser);

    // Transfer the completed lists to the caller's configuration storage.
    memcpy(driverConfig, &state.Driver, sizeof(struct DriverConfiguration));
    TRACE("DmDriverConfigParseYaml: parsed class %u, subclass %u, capabilities 0x%x",
          driverConfig->Class, driverConfig->Subclass, driverConfig->Capabilities);
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
