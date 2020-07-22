import os
from .shared import Parameter, Function, Protocol


class CONST(object):
    __slots__ = ()
    TYPENAME_CASE_SIZEOF = 0
    TYPENAME_CASE_FUNCTION_CALL = 1
    TYPENAME_CASE_FUNCTION_STATUS = 2
    TYPENAME_CASE_FUNCTION_RESPONSE = 3
    TYPENAME_CASE_MEMBER = 4


def should_define_parameter(param, case):
    if case == CONST.TYPENAME_CASE_SIZEOF or case == CONST.TYPENAME_CASE_MEMBER:
        return not param.is_output()
    elif case == CONST.TYPENAME_CASE_FUNCTION_CALL:
        return not param.is_output() and not param.is_hidden()
    elif case == CONST.TYPENAME_CASE_FUNCTION_STATUS:
        return param.is_output() and not param.is_hidden()
    elif case == CONST.TYPENAME_CASE_FUNCTION_RESPONSE:
        return param.is_output() and not param.is_hidden()
    return False


def should_define_param_length_component(param, case):
    if case == CONST.TYPENAME_CASE_SIZEOF or case == CONST.TYPENAME_CASE_MEMBER:
        return False
    elif case == CONST.TYPENAME_CASE_FUNCTION_CALL:
        if param.is_buffer() and param.get_subtype() == "void*":
            return True
        if param.is_string() and param.is_output():
            return True
        return False
    elif case == CONST.TYPENAME_CASE_FUNCTION_STATUS:
        return False
    elif case == CONST.TYPENAME_CASE_FUNCTION_RESPONSE:
        if param.is_buffer() and param.get_subtype() == "void*" and param.get_count() == "1":
            return True
        return False
    return False


def get_input_struct_name(protocol, func):
    return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() \
           + "_" + func.get_name().lower() + "_args"


def get_event_struct_name(protocol, evt):
    return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() \
           + "_" + evt.get_name().lower() + "_event"


def get_enum_name(protocol, enum):
    if enum.is_global():
        return protocol.get_namespace().lower() + "_" + enum.get_name().lower()
    else:
        return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + enum.get_name().lower()


def get_message_flags_func(func):
    if len(func.get_response_params()) == 0:
        return "MESSAGE_FLAG_ASYNC"
    return "MESSAGE_FLAG_SYNC"


def get_message_size_string(params):
    message_size = "sizeof(struct gracht_message)"
    if len(params) > 0:
        message_size = message_size + " + (" + str(len(params)) + " * sizeof(struct gracht_param))"
    return message_size


def define_headers(headers, outfile):
    for header in headers:
        outfile.write("#include " + header + "\n")
    outfile.write("\n")
    return


def define_types(protocol, outfile):
    for type_def in protocol.get_types():
        if not type_def.get_header():
            outfile.write("typedef " + type_def.get_definition() + " " + type_def.get_name() + ";\n")
    outfile.write("\n")
    return


def include_shared_header(protocol, outfile):
    outfile.write("#include \"" + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol.h\"\n")
    return


def define_protocol_headers(protocol, outfile):
    for imp in protocol.get_imports():
        outfile.write("#include <" + imp + ">\n")
    outfile.write("\n")
    return


def write_param_values(protocol, func, param, outfile):
    for value in param.get_values():
        outfile.write(
            "#define " + protocol.get_namespace().upper() + "_" + protocol.get_name().upper() + "_"
            + func.get_name().upper() + "_" + param.get_name().upper() + "_"
            + value.get_name() + " " + value.get_value() + "\n")
    outfile.write("\n")
    return


