#!/usr/bin/env python
import os, sys
import argparse
import xml.etree.ElementTree as ET

class TypeDefinition:
    def __init__(self, name, header, definition):
        self.name = name
        self.header = header
        self.definition = definition
    
    def get_name(self):
        return self.name
    def get_header(self):
        return self.header
    def get_definition(self):
        return self.definition

class ValueDefinition:
    def __init__(self, name, value):
        self.name = name
        self.value = value

    def get_name(self):
        return self.name
    def get_value(self):
        return self.value

class Enumerator:
    def __init__(self, name, values):
        self.name = name
        self.values = values

    def get_name(self):
        return self.name
    def get_values(self):
        return self.values

class Parameter:
    def __init__(self, name, typename, count):
        self.name = name
        self.typename = typename
        self.count = count
        self.type_id = 0
        self.output = False

    def set_enum(self):
        self.type_id = 1
    def set_output(self, output):
        self.output = output

    def get_name(self):
        return self.name
    def get_typename(self):
        return self.typename
    def get_count(self):
        return self.count
    def is_enum(self):
        return self.type_id == 1
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
                param.set_enum()
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

global_protocol_id = 16
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

def get_dir_or_default(path, protocol_xml_path):
    if not path or not os.path.isdir(args.protocol):
        return os.path.dirname(protocol_xml_path)
    return path

def get_protocol_id():
    global global_protocol_id

    p_id = global_protocol_id
    global_protocol_id = global_protocol_id + 1
    return p_id

def get_id():
    global global_id

    p_id = global_id
    global_id = global_id + 1
    return p_id

def reset_id():
    global global_id
    global_id = 0
    return

def create_type_from_xml(xml_type):
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
        return TypeDefinition(name, header, definition)
    except:
        error("could not parse protocol types")
    return None

def parse_protocol_types(root):
    types = []
    for xml_type in root.findall('types/type'):
        types.append(create_type_from_xml(xml_type))
    return types

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

def parse_enum(xml_enum):
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
        
        return Enumerator(name, values)
    except Exception as e:
        error("could not parse enum: " + str(e))
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
        return Function(name, get_id(), sync, request_params, response_params)
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
        return Event(name, get_id(), params)
    except Exception as e:
        error("could not parse event: " + str(e))
    return None

def parse_protocol(namespace, types, xml_protocol):
    try:
        reset_id()
        name = xml_protocol.get("name")
        enums = []
        functions = []
        events = []

        # validation
        if name is None:
            raise Exception("name attribute of <protocol> tag must be specified")

        trace("parsing protocol: " + name)
        for xml_enum in xml_protocol.findall('enums/enum'):
            enums.append(parse_enum(xml_enum))

        for xml_function in xml_protocol.findall('functions/function'):
            functions.append(parse_function(xml_function))

        for xml_event in xml_protocol.findall('events/event'):
            events.append(parse_event(xml_event))
        return Protocol(namespace, get_protocol_id(), name, types, enums, functions, events)
    except Exception as e:
        error("could not parse protocol: " + str(e))
    return None

def parse_protocols(types, root):
    protocols = []
    xml_protocols_header = root.find("protocols")
    if xml_protocols_header is None:
        error("could not parse protocol: missing <protocols> tag")

    namespace = xml_protocols_header.get("namespace")
    if namespace is None:
        error("could not parse protocol: namespace attribute was not defined in the <protocols> tag")
    
    trace("parsed namespace: " + namespace)
    for xml_protocol in root.findall('protocols/protocol'):
        protocols.append(parse_protocol(namespace, types, xml_protocol))
    return protocols

def parse_protocol_xml(protocol_xml_path):
    root = ET.parse(protocol_xml_path).getroot()
    types = parse_protocol_types(root)
    protocols = parse_protocols(types, root)
    return protocols

##################
# C Generator Code
##################
def write_header(outfile):
    outfile.write("/**\n")
    outfile.write(" * This file was generated by the wm protocol generator script. Any changes done here will be overwritten.\n")
    outfile.write(" */\n\n")
    return

