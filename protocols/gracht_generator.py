#!/usr/bin/env python
import os, sys
import argparse
import copy
import xml.etree.ElementTree as ET

class TypeDefinition:
    def __init__(self, name, header, definition, global_scope):
        self.name = name
        self.header = header
        self.definition = definition
        self.global_scope = global_scope
    
    def get_name(self):
        return self.name
    def get_header(self):
        return self.header
    def get_definition(self):
        return self.definition
    def is_global(self):
        return self.global_scope

class ValueDefinition:
    def __init__(self, name, value):
        self.name = name
        self.value = value

    def get_name(self):
        return self.name
    def get_value(self):
        return self.value

class Enumerator:
    def __init__(self, name, values, global_scope):
        self.name = name
        self.values = values
        self.global_scope = global_scope

    def get_name(self):
        return self.name
    def get_values(self):
        return self.values
    def is_global(self):
        return self.global_scope

class Parameter:
    def __init__(self, name, typename, count):
        self.name = name
        self.typename = typename
        self.count = count
        self.output = False
        self.enum_ref = None

    def set_enum(self, enum_ref):
        self.enum_ref = enum_ref
    def set_output(self, output):
        self.output = output

    def get_name(self):
        return self.name
    def get_typename(self):
        return self.typename
    def get_count(self):
        return self.count
    def get_enum_ref(self):
        return self.enum_ref
    def is_output(self):
        return self.output

class Event:
    def __init__(self, name, id, params):
        self.name = name
        self.id = id
        self.params = params
    
    def get_name(self):
        return self.name
    def get_id(self):
        return self.id
    def get_params(self):
        return self.params

class Function:
    def __init__(self, name, id, synchronous, request_params, response_params):
        self.name = name
        self.id = id
        self.synchronous = synchronous
        self.request_params = request_params
        self.response_params = response_params
    
    def get_name(self):
        return self.name
    def get_id(self):
        return self.id
    def is_synchronous(self):
        return self.synchronous
    def get_request_params(self):
        return self.request_params
    def get_response_params(self):
        return self.response_params

class Protocol:
    def __init__(self, namespace, id, name, types, enums, functions, events):
        self.namespace = namespace
        self.name = name
        self.id = id
        self.types = types
        self.enums = enums
        self.functions = functions
        self.events = events
        self.resolve_enum_types()

    def resolve_param_enum_types(self, param):
        for enum in self.enums:
            if enum.get_name().lower() == param.get_typename().lower():
                param.set_enum(enum)
        return

    def resolve_enum_types(self):
        for func in self.functions:
            for param in func.get_request_params():
                self.resolve_param_enum_types(param)
            for param in func.get_response_params():
                self.resolve_param_enum_types(param)
                param.set_output(True)
        for evt in self.events:
            for param in evt.get_params():
                self.resolve_param_enum_types(param)
        return
    
    def get_namespace(self):
        return self.namespace
    def get_name(self):
        return self.name
    def get_id(self):
        return self.id
    def get_types(self):
        return self.types
    def get_enums(self):
        return self.enums
    def get_functions(self):
        return self.functions
    def get_events(self):
        return self.events

global_id = 0
trace_enabled = 0

def error(text):
    print(text)
    sys.exit(-1)

def trace(text):
    if trace_enabled != 0:
        print(text)

def str2bool(v):
    if v is None:
        return False
    return v.lower() in ("yes", "true", "t", "1")

def str2int(v):
    try:
        i = int(v, 0)
        return i
    except ValueError:
        return None
    except Exception:
        return None

def get_dir_or_default(path, protocol_xml_path):
    if not path or not os.path.isdir(path):
        return os.getcwd()
    return path

def get_id():
    global global_id

    p_id = global_id
    global_id = global_id + 1
    return p_id

def reset_id():
    global global_id
    global_id = 0
    return