def get_param_typename(protocol, param, case):
    # resolve enums and structs
    if param.get_enum_ref() is not None:
        param_typename = "enum " + get_enum_name(protocol, param.get_enum_ref())
    else:
        param_typename = param.get_typename()

    # resolve special types
    if not param.is_value():
        if param.is_string():
            param_typename = "char"
        elif param.is_buffer():
            param_typename = param.get_subtype()

    # format parameter, unfortunately there are 5 cases to do this
    # TYPENAME_CASE_SIZEOF means we would like the typename for a sizeof call
    if case == CONST.TYPENAME_CASE_SIZEOF:
        return param_typename

    # TYPENAME_CASE_FUNCTION_CALL is used for event prototypes and normal
    # async function prototypes
    elif case == CONST.TYPENAME_CASE_FUNCTION_CALL or case == CONST.TYPENAME_CASE_FUNCTION_STATUS:
        param_name = param.get_name()
        if not param.is_value() and not param.is_array() and not param_typename.endswith("*"):
            param_typename = param_typename + "*"

        if param.is_output():
            if param.is_value():
                param_typename = param_typename + "*"
            param_name = param_name + "_out"
        param_typename = param_typename + " " + param_name
        if param.is_array():
            param_typename = param_typename + "[" + str(param.get_count()) + "]"
        return param_typename

    elif case == CONST.TYPENAME_CASE_FUNCTION_RESPONSE:
        if not param.is_value() and not param_typename.endswith("*"):
            param_typename = param_typename + "*"
        param_typename = param_typename + " " + param.get_name()
        if param.is_array():
            param_typename = param_typename + "[" + str(param.get_count()) + "]"
        return param_typename

    elif case == CONST.TYPENAME_CASE_MEMBER:
        if not param.is_value() and not param_typename.endswith("*"):
            param_typename = param_typename + "*"
        param_typename = param_typename + " " + param.get_name()
        #if param.is_array():
        #    param_typename = param_typename + "[" + str(param.get_count()) + "]"
        return param_typename
    return param_typename


def get_parameter_string(protocol, params, case):
    parameters_valid = []
    for param in params:
        if should_define_parameter(param, case):
            parameters_valid.append(get_param_typename(protocol, param, case))
        if should_define_param_length_component(param, case):
            parameters_valid.append(
                get_param_typename(protocol, Parameter(param.get_name() + "_length", "size_t"), case))

    return ", ".join(parameters_valid)


def get_protocol_server_response_name(protocol, func):
    return protocol.get_namespace() + "_" + protocol.get_name() + "_" + func.get_name() + "_response"


def get_protocol_server_callback_name(protocol, func):
    return protocol.get_namespace() + "_" + protocol.get_name() + "_" + func.get_name() + "_callback"


def get_protocol_client_event_callback_name(protocol, evt):
    return protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_callback"