def write_header_guard_start(protocol, outfile):
    define_name = protocol.get_namespace().upper() + "_" + protocol.get_name().upper()
    outfile.write("#ifndef __" + define_name + "_PROTOCOL_H__\n")
    outfile.write("#define __" + define_name + "_PROTOCOL_H__\n\n")
    return

def write_header_guard_end(protocol, outfile):
    define_name = protocol.get_namespace().upper() + "_" + protocol.get_name().upper()
    outfile.write("#endif //!__" + define_name + "_PROTOCOL_H__\n")
    return

def define_shared_ids(protocol, outfile):
    prefix = protocol.get_namespace().upper() + "_" + protocol.get_name().upper()
    outfile.write("#define PROTOCOL_" + prefix + "_ID " + str(protocol.get_id()) + "\n")
    outfile.write("#define PROTOCOL_" + prefix + "_FUNCTION_COUNT " + str(len(protocol.get_functions())) + "\n\n")

    for func in protocol.get_functions():
        func_prefix = prefix + "_" + func.get_name().upper()
        outfile.write("#define PROTOCOL_" + func_prefix + "_ID " + str(func.get_id()) + "\n")
    outfile.write("\n")

    for evt in protocol.get_events():
        evt_prefix = prefix + "_EVENT_" + evt.get_name().upper()
        outfile.write("#define PROTOCOL_" + evt_prefix + "_ID " + str(evt.get_id()) + "\n")
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
    for enum in protocol.get_enums():
        if len(enum.get_values()):
            enum_name = protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + enum.get_name().lower()
            write_enum(enum_name, enum.get_values(), outfile)
    outfile.write("\n")
    return

def get_param_typename(protocol, param):
    param_typename = ""
    if param.is_enum():
        param_typename = "enum " + protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + param.get_typename()
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

    return param_typename + " "  + param.get_name()

def get_struct_member_typename(protocol, param):
    param_typename = ""
    if param.is_enum():
        param_typename = "enum " + protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + param.get_typename()
    else:
        param_typename = param.get_typename()
    
    # overwrite string with char
    if param.get_typename() == "string":
        param_typename = "char"

    param_typename = param_typename + " "  + param.get_name()
    if int(param.get_count()) > 1:
        param_typename = param_typename + "[" + str(param.get_count()) + "]"
    return param_typename


def write_structure(protocol, struct_name, params, outfile):
    outfile.write("struct " + struct_name + " {\n")
    for param in params:
        outfile.write("    " + get_struct_member_typename(protocol, param) + ";\n")
    outfile.write("};\n")
    return

def get_input_struct_name(protocol, func):
    return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + func.get_name().lower() + "_args"

def get_output_struct_name(protocol, func):
    return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + func.get_name().lower() + "_ret"

def get_event_struct_name(protocol, evt):
    return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + evt.get_name().lower() + "_event"

def define_structures(protocol, outfile):
    for func in protocol.get_functions():
        if len(func.get_request_params()):
            write_structure(protocol, get_input_struct_name(protocol, func), func.get_request_params(), outfile)
            outfile.write("\n")
        if len(func.get_response_params()):
            write_structure(protocol, get_output_struct_name(protocol, func), func.get_response_params(), outfile)

    for evt in protocol.get_events():
        if len(evt.get_params()):
            write_structure(protocol, get_event_struct_name(protocol, evt), evt.get_params(), outfile)
    outfile.write("\n")
    return

def include_shared_header(protocol, outfile):
    outfile.write("#include \"" + protocol.get_name() + "_protocol.h\"\n")
    return

def define_protocol_headers(protocol, outfile):
    headers_printed = []
    for type_def in protocol.get_types():
        header_name = type_def.get_header()
        if header_name and header_name not in headers_printed:
            outfile.write("#include <" + header_name + ">\n")
            headers_printed.append(header_name)
    outfile.write("\n")
    return

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

def get_parameter_string(protocol, params):
    last_index = len(params) - 1
    parameter_string = ""
    for index, param in enumerate(params):
        parameter_string = parameter_string + get_param_typename(protocol, param)
        if index < last_index:
            parameter_string = parameter_string + ", "
    return parameter_string

