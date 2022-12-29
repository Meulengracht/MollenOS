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
 */

//#define __TRACE

#include <discover.h>
#include <ddk/utils.h>
#include <yaml/yaml.h>

/**
 * service configuration valuesservice:
 * # The service API path, for other instances to expect connection through.
 * # These API paths are registered upon startup of the service, and allows an
 * # easy way to recognize system services.
 * path: /service/serve
 *
 * # Dependencies allow for different criterias to be met before the service is
 * # started. Services can depend on other services, paths or devices to be registered
 * # with the system before being started up.
 * depends:
 *   # The list of services that should be running before starting this service.
 *   services:
 *     - filed # Require filed to be running as we need FS access
 *     - sessiond # Require sessiond to be running for user functionality
 *   paths:
 *   devices:
 */
enum state {
    STATE_START,    /* start state */
    STATE_STREAM,   /* start/end stream */
    STATE_DOCUMENT, /* start/end document */
    STATE_SECTION,  /* top level */

    STATE_SERVICE,          // service object discovered
    STATE_SERVICEKEY,       // service key discovered, followed by scalar
    STATE_SERVICEPATH,      // service path mapping
    STATE_SERVICEDEPS,      // service depends mapping

    STATE_DEPS_SERVICES,

    STATE_STOP
};

struct parser_state {
    enum state state;
    mstring_t* api_path;
    list_t     dependencies;
};

static int
__AddDependency(
        _In_ struct parser_state* s,
        _In_ const char*          d)
{
    element_t* entry;
    mstring_t* name;

    entry = malloc(sizeof(element_t));
    if (entry == NULL) {
        ERROR("__AddDependency failed to allocate memory for dependency");
        return -1;
    }

    name = mstr_new_u8(d);
    if (name == NULL) {
        ERROR("__AddDependency failed to allocate memory for dependency name");
        free(entry);
        return -1;
    }

    ELEMENT_INIT(entry, NULL, name);
    list_append(&s->dependencies, entry);
    return 0;
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
                    if (strcmp(value, "service") == 0) {
                        s->state = STATE_SERVICE;
                    } else {
                        ERROR("__ConsumeEvent Unexpected scalar: %s, expected 'service'", value);
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

        case STATE_SERVICE:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    s->state = STATE_SERVICEKEY;
                    break;
                case YAML_MAPPING_END_EVENT:
                    s->state = STATE_SECTION;
                    break;
                default:
                    ERROR("Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_SERVICEKEY:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "path") == 0) {
                        s->state = STATE_SERVICEPATH;
                    } else if (strcmp(value, "depends") == 0) {
                        s->state = STATE_SERVICEDEPS;
                    } else {
                        ERROR("__ConsumeEvent Unexpected key: %s", value);
                        return -1;
                    } break;
                case YAML_MAPPING_END_EVENT:
                    // end of driver structure
                    s->state = STATE_SERVICE;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        // Handle the value for the key service.path
        case STATE_SERVICEPATH:
            switch (event->type) {
                case YAML_SCALAR_EVENT:
                    value = (char*)event->data.scalar.value;
                    if (strlen(value) == 0 || strchr(value, '/') == NULL) {
                        ERROR("__ConsumeEvent Invalid path set for service.key=%s", value);
                        return -1;
                    }
                    s->api_path = mstr_new_u8(value);
                    s->state = STATE_SERVICEKEY;
                    break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_SERVICEDEPS:
            switch (event->type) {
                case YAML_MAPPING_START_EVENT:
                    // Start of dependencies
                    break;
                case YAML_MAPPING_END_EVENT:
                    // End of dependencies
                    s->state = STATE_SERVICEKEY;
                    break;

                case YAML_SCALAR_EVENT:
                    value = (char *)event->data.scalar.value;
                    if (strcmp(value, "services") == 0) {
                        s->state = STATE_DEPS_SERVICES;
                    } else {
                        ERROR("__ConsumeEvent Unexpected scalar: %s, expected 'services'", value);
                        return -1;
                    } break;
                default:
                    ERROR("__ConsumeEvent Unexpected event %d in state %d.", event->type, s->state);
                    return -1;
            }
            break;

        case STATE_DEPS_SERVICES:
            switch (event->type) {
                case YAML_SEQUENCE_START_EVENT:
                    // list start of dependencies
                    break;

                case YAML_SEQUENCE_END_EVENT:
                    // list end
                    s->state = STATE_SERVICEDEPS;
                    break;

                case YAML_SCALAR_EVENT:
                    // Occurs on a new dependency entry
                    if (__AddDependency(s, (const char*)event->data.scalar.value)) {
                        return -1;
                    }
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

oserr_t
PSParseServiceYAML(
        _In_ struct SystemService* systemService,
        _In_ const uint8_t*        yaml,
        _In_ size_t                length)
{
    yaml_parser_t       parser;
    yaml_event_t        event;
    struct parser_state state;
    int                 status;

    TRACE("PSParseServiceYAML()");
    if (!yaml || !length) {
        return OS_EINVALPARAMS;
    }

    memset(&state, 0, sizeof(state));
    state.state = STATE_START;
    list_construct(&state.dependencies);

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, yaml, length);
    do {
        status = yaml_parser_parse(&parser, &event);
        if (status == 0) {
            ERROR("PSParseServiceYAML failed to parse driver configuration");
            return OS_EUNKNOWN;
        }
        status = __ConsumeEvent(&state, &event);
        if (status == 0) {
            ERROR("PSParseServiceYAML failed to parse driver configuration");
            return OS_EUNKNOWN;
        }
        yaml_event_delete(&event);
    } while (state.state != STATE_STOP);

    // copy all the data into the structure provided
    systemService->APIPath = state.api_path;
    memcpy(&systemService->Dependencies, &state.dependencies, sizeof(list_t));

    yaml_parser_delete(&parser);
    return OS_EOK;
}