def get_protocol_event_prototype_name_single(protocol, evt, case):
    evt_client_param = get_param_typename(protocol, Parameter("client", "int"), case)
    evt_name = "int " + protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_single(" + evt_client_param

    if len(evt.get_params()) > 0:
        evt_name = evt_name + ", " + get_parameter_string(protocol, evt.get_params(), case)
    return evt_name + ")"


def get_protocol_event_prototype_name_all(protocol, evt, case):
    evt_name = "int " + protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_all("

    if len(evt.get_params()) > 0:
        evt_name = evt_name + get_parameter_string(protocol, evt.get_params(), case)
    else:
        evt_name = evt_name + "void"
    return evt_name + ")"


def write_header(outfile):
    outfile.write("/**\n")
    outfile.write(
        " * This file was generated by the gracht protocol generator script. Any changes done here will be overwritten.\n")
    outfile.write(" */\n\n")
    return


def write_header_guard_start(file_name, outfile):
    outfile.write("#ifndef __" + str.replace(file_name, ".", "_").upper() + "__\n")
    outfile.write("#define __" + str.replace(file_name, ".", "_").upper() + "__\n\n")
    return


def write_header_guard_end(file_name, outfile):
    outfile.write("#endif //!__" + str.replace(file_name, ".", "_").upper() + "__\n")
    return


def write_c_guard_start(outfile):
    outfile.write("#ifdef __cplusplus\n")
    outfile.write("extern \"C\" {\n")
    outfile.write("#endif\n")
    return


def write_c_guard_end(outfile):
    outfile.write("#ifdef __cplusplus\n")
    outfile.write("}\n")
    outfile.write("#endif\n\n")
    return


def define_shared_ids(protocol, outfile):
    prefix = protocol.get_namespace().upper() + "_" + protocol.get_name().upper()
    outfile.write("#define PROTOCOL_" + prefix + "_ID " + protocol.get_id() + "\n")
    outfile.write("#define PROTOCOL_" + prefix + "_FUNCTION_COUNT " + str(len(protocol.get_functions())) + "\n\n")

    if len(protocol.get_functions()) > 0:
        for func in protocol.get_functions():
            func_prefix = prefix + "_" + func.get_name().upper()
            outfile.write("#define PROTOCOL_" + func_prefix + "_ID " + func.get_id() + "\n")
        outfile.write("\n")

    if len(protocol.get_events()) > 0:
        for evt in protocol.get_events():
            evt_prefix = prefix + "_EVENT_" + evt.get_name().upper()
            outfile.write("#define PROTOCOL_" + evt_prefix + "_ID " + evt.get_id() + "\n")
        outfile.write("\n")
    return


def get_size_function(protocol, param, case):
    # check for special function or parameter
    if case == CONST.TYPENAME_CASE_FUNCTION_STATUS and not param.is_value():
        return "0"

    if should_define_param_length_component(param, case):
        return param.get_name() + "_length"

    if param.is_string():
        return "((" + param.get_name() + " == NULL) ? 0 : (strlen(&" + param.get_name() + "[0]) + 1))"

    # otherwise we use sizeof with optional * count
    if param.is_array():
        return "sizeof(" + get_param_typename(protocol, param,
                                              CONST.TYPENAME_CASE_SIZEOF) + ") * " + param.get_count()

    return "sizeof(" + get_param_typename(protocol, param, CONST.TYPENAME_CASE_SIZEOF) + ")"


def define_message_struct(protocol, action_id, params_in, params_out, flags, case, outfile):
    params_all = params_in + params_out

    # define variables
    if len(params_all) > 0:
        outfile.write("    struct {\n")
        outfile.write("        struct gracht_message_header __base;\n")
        outfile.write("        struct gracht_param          __params[" + str(len(params_all)) + "];\n")
        outfile.write("    } __message = { .__base = { \n")
        outfile.write("        .id = 0,\n")
        outfile.write("        .length = sizeof(struct gracht_message)")
        outfile.write(" + (" + str(len(params_all)) + " * sizeof(struct gracht_param))")
        for param in params_in:
            if not param.is_value():
                size_function = get_size_function(protocol, param, case)
                outfile.write(" + " + size_function)
    else:
        outfile.write("    struct gracht_message_header __message = {\n")
        outfile.write("        .id = 0,\n")
        outfile.write("        .length = sizeof(struct gracht_message_header)")
    outfile.write(",\n")

    outfile.write("        .param_in = " + str(len(params_in)) + ",\n")
    outfile.write("        .param_out = " + str(len(params_out)) + ",\n")
    outfile.write("        .flags = " + flags + ",\n")
    outfile.write("        .protocol = " + str(protocol.get_id()) + ",\n")
    outfile.write("        .action = " + str(action_id))
    if len(params_all) > 0:
        outfile.write("\n    }, .__params = {\n")
        for index, param in enumerate(params_in):
            size_function = get_size_function(protocol, param, case)
            if param.is_hidden() and param.get_default_value():
                outfile.write(
                    "            { .type = GRACHT_PARAM_VALUE, .data.value = (size_t)"
                    + param.get_default_value() + ", .length = " + size_function + " }")
            elif param.is_value():
                outfile.write(
                    "            { .type = GRACHT_PARAM_VALUE, .data.value = (size_t)"
                    + param.get_name() + ", .length = " + size_function + " }")
            elif param.is_buffer() or param.is_string() or param.is_array():
                outfile.write(
                    "            { .type = GRACHT_PARAM_BUFFER, .data.buffer = "
                    + param.get_name() + ", .length = " + size_function + " }")
            elif param.is_shm():
                outfile.write(
                    "            { .type = GRACHT_PARAM_SHM, .data.buffer = "
                    + param.get_name() + ", .length = " + size_function + " }")

            if index + 1 < len(params_all):
                outfile.write(",\n")
            else:
                outfile.write("\n")
        for index, param in enumerate(params_out):
            size_function = get_size_function(protocol, param, case)
            if param.is_value():
                outfile.write(
                    "            { .type = GRACHT_PARAM_VALUE, .data.buffer = NULL, .length = "
                    + size_function + " }")
            elif param.is_buffer() or param.is_string() or param.is_array():
                outfile.write(
                    "            { .type = GRACHT_PARAM_BUFFER, .data.buffer = NULL, .length = "
                    + size_function + " }")
            elif param.is_shm():
                outfile.write(
                    "            { .type = GRACHT_PARAM_SHM, .data.buffer = NULL, .length = "
                    + size_function + " }")

            if index + 1 < len(params_out):
                outfile.write(",\n")
            else:
                outfile.write("\n")
        outfile.write("        }")
    outfile.write("\n    };\n\n")
    return


def define_status_struct(protocol, params_out, case, outfile):
    outfile.write("    struct gracht_param __params[" + str(len(params_out)) + "] = {\n")
    for index, param in enumerate(params_out):
        size_function = get_size_function(protocol, param, case)
        buffer_variable = param.get_name() + "_out"

        if param.is_value():
            outfile.write(
                "            { .type = GRACHT_PARAM_VALUE, .data.buffer = "
                + buffer_variable + ", .length = " + size_function + " }")
        elif param.is_buffer() or param.is_string() or param.is_array():
            outfile.write(
                "            { .type = GRACHT_PARAM_BUFFER, .data.buffer = "
                + buffer_variable + ", .length = " + size_function + " }")
        elif param.is_shm():
            outfile.write(
                "            { .type = GRACHT_PARAM_SHM, .data.buffer = "
                + buffer_variable + ", .length = " + size_function + " }")

        if index + 1 < len(params_out):
            outfile.write(",\n")
        else:
            outfile.write("\n")
    outfile.write("    };\n\n")
    return


def define_function_body(protocol, func, outfile):
    flags = get_message_flags_func(func)
    define_message_struct(protocol, func.get_id(), func.get_request_params(), func.get_response_params(),
                          flags, CONST.TYPENAME_CASE_FUNCTION_CALL, outfile)
    outfile.write("    return gracht_client_invoke(client, context, (struct gracht_message*)&__message);\n")
    return


def define_status_body(protocol, func, outfile):
    define_status_struct(protocol, func.get_response_params(),
                         CONST.TYPENAME_CASE_FUNCTION_STATUS, outfile)
    outfile.write("    return gracht_client_status(client, context, &__params[0]);\n")
    return


def define_event_body_single(protocol, evt, outfile):
    define_message_struct(protocol, evt.get_id(), evt.get_params(), [], "MESSAGE_FLAG_EVENT",
                          CONST.TYPENAME_CASE_FUNCTION_CALL, outfile)
    outfile.write("    return gracht_server_send_event(client, (struct gracht_message*)&__message, 0);\n")
    return


def define_event_body_all(protocol, evt, outfile):
    define_message_struct(protocol, evt.get_id(), evt.get_params(), [], "MESSAGE_FLAG_EVENT",
                          CONST.TYPENAME_CASE_FUNCTION_CALL, outfile)
    outfile.write("    return gracht_server_broadcast_event((struct gracht_message*)&__message, 0);\n")
    return


def define_response_body(protocol, func, flags, outfile):
    define_message_struct(protocol, func.get_id(), func.get_response_params(), [], flags,
                          CONST.TYPENAME_CASE_FUNCTION_RESPONSE, outfile)
    outfile.write("    return gracht_server_respond(message, (struct gracht_message*)&__message);\n")
    return


def define_message_sizes(protocol, outfile):
    prefix = protocol.get_namespace().upper() + "_" + protocol.get_name().upper()
    if len(protocol.get_functions()) > 0:
        for func in protocol.get_functions():
            func_prefix = prefix + "_" + func.get_name().upper()
            func_params_all = func.get_request_params() + func.get_response_params()
            outfile.write(
                "#define PROTOCOL_" + func_prefix + "_SIZE " + get_message_size_string(func_params_all) + "\n")
        outfile.write("\n")

    if len(protocol.get_events()) > 0:
        for evt in protocol.get_events():
            evt_prefix = prefix + "_EVENT_" + evt.get_name().upper()
            outfile.write(
                "#define PROTOCOL_" + evt_prefix + "_SIZE " + get_message_size_string(evt.get_params()) + "\n")
        outfile.write("\n")
    return


def write_enum(enum_name, values, outfile):
    outfile.write("enum " + enum_name + " {\n")
    for value in values:
        if value.get_value() is None:
            outfile.write("    " + value.get_name() + ",\n")
        else:
            outfile.write("    " + value.get_name() + " = " + value.get_value() + ",\n")
    outfile.write("};\n")
    return


def define_enums(protocol, outfile):
    if len(protocol.get_enums()) > 0:
        for enum in protocol.get_enums():
            if len(enum.get_values()):
                enum_name = get_enum_name(protocol, enum)
                if enum.is_global():
                    outfile.write("#ifndef __" + enum_name.upper() + "_DEFINED__\n")
                    outfile.write("#define __" + enum_name.upper() + "_DEFINED__\n")
                write_enum(enum_name, enum.get_values(), outfile)
                if enum.is_global():
                    outfile.write("#endif //!__" + enum_name.upper() + "_DEFINED__\n\n")
        outfile.write("\n")
    return


def write_structure(protocol, struct_name, params, case, outfile):
    outfile.write("GRACHT_STRUCT(" + struct_name + ", {\n")
    for param in params:
        outfile.write("    " + get_param_typename(protocol, param, case) + ";\n")
    outfile.write("});\n")
    return


def define_param_values(protocol, func, outfile):
    for param in func.get_request_params():
        if len(param.get_values()) > 0:
            write_param_values(protocol, func, param, outfile)
    for param in func.get_response_params():
        if len(param.get_values()) > 0:
            write_param_values(protocol, func, param, outfile)


def define_protocol_values(protocol, outfile):
    for func in protocol.get_functions():
        define_param_values(protocol, func, outfile)


def define_structures(protocol, outfile):
    for func in protocol.get_functions():
        if len(func.get_request_params()):
            write_structure(protocol, get_input_struct_name(protocol, func), func.get_request_params(),
                            CONST.TYPENAME_CASE_MEMBER, outfile)
            outfile.write("\n")

    for evt in protocol.get_events():
        if len(evt.get_params()):
            write_structure(protocol, get_event_struct_name(protocol, evt), evt.get_params(),
                            CONST.TYPENAME_CASE_MEMBER, outfile)
            outfile.write("\n")
    return


class CGenerator:

    def get_server_callback_prototype(self, protocol, func):
        function_prototype = "void " + get_protocol_server_callback_name(protocol, func) + "("
        function_message_param = get_param_typename(protocol, Parameter("message", "struct gracht_recv_message*"),
                                                    CONST.TYPENAME_CASE_FUNCTION_RESPONSE)
        parameter_string = function_message_param
        if len(func.get_request_params()) > 0:
            parameter_string = parameter_string + ", struct " + get_input_struct_name(protocol, func) + "*"
        return function_prototype + parameter_string + ")"

    def get_response_prototype(self, protocol, func, case):
        function_prototype = "int " + get_protocol_server_response_name(protocol, func) + "("
        function_message_param = get_param_typename(protocol, Parameter("message", "struct gracht_recv_message*"),
                                                    case)
        parameter_string = function_message_param + ", "
        parameter_string = parameter_string + get_parameter_string(protocol, func.get_response_params(), case)
        return function_prototype + parameter_string + ")"

    def get_function_prototype(self, protocol, func, case):
        function_prototype = "int " + protocol.get_namespace().lower() + "_" \
                             + protocol.get_name().lower() + "_" + func.get_name()
        function_client_param = get_param_typename(protocol, Parameter("client", "gracht_client_t*"), case)
        function_context_param = get_param_typename(protocol,
                                                    Parameter("context", "struct gracht_message_context*"), case)
        function_prototype = function_prototype + "(" + function_client_param + ", " + function_context_param
        input_parameters = get_parameter_string(protocol, func.get_request_params(), case)
        output_parameters = get_parameter_string(protocol, func.get_response_params(), case)

        if input_parameters != "":
            input_parameters = ", " + input_parameters
        if output_parameters != "":
            output_parameters = ", " + output_parameters

        return function_prototype + input_parameters + output_parameters + ")"

    def get_function_status_prototype(self, protocol, func, case):
        function_prototype = "int " + protocol.get_namespace().lower() + "_" \
                             + protocol.get_name().lower() + "_" + func.get_name() + "_result"
        function_client_param = get_param_typename(protocol, Parameter("client", "gracht_client_t*"), case)
        function_context_param = get_param_typename(protocol,
                                                    Parameter("context", "struct gracht_message_context*"), case)
        function_prototype = function_prototype + "(" + function_client_param + ", " + function_context_param
        output_param_string = get_parameter_string(protocol, func.get_response_params(), case)
        return function_prototype + ", " + output_param_string + ")"

    def define_prototypes(self, protocol, outfile):
        # This actually defines the client functions implementations, to support the subscribe/unsubscribe we must
        # generate two additional functions that have special ids
        self.define_client_subscribe_prototype(protocol, outfile)
        self.define_client_unsubscribe_prototype(protocol, outfile)

        for func in protocol.get_functions():
            outfile.write("    " +
                          self.get_function_prototype(protocol, func, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
            if len(func.get_response_params()) > 0:
                outfile.write("    " + self.get_function_status_prototype(protocol, func,
                                                                          CONST.TYPENAME_CASE_FUNCTION_STATUS) + ";\n")
        outfile.write("\n")
        return

    def define_client_subscribe_prototype(self, protocol, outfile):
        subscribe_arg = Parameter("protocol", "uint8_t", "void*", "1", [], str(protocol.get_id()), True)
        subscribe_fn = Function("subscribe", 0, [subscribe_arg], [])
        control_protocol = Protocol(protocol.get_namespace(), 0, protocol.get_name(), [], [], [], [])
        outfile.write("    " +
                      self.get_function_prototype(control_protocol,
                                                  subscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        return

    def define_client_subscribe(self, protocol, outfile):
        subscribe_arg = Parameter("protocol", "uint8_t", "void*", "1", [], str(protocol.get_id()), True)
        subscribe_fn = Function("subscribe", 0, [subscribe_arg], [])
        control_protocol = Protocol(protocol.get_namespace(), 0, protocol.get_name(), [], [], [], [])
        outfile.write(
            self.get_function_prototype(control_protocol, subscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
        outfile.write("{\n")
        define_function_body(control_protocol, subscribe_fn, outfile)
        outfile.write("}\n\n")
        return

    def define_client_unsubscribe_prototype(self, protocol, outfile):
        subscribe_arg = Parameter("protocol", "uint8_t", "void*", "1", [], str(protocol.get_id()), True)
        subscribe_fn = Function("unsubscribe", 1, [subscribe_arg], [])
        control_protocol = Protocol(protocol.get_namespace(), 0, protocol.get_name(), [], [], [], [])
        outfile.write("    " +
                      self.get_function_prototype(control_protocol,
                                                  subscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        return

    def define_client_unsubscribe(self, protocol, outfile):
        subscribe_arg = Parameter("protocol", "uint8_t", "void*", "1", [], str(protocol.get_id()), True)
        subscribe_fn = Function("unsubscribe", 1, [subscribe_arg], [])
        control_protocol = Protocol(protocol.get_namespace(), 0, protocol.get_name(), [], [], [], [])
        outfile.write(
            self.get_function_prototype(control_protocol, subscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
        outfile.write("{\n")
        define_function_body(control_protocol, subscribe_fn, outfile)
        outfile.write("}\n\n")
        return

    def define_client_functions(self, protocol, outfile):
        # This actually defines the client functions implementations, to support the subscribe/unsubscribe we must
        # generate two additional functions that have special ids
        self.define_client_subscribe(protocol, outfile)
        self.define_client_unsubscribe(protocol, outfile)

        for func in protocol.get_functions():
            outfile.write(self.get_function_prototype(protocol, func, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
            outfile.write("{\n")
            define_function_body(protocol, func, outfile)
            outfile.write("}\n\n")

            if len(func.get_response_params()) > 0:
                outfile.write(
                    self.get_function_status_prototype(protocol, func, CONST.TYPENAME_CASE_FUNCTION_STATUS) + "\n")
                outfile.write("{\n")
                define_status_body(protocol, func, outfile)
                outfile.write("}\n\n")
        return

    def define_server_responses(self, protocol, outfile):
        for func in protocol.get_functions():
            if len(func.get_response_params()) > 0:
                outfile.write(self.get_response_prototype(protocol, func, CONST.TYPENAME_CASE_FUNCTION_RESPONSE) + "\n")
                outfile.write("{\n")
                define_response_body(protocol, func, "MESSAGE_FLAG_RESPONSE", outfile)
                outfile.write("}\n\n")

    def define_events(self, protocol, outfile):
        for evt in protocol.get_events():
            outfile.write(
                get_protocol_event_prototype_name_single(protocol, evt, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
            outfile.write("{\n")
            define_event_body_single(protocol, evt, outfile)
            outfile.write("}\n\n")

            outfile.write(
                get_protocol_event_prototype_name_all(protocol, evt, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
            outfile.write("{\n")
            define_event_body_all(protocol, evt, outfile)
            outfile.write("}\n\n")
        return

    def write_protocol_server_callback(self, protocol, func, outfile):
        outfile.write("    " + self.get_server_callback_prototype(protocol, func))
        outfile.write(";\n")

        if len(func.get_response_params()) > 0:
            outfile.write("    " + self.get_response_prototype(protocol, func, CONST.TYPENAME_CASE_FUNCTION_RESPONSE))
            outfile.write(";\n")
        return

    def write_protocol_event_prototype(self, protocol, evt, outfile):
        outfile.write("    " + get_protocol_event_prototype_name_single(protocol, evt,
                                                                        CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        outfile.write("    " + get_protocol_event_prototype_name_all(protocol, evt,
                                                                     CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        return

    def get_client_protocol_event_callback(self, protocol, evt):
        prototype = "void " + get_protocol_client_event_callback_name(protocol, evt) + "("
        if len(evt.get_params()) > 0:
            prototype = prototype + "struct " + get_event_struct_name(protocol, evt) + "*"
        else:
            prototype = prototype + "void"
        return prototype + ")"

    def write_client_protocol_prototype(self, protocol, outfile):
        if len(protocol.get_events()) > 0:
            outfile.write("/*\n")
            outfile.write(" * Define the callback array like this for this protocol\n")
            outfile.write(" * \n")
            for evt in protocol.get_events():
                outfile.write(" * " + self.get_client_protocol_event_callback(protocol, evt) + ";\n")
            outfile.write(" *\n")
            outfile.write(" * static gracht_protocol_function_t ")
            outfile.write(protocol.get_namespace() + "_" + protocol.get_name() + "_callbacks[")
            outfile.write(str(len(protocol.get_events())) + "] = {\n")
            for evt in protocol.get_events():
                evt_name = protocol.get_namespace().upper() + "_" \
                           + protocol.get_name().upper() + "_EVENT_" + evt.get_name().upper()
                evt_definition = "PROTOCOL_" + evt_name + "_ID "
                outfile.write(" *    { " + evt_definition + ", "
                              + get_protocol_client_event_callback_name(protocol, evt) + " },\n")
            outfile.write(" * };\n")
            outfile.write(" */\n\n")

            outfile.write("#define DEFINE_" + protocol.get_namespace().upper() + "_" + protocol.get_name().upper())
            outfile.write("_CLIENT_PROTOCOL(cbTable, cbCount) gracht_protocol_t ")
            outfile.write(protocol.get_namespace() + "_" + protocol.get_name() + "_client_protocol = ")
            outfile.write("GRACHT_PROTOCOL_INIT(" + protocol.get_id() + ", \""
                          + protocol.get_namespace().lower() + "_" + protocol.get_name().lower()
                          + "\", cbCount, cbTable)\n\n")
        return

    def write_server_protocol_prototypes(self, protocol, outfile):
        # write response prototypes
        if len(protocol.get_functions()) > 0:
            for func in protocol.get_functions():
                if len(func.get_response_params()) > 0:
                    outfile.write("    " + self.get_response_prototype(
                        protocol, func, CONST.TYPENAME_CASE_FUNCTION_RESPONSE))
                    outfile.write(";\n")
            outfile.write("\n")

        # write event prototypes
        if len(protocol.get_events()) > 0:
            for evt in protocol.get_events():
                self.write_protocol_event_prototype(protocol, evt, outfile)
            outfile.write("\n")
        return

    def write_server_protocol_helpers(self, protocol, outfile):
        if len(protocol.get_functions()) > 0:
            outfile.write("/*\n")
            outfile.write(" * Define the callback array like this for this protocol\n")
            outfile.write(" * \n")
            for func in protocol.get_functions():
                outfile.write(" * " + self.get_server_callback_prototype(protocol, func) + ";\n")
            outfile.write(" *\n")
            outfile.write(" * static gracht_protocol_function_t ")
            outfile.write(protocol.get_namespace() + "_" + protocol.get_name() + "_callbacks[")
            outfile.write(str(len(protocol.get_functions())) + "] = {\n")
            for func in protocol.get_functions():
                func_name = protocol.get_namespace().upper() + "_" \
                            + protocol.get_name().upper() + "_" + func.get_name().upper()
                func_definition = "PROTOCOL_" + func_name + "_ID "
                outfile.write(" *    { " + func_definition + ", "
                              + get_protocol_server_callback_name(protocol, func) + " },\n")
            outfile.write(" * };\n")
            outfile.write(" */\n\n")

            outfile.write("#define DEFINE_" + protocol.get_namespace().upper() + "_" + protocol.get_name().upper())
            outfile.write("_SERVER_PROTOCOL(cbTable, cbCount) gracht_protocol_t ")
            outfile.write(protocol.get_namespace() + "_" + protocol.get_name() + "_server_protocol = ")
            outfile.write("GRACHT_PROTOCOL_INIT(" + protocol.get_id() + ", \""
                          + protocol.get_namespace().lower() + "_" + protocol.get_name().lower()
                          + "\", cbCount, cbTable)\n\n")
        return

    def generate_shared_header(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            write_header(f)
            write_header_guard_start(file_name, f)
            define_headers(["<gracht/types.h>"], f)
            define_protocol_headers(protocol, f)
            define_shared_ids(protocol, f)
            define_message_sizes(protocol, f)
            define_enums(protocol, f)
            define_protocol_values(protocol, f)
            define_structures(protocol, f)
            write_header_guard_end(file_name, f)
        return

    def generate_client_header(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_client.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            write_header(f)
            write_header_guard_start(file_name, f)
            define_headers(["<gracht/client.h>"], f)
            include_shared_header(protocol, f)
            define_types(protocol, f)
            write_c_guard_start(f)
            self.define_prototypes(protocol, f)
            write_c_guard_end(f)
            self.write_client_protocol_prototype(protocol, f)
            write_header_guard_end(file_name, f)
        return

    def generate_client_impl(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_client.c"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            write_header(f)
            define_headers([
                "\"" + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_client.h\"",
                "<string.h>"], f)
            self.define_client_functions(protocol, f)
        return

    def generate_server_header(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_server.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            write_header(f)
            write_header_guard_start(file_name, f)
            define_headers(["<gracht/server.h>"], f)
            include_shared_header(protocol, f)
            define_types(protocol, f)
            write_c_guard_start(f)
            self.write_server_protocol_prototypes(protocol, f)
            write_c_guard_end(f)
            self.write_server_protocol_helpers(protocol, f)
            write_header_guard_end(file_name, f)
        return

    def generate_server_impl(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_server.c"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            write_header(f)
            define_headers([
                "\"" + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_server.h\"",
                "<string.h>"], f)
            self.define_server_responses(protocol, f)
            self.define_events(protocol, f)
        return

    def generate_shared_files(self, out, protocols, include_protocols):
        for proto in protocols:
            if (len(include_protocols) == 0) or (proto.get_name() in include_protocols):
                self.generate_shared_header(proto, out)
        return

    def generate_client_files(self, out, protocols, include_protocols):
        for proto in protocols:
            if (len(include_protocols) == 0) or (proto.get_name() in include_protocols):
                self.generate_client_header(proto, out)
                self.generate_client_impl(proto, out)
        return

    def generate_server_files(self, out, protocols, include_protocols):
        for proto in protocols:
            if (len(include_protocols) == 0) or (proto.get_name() in include_protocols):
                self.generate_server_header(proto, out)
                self.generate_server_impl(proto, out)
        return