def get_function_prototype(protocol, func):
    function_prototype = "int " + protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + func.get_name()
    parameter_string = ""
    if func.is_synchronous():
        function_prototype = function_prototype + "_sync(wm_client* client"
        input_param_string = get_parameter_string(protocol, func.get_request_params())
        output_param_string = get_parameter_string(protocol, func.get_response_params())
        if len(func.get_request_params()) > 0 or len(func.get_response_params()) > 0:
            function_prototype = function_prototype + ", "
        if len(func.get_request_params()) > 0 and len(func.get_response_params()) > 0:
            output_param_string = ", " + output_param_string
        parameter_string = input_param_string + output_param_string
    else:
        function_prototype = function_prototype + "(wm_client* client"
        parameter_string = get_parameter_string(protocol, func.get_request_params())
        if parameter_string != "":
            function_prototype = function_prototype + ", "
    return function_prototype + parameter_string + ")"

def define_prototypes(protocol, outfile):
    for func in protocol.get_functions():
        outfile.write(get_function_prototype(protocol, func) + ";\n")
    outfile.write("\n")
    return

def define_function_body(protocol, func, outfile):
    input_struct_name = ""
    output_struct_name = ""

    # define variables
    outfile.write("    int status;\n")
    if len(func.get_request_params()) > 0:
        input_struct_name = get_input_struct_name(protocol, func)
        outfile.write("    struct " + input_struct_name + " __input;\n")
    if func.is_synchronous() and len(func.get_response_params()) > 0:
        output_struct_name = get_output_struct_name(protocol, func)
        outfile.write("    struct " + output_struct_name + " __output;\n")
    outfile.write("\n")

    # fill in <input> structures from parameters
    for param in func.get_request_params():
        if int(param.get_count()) > 1:
            size_function = ""
            if param.get_typename() == "string":
                size_function = "strnlen(&" + param.get_name() + "[0], " + str(param.get_count()) + " - 1) + 1" # include space for null terminator
            else:
                size_function = "sizeof(" + param.get_typename() + ") * " + str(param.get_count())
            outfile.write("    memcpy((void*)&__input." + param.get_name() + "[0], (const void*)&" + param.get_name() + "[0], " + size_function + ");\n")
        else:
            outfile.write("    __input." + param.get_name() + " = " + param.get_name() + ";\n")

    # perform call
    outfile.write("    status = wm_client_invoke(client, " + str(protocol.get_id()) + ", " + str(func.get_id()) + "\n")
    if len(func.get_request_params()) > 0:
        outfile.write("        &__input, sizeof(struct " + input_struct_name + ")\n")
    else:
        outfile.write("        NULL, 0\n")
    
    if func.is_synchronous() and len(func.get_response_params()) > 0:
        outfile.write("        &__output, sizeof(struct " + output_struct_name + ")\n")
    else:
        outfile.write("        NULL, 0\n")
    outfile.write("        );\n\n")

    # if synchronous, fill output parameters
    if func.is_synchronous() and len(func.get_response_params()) > 0:
        outfile.write("    if (!status) {\n")
        for param in func.get_response_params():
            if int(param.get_count()) > 1:
                outfile.write("        memcpy((void*)&" + param.get_name() + "[0], (const void*)&__output." + param.get_name() + "[0], sizeof(" + param.get_typename() + ") * " + str(param.get_count()) + ");\n")
            else:
                outfile.write("        *" + param.get_name() + " = __output." + param.get_name() + ";\n")
        outfile.write("    }\n")
    outfile.write("    return status\n")
    return

def define_functions(protocol, outfile):
    for func in protocol.get_functions():
        outfile.write(get_function_prototype(protocol, func) + "\n")
        outfile.write("{\n")
        define_function_body(protocol, func, outfile)
        outfile.write("}\n\n")
    return

def get_protocol_server_callback_name(protocol, func):
    return protocol.get_namespace() + "_" + protocol.get_name() + "_" + func.get_name() + "_callback"

def define_protocol_callback(protocol, func, outfile):
    outfile.write("void " + get_protocol_server_callback_name(protocol, func) + "(int client")
    if len(func.get_request_params()) > 0:
        outfile.write(", struct " + get_input_struct_name(protocol, func) + "*")
    if len(func.get_response_params()) > 0:
        outfile.write(", struct " + get_output_struct_name(protocol, func) + "*")
    outfile.write(");\n")
    return