def create_type_from_xml(xml_type, global_scope):
    try:
        name = xml_type.get("name")
        header = xml_type.get("header")
        definition = xml_type.get("definition")
        
        # validation
        if name is None:
            raise Exception("name attribute of <type> tag must be specified")
        if header is None and definition is None:
            raise Exception("either header or definition must be specified for a <type> tag")

        # continue
        trace("parsed type: " + name)
        return TypeDefinition(name, header, definition, global_scope)
    except:
        error("could not parse protocol types")
    return None

def create_enum_from_xml(xml_enum, global_scope):
    try:
        name = xml_enum.get("name")
        values = []
        
        # validation
        if name is None:
            raise Exception("name attribute of <enum> tag must be specified")

        trace("parsing enum: " + name)
        for xml_value in xml_enum.findall('value'):
            values.append(parse_value(xml_value))
        
        # validation
        if len(values) == 0:
            raise Exception("enums must have atleast one value specified")
        
        return Enumerator(name, values, global_scope)
    except Exception as e:
        error("could not parse enum: " + str(e))
    return None

def parse_value(xml_value):
    try:
        name = xml_value.get("name")
        value = xml_value.get("value")

        # validation
        if name is None:
            raise Exception("name attribute of <value> tag must be specified")

        if value is None:
            trace("parsed enum value: " + name)
        else:
            trace("parsed enum value: " + name + " = " + value)
        return ValueDefinition(name, value)
    except Exception as e:
        error("could not parse enum value: " + str(e))
    return None

def parse_param(xml_param):
    try:
        name = xml_param.get("name")
        p_type = xml_param.get("type")
        count = xml_param.get("count")
        
        # validation
        if name is None:
            raise Exception("name attribute of <param> tag must be specified")
        if p_type is None:
            raise Exception("type attribute of <param> tag must be specified")
        
        # default to count 1
        if count is None:
            count = "1"

        trace("parsed parameter: " + name)
        return Parameter(name, p_type, count)
    except Exception as e:
        error("could not parse parameter: " + str(e))
    return None

def parse_function(xml_function):
    try:
        name = xml_function.get("name")
        sync = str2bool(xml_function.get("synchronous"))
        request_params = []
        response_params = []

        # validation
        if name is None:
            raise Exception("name attribute of <function> tag must be specified")

        trace("parsing function: " + name)
        for xml_param in xml_function.findall("request/param"):
            request_params.append(parse_param(xml_param))
        for xml_param in xml_function.findall("response/param"):
            response_params.append(parse_param(xml_param))
        return Function(name, str(get_id()), sync, request_params, response_params)
    except Exception as e:
        error("could not parse function: " + str(e))
    return None

def parse_event(xml_event):
    try:
        name = xml_event.get("name")
        params = []
        
        # validation
        if name is None:
            raise Exception("name attribute of <event> tag must be specified")

        trace("parsing event: " + name)
        for xml_param in xml_event.findall('param'):
            params.append(parse_param(xml_param))
        return Event(name, str(get_id()), params)
    except Exception as e:
        error("could not parse event: " + str(e))
    return None

def parse_protocol(global_types, global_enums, namespace, xml_protocol):
    try:
        reset_id()
        name = xml_protocol.get("name")
        p_id = xml_protocol.get("id")
        enums = copy.copy(global_enums)
        types = copy.copy(global_types)
        functions = []
        events = []

        # validation
        if name is None:
            raise Exception("name attribute of <protocol> tag must be specified")
        if p_id is None:
            raise Exception("id attribute of <protocol> tag must be specified")
        
        is_valid_id = str2int(p_id)
        if is_valid_id is None:
            raise Exception("id " + p_id + " attribute of <protocol> tag must an integer/hex string")
        if is_valid_id == 0:
            raise Exception("id 0 of <protocol> " + name + " is reserved for internal usage")
        if is_valid_id > 255:
            raise Exception("id of <protocol> " + name + " can not be higher than 255")

        trace("parsing protocol: " + name)
        for xml_enum in xml_protocol.findall('enums/enum'):
            enums.append(create_enum_from_xml(xml_enum, False))

        for xml_function in xml_protocol.findall('functions/function'):
            functions.append(parse_function(xml_function))

        for xml_event in xml_protocol.findall('events/event'):
            events.append(parse_event(xml_event))
        return Protocol(namespace, p_id, name, types, enums, functions, events)
    except Exception as e:
        error("could not parse protocol: " + str(e))
    return None

def parse_protocols(global_types, global_enums, root):
    protocols = []
    xml_protocols_header = root.find("protocols")
    if xml_protocols_header is None:
        error("could not parse protocol: missing <protocols> tag")

    namespace = xml_protocols_header.get("namespace")
    if namespace is None:
        error("could not parse protocol: namespace attribute was not defined in the <protocols> tag")
    
    trace("parsed namespace: " + namespace)
    for xml_protocol in root.findall('protocols/protocol'):
        protocols.append(parse_protocol(global_types, global_enums, namespace, xml_protocol))
    return protocols

def parse_global_types(root):
    types = []
    for xml_type in root.findall('types/type'):
        types.append(create_type_from_xml(xml_type, True))
    return types

def parse_global_enums(root):
    enums = []
    for xml_enum in root.findall('enums/enum'):
        enums.append(create_enum_from_xml(xml_enum, True))
    return enums

def parse_protocol_xml(protocol_xml_path):
    root = ET.parse(protocol_xml_path).getroot()
    global_types = parse_global_types(root)
    global_enums = parse_global_enums(root)
    protocols = parse_protocols(global_types, global_enums, root)
    return protocols

##################
# C Generator Code
##################
class CGenerator:
    def get_input_struct_name(self, protocol, func):
        return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + func.get_name().lower() + "_args"

    def get_output_struct_name(self, protocol, func):
        return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + func.get_name().lower() + "_ret"

    def get_event_struct_name(self, protocol, evt):
        return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + evt.get_name().lower() + "_event"
        
    def get_enum_name(self, protocol, enum):
        if enum.is_global():
            return protocol.get_namespace().lower() + "_" + enum.get_name().lower()
        else:
            return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + enum.get_name().lower()
        
    def get_param_typename(self, protocol, param, omit_parameter_name):
        param_typename = ""
        if param.get_enum_ref() is not None:
            param_typename = "enum " + self.get_enum_name(protocol, param.get_enum_ref())
        else:
            param_typename = param.get_typename()
        
        # overwrite string with char
        if param.get_typename() == "string":
            param_typename = "char"
        
        # handle special cases
        if param.is_output():
            param_typename = param_typename + "*"
        if int(param.get_count()) > 1:
            param_typename = param_typename + "*"

        if omit_parameter_name:
            return param_typename
        else:
            return param_typename + " "  + param.get_name()

    def get_struct_member_typename(self, protocol, param):
        param_typename = ""
        if param.get_enum_ref() is not None:
            param_typename = "enum " + self.get_enum_name(protocol, param.get_enum_ref())
        else:
            param_typename = param.get_typename()
        
        # overwrite string with char
        if param.get_typename() == "string":
            param_typename = "char"

        param_typename = param_typename + " "  + param.get_name()
        if int(param.get_count()) > 1:
            param_typename = param_typename + "[" + str(param.get_count()) + "]"
        return param_typename

    def get_parameter_string(self, protocol, params, omit_parameter_names):
        last_index = len(params) - 1
        parameter_string = ""
        for index, param in enumerate(params):
            parameter_string = parameter_string + self.get_param_typename(protocol, param, omit_parameter_names)
            if index < last_index:
                parameter_string = parameter_string + ", "
        return parameter_string

    def get_protocol_server_callback_name(self, protocol, func):
        return protocol.get_namespace() + "_" + protocol.get_name() + "_" + func.get_name() + "_callback"

    def get_protocol_client_callback_name(self, protocol, evt):
        return protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_callback"

    def get_protocol_client_event_callback_name(self, protocol, evt):
        return protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_callback"

    def get_protocol_event_prototype_name_single(self, protocol, evt, omit_parameter_names):
        evt_name = "int " + protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_single(int"
        if not omit_parameter_names:
            evt_name = evt_name + " client"
        
        if len(evt.get_params()) > 0:
            evt_name = evt_name + ", " + self.get_parameter_string(protocol, evt.get_params(), omit_parameter_names)
        return evt_name + ")"
        
    def get_protocol_event_prototype_name_all(self, protocol, evt, omit_parameter_names):
        evt_name = "int " + protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_all("
        
        if len(evt.get_params()) > 0:
            evt_name = evt_name + self.get_parameter_string(protocol, evt.get_params(), omit_parameter_names)
        else:
            evt_name = evt_name + "void"
        return evt_name + ")"

    def write_header(self, outfile):
        outfile.write("/**\n")
        outfile.write(" * This file was generated by the gracht protocol generator script. Any changes done here will be overwritten.\n")
        outfile.write(" */\n\n")
        return

    def write_header_guard_start(self, file_name, outfile):
        outfile.write("#ifndef __" + str.replace(file_name, ".", "_").upper() + "__\n")
        outfile.write("#define __" + str.replace(file_name, ".", "_").upper() + "__\n\n")
        return

    def write_header_guard_end(self, file_name, outfile):
        outfile.write("#endif //!__" + str.replace(file_name, ".", "_").upper() + "__\n")
        return

    def write_c_guard_start(self, outfile):
        outfile.write("#ifdef __cplusplus\n")
        outfile.write("extern \"C\" {\n")
        outfile.write("#endif\n")
        return

    def write_c_guard_end(self, outfile):
        outfile.write("#ifdef __cplusplus\n")
        outfile.write("}\n")
        outfile.write("#endif\n\n")
        return

    def define_shared_ids(self, protocol, outfile):
        prefix = protocol.get_namespace().upper() + "_" + protocol.get_name().upper()
        outfile.write("#define PROTOCOL_" + prefix + "_ID " + protocol.get_id() + "\n")
        outfile.write("#define PROTOCOL_" + prefix + "_FUNCTION_COUNT " + str(len(protocol.get_functions())) + "\n\n")

        for func in protocol.get_functions():
            func_prefix = prefix + "_" + func.get_name().upper()
            outfile.write("#define PROTOCOL_" + func_prefix + "_ID " + func.get_id() + "\n")
        outfile.write("\n")

        for evt in protocol.get_events():
            evt_prefix = prefix + "_EVENT_" + evt.get_name().upper()
            outfile.write("#define PROTOCOL_" + evt_prefix + "_ID " + evt.get_id() + "\n")
        outfile.write("\n")
        return

    def write_enum(self, enum_name, values, outfile):
        outfile.write("enum " + enum_name + " {\n")
        for value in values:
            if value.get_value() is None:
                outfile.write("    " + value.get_name() + ",\n")
            else:
                outfile.write("    " + value.get_name() + " = " + value.get_value() + ",\n")
        outfile.write("};\n")
        return

    def define_enums(self, protocol, outfile):
        for enum in protocol.get_enums():
            if len(enum.get_values()):
                enum_name = self.get_enum_name(protocol, enum)
                if enum.is_global():
                    outfile.write("#ifndef __" + enum_name.upper() + "_DEFINED__\n")
                    outfile.write("#define __" + enum_name.upper() + "_DEFINED__\n")
                self.write_enum(enum_name, enum.get_values(), outfile)
                if enum.is_global():
                    outfile.write("#endif //!__" + enum_name.upper() + "_DEFINED__\n\n")
        outfile.write("\n")
        return

    def write_structure(self, protocol, struct_name, params, outfile):
        outfile.write("struct " + struct_name + " {\n")
        for param in params:
            outfile.write("    " + self.get_struct_member_typename(protocol, param) + ";\n")
        outfile.write("};\n")
        return

    def define_structures(self, protocol, outfile):
        for func in protocol.get_functions():
            if len(func.get_request_params()):
                self.write_structure(protocol, self.get_input_struct_name(protocol, func), func.get_request_params(), outfile)
                outfile.write("\n")
            if len(func.get_response_params()):
                self.write_structure(protocol, self.get_output_struct_name(protocol, func), func.get_response_params(), outfile)
                outfile.write("\n")

        for evt in protocol.get_events():
            if len(evt.get_params()):
                self.write_structure(protocol, self.get_event_struct_name(protocol, evt), evt.get_params(), outfile)
                outfile.write("\n")
        outfile.write("\n")
        return

    def include_shared_header(self, protocol, outfile):
        outfile.write("#include \"" + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol.h\"\n")
        return

    def define_protocol_headers(self, protocol, outfile):
        headers_printed = []
        for type_def in protocol.get_types():
            header_name = type_def.get_header()
            if header_name and header_name not in headers_printed:
                outfile.write("#include <" + header_name + ">\n")
                headers_printed.append(header_name)
        outfile.write("\n")
        return

    def define_headers(self, headers, outfile):
        for header in headers:
            outfile.write("#include " + header + "\n")
        outfile.write("\n")
        return

    def define_types(self, protocol, outfile):    
        for type_def in protocol.get_types():
            if not type_def.get_header():
                outfile.write("typedef " + type_def.get_definition() + " " + type_def.get_name() + ";\n")
        outfile.write("\n")
        return

    def get_function_prototype(self, protocol, func, omit_parameter_names):
        function_prototype = "int " + protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + func.get_name()
        parameter_string = ""
        if func.is_synchronous():
            function_prototype = function_prototype + "_sync(gracht_client_t*"
            if not omit_parameter_names:
                function_prototype = function_prototype + " client"
            input_param_string = self.get_parameter_string(protocol, func.get_request_params(), omit_parameter_names)
            output_param_string = self.get_parameter_string(protocol, func.get_response_params(), omit_parameter_names)
            if len(func.get_request_params()) > 0 or len(func.get_response_params()) > 0:
                function_prototype = function_prototype + ", "
            if len(func.get_request_params()) > 0 and len(func.get_response_params()) > 0:
                output_param_string = ", " + output_param_string
            parameter_string = input_param_string + output_param_string
        else:
            function_prototype = function_prototype + "(gracht_client_t*"
            if not omit_parameter_names:
                function_prototype = function_prototype + " client"
            parameter_string = self.get_parameter_string(protocol, func.get_request_params(), omit_parameter_names)
            if parameter_string != "":
                function_prototype = function_prototype + ", "
        return function_prototype + parameter_string + ")"

    def define_prototypes(self, protocol, outfile):
        for func in protocol.get_functions():
            outfile.write("    " + self.get_function_prototype(protocol, func, True) + ";\n")
        outfile.write("\n")
        return

    def write_input_copy_code(self, params, outfile):
        for param in params:
            if int(param.get_count()) > 1:
                size_function = ""
                if param.get_typename() == "string":
                    size_function = "strnlen(&" + param.get_name() + "[0], " + str(param.get_count()) + " - 1) + 1" # include space for null terminator
                else:
                    size_function = "sizeof(" + param.get_typename() + ") * " + str(param.get_count())
                outfile.write("    memcpy((void*)&__input." + param.get_name() + "[0], (const void*)&" + param.get_name() + "[0], " + size_function + ");\n")
            else:
                outfile.write("    __input." + param.get_name() + " = " + param.get_name() + ";\n")
        return

    def define_function_body(self, protocol, func, outfile):
        input_struct_name = ""
        output_struct_name = ""

        # define variables
        outfile.write("    int __status;\n")
        if len(func.get_request_params()) > 0:
            input_struct_name = self.get_input_struct_name(protocol, func)
            outfile.write("    struct " + input_struct_name + " __input;\n")
        if func.is_synchronous() and len(func.get_response_params()) > 0:
            output_struct_name = self.get_output_struct_name(protocol, func)
            outfile.write("    struct " + output_struct_name + " __output;\n")
        outfile.write("\n")

        # fill in <input> structures from parameters
        self.write_input_copy_code(func.get_request_params(), outfile)

        # perform call
        outfile.write("    __status = gracht_client_invoke(client, " + protocol.get_id() + ", " + func.get_id() + ",\n")
        if len(func.get_request_params()) > 0:
            outfile.write("        &__input, sizeof(struct " + input_struct_name + "),\n")
        else:
            outfile.write("        NULL, 0,\n")
        
        if func.is_synchronous() and len(func.get_response_params()) > 0:
            outfile.write("        &__output, sizeof(struct " + output_struct_name + ")\n")
        else:
            outfile.write("        NULL, 0\n")
        outfile.write("        );\n\n")

        # if synchronous, fill output parameters
        if func.is_synchronous() and len(func.get_response_params()) > 0:
            outfile.write("    if (!__status) {\n")
            for param in func.get_response_params():
                if int(param.get_count()) > 1:
                    outfile.write("        memcpy((void*)&" + param.get_name() + "[0], (const void*)&__output." + param.get_name() + "[0], sizeof(" + param.get_typename() + ") * " + str(param.get_count()) + ");\n")
                else:
                    outfile.write("        *" + param.get_name() + " = __output." + param.get_name() + ";\n")
            outfile.write("    }\n")
        outfile.write("    return __status;\n")
        return

    def define_event_body_single(self, protocol, evt, outfile):
        input_struct_name = ""

        # define variables
        outfile.write("    int __status;\n")
        if len(evt.get_params()) > 0:
            input_struct_name = self.get_event_struct_name(protocol, evt)
            outfile.write("    struct " + input_struct_name + " __input;\n")
        outfile.write("\n")

        # fill in <input> structures from parameters
        self.write_input_copy_code(evt.get_params(), outfile)
        
        # perform call
        outfile.write("    __status = gracht_server_send_event(client, " + protocol.get_id() + ", " + evt.get_id() + ",\n")
        if len(evt.get_params()) > 0:
            outfile.write("        &__input, sizeof(struct " + input_struct_name + "));\n")
        else:
            outfile.write("        NULL, 0);\n")
        outfile.write("    return __status;\n")
        return

    def define_event_body_all(self, protocol, evt, outfile):
        input_struct_name = ""

        # define variables
        outfile.write("    int __status;\n")
        if len(evt.get_params()) > 0:
            input_struct_name = self.get_event_struct_name(protocol, evt)
            outfile.write("    struct " + input_struct_name + " __input;\n")
        outfile.write("\n")

        # fill in <input> structures from parameters
        self.write_input_copy_code(evt.get_params(), outfile)
        
        # perform call
        outfile.write("    __status = gracht_server_broadcast_event(" + protocol.get_id() + ", " + evt.get_id() + ",\n")
        if len(evt.get_params()) > 0:
            outfile.write("        &__input, sizeof(struct " + input_struct_name + "));\n")
        else:
            outfile.write("        NULL, 0);\n")
        outfile.write("    return __status;\n")
        return

    def define_functions(self, protocol, outfile):
        for func in protocol.get_functions():
            outfile.write(self.get_function_prototype(protocol, func, False) + "\n")
            outfile.write("{\n")
            self.define_function_body(protocol, func, outfile)
            outfile.write("}\n\n")
        return

    def define_events(self, protocol, outfile):
        for evt in protocol.get_events():
            outfile.write(self.get_protocol_event_prototype_name_single(protocol, evt, False) + "\n")
            outfile.write("{\n")
            self.define_event_body_single(protocol, evt, outfile)
            outfile.write("}\n\n")
            
            outfile.write(self.get_protocol_event_prototype_name_all(protocol, evt, False) + "\n")
            outfile.write("{\n")
            self.define_event_body_all(protocol, evt, outfile)
            outfile.write("}\n\n")
        return

    def write_protocol_server_callback(self, protocol, func, outfile):
        outfile.write("    void " + self.get_protocol_server_callback_name(protocol, func) + "(int")
        if len(func.get_request_params()) > 0:
            outfile.write(", struct " + self.get_input_struct_name(protocol, func) + "*")
        if len(func.get_response_params()) > 0:
            outfile.write(", struct " + self.get_output_struct_name(protocol, func) + "*")
        outfile.write(");\n")
        return

    def write_protocol_client_callback(self, protocol, func, outfile):
        if len(func.get_response_params()) > 0:
            outfile.write("void " + self.get_protocol_server_callback_name(protocol, func) + "(")
            outfile.write("struct " + self.get_output_struct_name(protocol, func) + "*")
            outfile.write(");\n")
        return

    def write_protocol_event_prototype(self, protocol, evt, outfile):
        outfile.write("    " + self.get_protocol_event_prototype_name_single(protocol, evt, True) + ";\n")
        outfile.write("    " + self.get_protocol_event_prototype_name_all(protocol, evt, True) + ";\n")
        return

    def write_protocol_event_callback(self, protocol, evt, outfile):
        outfile.write("    void " + self.get_protocol_client_event_callback_name(protocol, evt) + "(")
        if len(evt.get_params()) > 0:
            outfile.write("struct " + self.get_event_struct_name(protocol, evt) + "*")
        outfile.write(");\n")
        return

    def write_server_protocol_prototypes(self, protocol, outfile):
        if len(protocol.get_functions()) > 0:
            for func in protocol.get_functions():
                self.write_protocol_server_callback(protocol, func, outfile)
            outfile.write("\n")

        if len(protocol.get_events()) > 0:
            for evt in protocol.get_events():
                self.write_protocol_event_prototype(protocol, evt, outfile)
            outfile.write("\n")
        return
    
    def write_server_protocol(self, protocol, outfile):
        if len(protocol.get_functions()) > 0:
            function_array_name = protocol.get_namespace() + "_" + protocol.get_name() + "_functions"
            outfile.write("static gracht_protocol_function_t " + function_array_name + "[] = {\n")
            for func in protocol.get_functions():
                outfile.write("    { " + func.get_id() + ", " + self.get_protocol_server_callback_name(protocol, func) + " },\n")
            outfile.write("};\n\n")
            outfile.write("gracht_protocol_t " + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol = ")
            outfile.write("GRACHT_PROTOCOL_INIT(" + protocol.get_id() + ", " + str(len(protocol.get_functions())) + ", " + function_array_name + ");\n\n")
        return
    
    def write_protocol_server_prototype(self, protocol, outfile):
        if len(protocol.get_functions()) > 0:
            outfile.write("    extern gracht_protocol_t " + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol;\n")
        return
    
    def write_protocol_client_prototype(self, protocol, outfile):
        if len(protocol.get_events()) > 0:
            outfile.write("    extern gracht_protocol_t " + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol;\n")
        return
    
    def write_protocol_client_event_prototypes(self, protocol, outfile):
        if len(protocol.get_events()) > 0:
            for evt in protocol.get_events():
                self.write_protocol_event_callback(protocol, evt, outfile)
            outfile.write("\n")
        return
    
    def write_protocol_client(self, protocol, outfile):
        if len(protocol.get_events()) > 0:
            function_array_name = protocol.get_namespace() + "_" + protocol.get_name() + "_functions"
            outfile.write("static gracht_protocol_function_t " + function_array_name + "[] = {\n")
            for evt in protocol.get_events():
                outfile.write("    { " + evt.get_id() + ", " + self.get_protocol_client_callback_name(protocol, evt) + " },\n")
            outfile.write("};\n\n")
            outfile.write("gracht_protocol_t " + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol = ")
            outfile.write("GRACHT_PROTOCOL_INIT(" + protocol.get_id() + ", " + str(len(protocol.get_functions())) + ", " + function_array_name + ");\n\n")
        return

    def generate_shared_header(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            self.write_header(f)
            self.write_header_guard_start(file_name, f)
            self.define_protocol_headers(protocol, f)
            self.define_shared_ids(protocol, f)
            self.define_enums(protocol, f)
            self.define_structures(protocol, f)
            self.write_header_guard_end(file_name, f)
        return

    def generate_client_header(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_client.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            self.write_header(f)
            self.write_header_guard_start(file_name, f)
            self.define_headers(["<gracht/client.h>"], f)
            self.include_shared_header(protocol, f)
            self.define_types(protocol, f)
            self.write_c_guard_start(f)
            self.define_prototypes(protocol, f)
            self.write_protocol_client_event_prototypes(protocol, f)
            self.write_protocol_client_prototype(protocol, f)
            self.write_c_guard_end(f)
            self.write_header_guard_end(file_name, f)
        return

    def generate_client_impl(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_client.c"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            self.write_header(f)
            self.define_headers(["\"" + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_client.h\"", "<string.h>"], f)
            self.write_protocol_client(protocol, f)
            self.define_functions(protocol, f)
        return

    def generate_server_header(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_server.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            self.write_header(f)
            self.write_header_guard_start(file_name, f)
            self.define_headers(["<gracht/server.h>"], f)
            self.include_shared_header(protocol, f)
            self.define_types(protocol, f)
            self.write_c_guard_start(f)
            self.write_server_protocol_prototypes(protocol, f)
            self.write_protocol_server_prototype(protocol, f)
            self.write_c_guard_end(f)
            self.write_header_guard_end(file_name, f)
        return

    def generate_server_impl(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_server.c"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            self.write_header(f)
            self.define_headers(["\"" + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_server.h\"", "<string.h>"], f)
            self.write_server_protocol(protocol, f)
            self.define_events(protocol, f)
        return
    
    def generate_client_files(self, out, protocols):
        for proto in protocols:
            self.generate_shared_header(proto, out)
            self.generate_client_header(proto, out)
            self.generate_client_impl(proto, out)
        return

    def generate_server_files(self, out, protocols):
        for proto in protocols:
            self.generate_shared_header(proto, out)
            self.generate_server_header(proto, out)
            self.generate_server_impl(proto, out)
        return

##########################################
# Argument parser and orchestration code
##########################################
def main(args):
    global trace_enabled
    if args.trace:
        trace_enabled = 1
    
    output_dir = get_dir_or_default(args.out, args.protocol)
    protocols = parse_protocol_xml(args.protocol)
    generator = None

    if args.lang_c:
        generator = CGenerator()

    if generator is not None:
        if args.client:
            generator.generate_client_files(output_dir, protocols)
        if args.server:
            generator.generate_server_files(output_dir, protocols)
    return

if __name__== "__main__":
    parser = argparse.ArgumentParser(description='Optional app description')
    parser.add_argument('--protocol', type=str, help='The protocol that should be parsed')
    parser.add_argument('--out', type=str, help='Protocol files output directory')
    parser.add_argument('--client', action='store_true', help='Generate client side files')
    parser.add_argument('--server', action='store_true', help='Generate server side files')
    parser.add_argument('--lang-c', action='store_true', help='Generate c-style headers and implementation files')
    parser.add_argument('--trace', action='store_true', help='Trace the protocol parsing process to debug')
    args = parser.parse_args()
    if not args.protocol or not os.path.isfile(args.protocol):
        parser.error("a valid protocol path must be specified")
    main(args)