def define_protocol(protocol, outfile):
    function_array_name = protocol.get_namespace() + "_" + protocol.get_name() + "_functions"

    for func in protocol.get_functions():
        define_protocol_callback(protocol, func, outfile)
    outfile.write("\n")
    outfile.write("static wm_protocol_function_t " + function_array_name + "[] = {\n")
    for func in protocol.get_functions():
        outfile.write("    { " + str(func.get_id()) + ", " + get_protocol_server_callback_name(protocol, func) + " },\n")
    outfile.write("};\n\n")
    outfile.write("static wm_protocol_t " + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol = ")
    outfile.write("WM_PROTOCOL_INIT(" + str(protocol.get_id()) + ", " + str(len(protocol.get_functions())) + ", " + function_array_name + ");\n\n")
    return

def generate_shared_header(protocol, directory):
    file_name = protocol.get_name() + "_protocol.h"
    file_path = os.path.join(directory, file_name)
    with open(file_path, 'w') as f:
        write_header(f)
        write_header_guard_start(protocol, f)
        define_protocol_headers(protocol, f)
        define_shared_ids(protocol, f)
        define_enums(protocol, f)
        define_structures(protocol, f)
        write_header_guard_end(protocol, f)
    return

def generate_client_header(protocol, directory):
    file_name = protocol.get_name() + "_protocol_client.h"
    file_path = os.path.join(directory, file_name)
    with open(file_path, 'w') as f:
        write_header(f)
        write_header_guard_start(protocol, f)
        define_headers(["<libwm_client.h>"], f)
        include_shared_header(protocol, f)
        define_types(protocol, f)
        define_prototypes(protocol, f)
        write_header_guard_end(protocol, f)
    return

def generate_client_impl(protocol, directory):
    file_name = protocol.get_name() + "_protocol_client.c"
    file_path = os.path.join(directory, file_name)
    with open(file_path, 'w') as f:
        write_header(f)
        define_headers(["\"" + protocol.get_name() + "_protocol_client.h\"", "<string.h>"], f)
        define_functions(protocol, f)
    return

def generate_server_header(protocol, directory):
    file_name = protocol.get_name() + "_protocol_server.h"
    file_path = os.path.join(directory, file_name)
    with open(file_path, 'w') as f:
        write_header(f)
        write_header_guard_start(protocol, f)
        define_headers(["<libwm_server.h>"], f)
        include_shared_header(protocol, f)
        define_types(protocol, f)
        define_protocol(protocol, f)
        write_header_guard_end(protocol, f)
    return

def generate_c_files(client_dir, server_dir, protocols):
    for proto in protocols:
        generate_shared_header(proto, client_dir)
        generate_shared_header(proto, server_dir)
        
        generate_client_header(proto, client_dir)
        generate_client_impl(proto, client_dir)
        
        generate_server_header(proto, server_dir)
    return

##########################################
# Argument parser and orchestration code
##########################################
def main(args):
    global trace_enabled
    if args.trace:
        trace_enabled = 1
    
    server_dir = get_dir_or_default(args.server_dir, args.protocol)
    client_dir = get_dir_or_default(args.client_dir, args.protocol)
    protocols = parse_protocol_xml(args.protocol)
    if args.lang_c:
        generate_c_files(client_dir, server_dir, protocols)
    return

if __name__== "__main__":
    parser = argparse.ArgumentParser(description='Optional app description')
    parser.add_argument('--protocol', type=str, help='The protocol that should be parsed')
    parser.add_argument('--client-dir', type=str, help='Client protocol files output directory')
    parser.add_argument('--server-dir', type=str, help='Server protocol files output directory')
    parser.add_argument('--lang-c', action='store_true', help='Generate c-style headers and implementation files')
    parser.add_argument('--trace', action='store_true', help='Trace the protocol parsing process to debug')
    args = parser.parse_args()
    if not args.protocol or not os.path.isfile(args.protocol):
        parser.error("a valid protocol path must be specified")
    main(args)
