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
    def __init__(self, name, typename, subtype="void*", count = "1", values = []):
        self.name = name
        self.typename = typename
        self.subtype = subtype
        self.count = count
        self.output = False
        self.enum_ref = None
        self.values = values

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
    def get_subtype(self):
        return self.subtype
    def get_values(self):
        return self.values
    def is_value(self):
        return not self.is_string() and not self.is_buffer() and not self.is_shm()
    def is_string(self):
        return self.typename == "string"
    def is_buffer(self):
        return self.typename == "buffer"
    def is_shm(self):
        return self.typename == "shm"
    def is_output(self):
        return self.output
    def has_length_component(self):
        if self.is_buffer() or self.is_shm():
            return self.subtype == "void*"
        if self.is_string() and self.is_output():
            return True
        return False

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
    def __init__(self, name, id, is_async, request_params, response_params):
        self.name = name
        self.id = id
        self.synchronous = not is_async
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

def is_valid_int(s):
    try: 
        int(s)
        return True
    except ValueError:
        return False

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
        subtype = xml_param.get("subtype")
        values = []
        
        # validation
        if name is None:
            raise Exception("name attribute of <param> tag must be specified")
        if p_type is None:
            raise Exception("type attribute of <param> tag must be specified")
        
        # set default parameters for optional values
        if count is None:
            count = "1"
        if subtype is None:
            subtype = "void*"

        if not is_valid_int(count):
            raise Exception("count is not a valid integer value")
        
        if int(count) < 1:
            raise Exception("count can not be less than 1")
        
        if int(count) > 1 and (p_type == "buffer" or p_type == "shm"):
            raise Exception("count is not supported for buffer or shm types")

        trace("parsing parameter values: " + name)
        for xml_value in xml_param.findall('value'):
            values.append(parse_value(xml_value))
        
        trace("parsed parameter: " + name)
        return Parameter(name, p_type, subtype, count, values)
    except Exception as e:
        error("could not parse parameter: " + str(e))
    return None

def parse_function(xml_function):
    try:
        name = xml_function.get("name")
        is_async = "async" in xml_function.keys()
        request_params = []
        response_params = []

        # validation
        if name is None:
            raise Exception("name attribute of <function> tag must be specified")
        
        trace("parsing function: " + name)
        trace("is async:" + str(is_async))

        for xml_param in xml_function.findall("request/param"):
            request_params.append(parse_param(xml_param))
        for xml_param in xml_function.findall("response/param"):
            response_params.append(parse_param(xml_param))
        return Function(name, str(get_id()), is_async, request_params, response_params)
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

class CONST(object):
    __slots__ = ()
    TYPENAME_CASE_SIZEOF = 0
    TYPENAME_CASE_FUNCTION_CALL = 1
    TYPENAME_CASE_FUNCTION_RESPONSE = 2
    TYPENAME_CASE_MEMBER = 3

##################
# C Generator Code
##################
class CGenerator:
    def get_input_struct_name(self, protocol, func):
        return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + func.get_name().lower() + "_args"

    def get_event_struct_name(self, protocol, evt):
        return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + evt.get_name().lower() + "_event"
        
    def get_enum_name(self, protocol, enum):
        if enum.is_global():
            return protocol.get_namespace().lower() + "_" + enum.get_name().lower()
        else:
            return protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + enum.get_name().lower()
        
    def get_param_typename(self, protocol, param, case):
        param_typename = ""
        
        # resolve enums and structs
        if param.get_enum_ref() is not None:
            param_typename = "enum " + self.get_enum_name(protocol, param.get_enum_ref())
        else:
            param_typename = param.get_typename()
        
        # resolve special types
        if not param.is_value():
            if param.is_string():
                param_typename = "char"
            elif param.is_buffer():
                param_typename = param.get_subtype()
        
        # format parameter, unfortunately there are 5 cases to do this
        if case == CONST.TYPENAME_CASE_SIZEOF:
            if int(param.get_count()) > 1:
                param_typename = param_typename + " * " + param.get_count()
            return param_typename
        
        elif case == CONST.TYPENAME_CASE_FUNCTION_CALL:
            param_name = param.get_name()
            if not param.is_value() and not param_typename.endswith("*"):
                param_typename = param_typename + "*"
            if param.is_output():
                if param.is_value():
                    param_typename = param_typename + "*"
                param_name = param_name + "_out"
            if int(param.get_count()) > 1:
                param_typename = param_typename + "*"
            return param_typename + " "  + param_name

        elif case == CONST.TYPENAME_CASE_FUNCTION_RESPONSE:
            if not param.is_value() and not param_typename.endswith("*"):
                param_typename = param_typename + "*"
            if int(param.get_count()) > 1:
                param_typename = param_typename + "*"
            return param_typename + " "  + param.get_name()
        
        elif case == CONST.TYPENAME_CASE_MEMBER:
            if not param.is_value() and not param_typename.endswith("*"):
                param_typename = param_typename + "*"
            param_typename = param_typename + " "  + param.get_name()
            if int(param.get_count()) > 1:
                param_typename = param_typename + "[" + str(param.get_count()) + "]"
            return param_typename
        return param_typename

    def get_parameter_string(self, protocol, params, case):
        last_index = len(params) - 1
        parameter_string = ""
        for index, param in enumerate(params):
            parameter_string = parameter_string + self.get_param_typename(protocol, param, case)
            if param.has_length_component() and (case != CONST.TYPENAME_CASE_FUNCTION_RESPONSE or not param.is_string()):
                length_param = self.get_param_typename(protocol, Parameter(param.get_name() + "_length", "size_t"), case)
                parameter_string = parameter_string + ", " + length_param

            if index < last_index:
                parameter_string = parameter_string + ", "
        return parameter_string

    def get_protocol_server_response_name(self, protocol, func):
        return protocol.get_namespace() + "_" + protocol.get_name() + "_" + func.get_name() + "_response"
    
    def get_protocol_server_callback_name(self, protocol, func):
        return protocol.get_namespace() + "_" + protocol.get_name() + "_" + func.get_name() + "_callback"

    def get_protocol_client_callback_name(self, protocol, evt):
        return protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_callback"

    def get_protocol_client_event_callback_name(self, protocol, evt):
        return protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_callback"

    def get_protocol_event_prototype_name_single(self, protocol, evt, case):
        evt_client_param = self.get_param_typename(protocol, Parameter("client", "int"), case)
        evt_name = "int " + protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_single(" + evt_client_param
        
        if len(evt.get_params()) > 0:
            evt_name = evt_name + ", " + self.get_parameter_string(protocol, evt.get_params(), case)
        return evt_name + ")"
        
    def get_protocol_event_prototype_name_all(self, protocol, evt, case):
        evt_name = "int " + protocol.get_namespace() + "_" + protocol.get_name() + "_event_" + evt.get_name() + "_all("
        
        if len(evt.get_params()) > 0:
            evt_name = evt_name + self.get_parameter_string(protocol, evt.get_params(), case)
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

    def define_message_sizes(self, protocol, outfile):
        prefix = protocol.get_namespace().upper() + "_" + protocol.get_name().upper()
        if len(protocol.get_functions()) > 0:
            for func in protocol.get_functions():
                func_prefix = prefix + "_" + func.get_name().upper()
                func_params_all = func.get_request_params() + func.get_response_params()
                outfile.write("#define PROTOCOL_" + func_prefix + "_SIZE " + self.get_message_size_string(func_params_all) + "\n")
            outfile.write("\n")

        if len(protocol.get_events()) > 0:
            for evt in protocol.get_events():
                evt_prefix = prefix + "_EVENT_" + evt.get_name().upper()
                outfile.write("#define PROTOCOL_" + evt_prefix + "_SIZE " + self.get_message_size_string(evt.get_params()) + "\n")
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
        if len(protocol.get_enums()) > 0:
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

    def write_param_values(self, protocol, func, param, outfile):
        for value in param.get_values():
            outfile.write("#define " + protocol.get_namespace().upper() + "_" + protocol.get_name().upper() + "_" + func.get_name().upper() + "_" + param.get_name().upper() + "_" + value.get_name() + " " + value.get_value() + "\n")
        outfile.write("\n")
        return

    def write_structure(self, protocol, struct_name, params, case, outfile):
        outfile.write("GRACHT_STRUCT(" + struct_name + ", {\n")
        for param in params:
            outfile.write("    " + self.get_param_typename(protocol, param, case) + ";\n")
        outfile.write("});\n")
        return

    def define_param_values(self, protocol, func, outfile):
        for param in func.get_request_params():
            if len(param.get_values()) > 0:
                self.write_param_values(protocol, func, param, outfile)
        for param in func.get_response_params():
            if len(param.get_values()) > 0:
                self.write_param_values(protocol, func, param, outfile)

    def define_protocol_values(self, protocol, outfile):
        for func in protocol.get_functions():
            self.define_param_values(protocol, func, outfile)

    def define_structures(self, protocol, outfile):
        for func in protocol.get_functions():
            if len(func.get_request_params()):
                self.write_structure(protocol, self.get_input_struct_name(protocol, func), func.get_request_params(), CONST.TYPENAME_CASE_MEMBER, outfile)
                outfile.write("\n")

        for evt in protocol.get_events():
            if len(evt.get_params()):
                self.write_structure(protocol, self.get_event_struct_name(protocol, evt), evt.get_params(), CONST.TYPENAME_CASE_MEMBER, outfile)
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

    def get_server_callback_prototype(self, protocol, func):
        function_prototype = "void " + self.get_protocol_server_callback_name(protocol, func) + "("
        function_message_param = self.get_param_typename(protocol, Parameter("message", "struct gracht_recv_message*"), CONST.TYPENAME_CASE_FUNCTION_RESPONSE)
        parameter_string = function_message_param
        if len(func.get_request_params()) > 0:
            parameter_string = parameter_string + ", struct " + self.get_input_struct_name(protocol, func) + "*"
        return function_prototype + parameter_string + ")"

    def get_response_prototype(self, protocol, func, case):
        function_prototype = "int " + self.get_protocol_server_response_name(protocol, func) + "("
        function_message_param = self.get_param_typename(protocol, Parameter("message", "struct gracht_recv_message*"), case)
        parameter_string = function_message_param + ", "
        parameter_string = parameter_string + self.get_parameter_string(protocol, func.get_response_params(), case)
        return function_prototype + parameter_string + ")"

    def get_function_prototype(self, protocol, func, case):
        function_prototype = "int " + protocol.get_namespace().lower() + "_" + protocol.get_name().lower() + "_" + func.get_name()
        function_client_param = self.get_param_typename(protocol, Parameter("client", "gracht_client_t*"), case)
        function_context_param = self.get_param_typename(protocol, Parameter("context", "void*"), case)
        parameter_string = ""
        
        function_prototype = function_prototype + "(" + function_client_param + ", " + function_context_param
        if func.is_synchronous():
            input_param_string = self.get_parameter_string(protocol, func.get_request_params(), case)
            output_param_string = self.get_parameter_string(protocol, func.get_response_params(), case)
            if len(func.get_request_params()) > 0 or len(func.get_response_params()) > 0:
                function_prototype = function_prototype + ", "
            if len(func.get_request_params()) > 0 and len(func.get_response_params()) > 0:
                output_param_string = ", " + output_param_string
            parameter_string = input_param_string + output_param_string
        else:
            parameter_string = self.get_parameter_string(protocol, func.get_request_params(), case)
            if parameter_string != "":
                function_prototype = function_prototype + ", "
        return function_prototype + parameter_string + ")"

    def define_prototypes(self, protocol, outfile):
        for func in protocol.get_functions():
            outfile.write("    " + self.get_function_prototype(protocol, func, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        outfile.write("\n")
        return

    def get_size_function(self, protocol, param, is_response):
        if param.has_length_component() and (not is_response or not param.is_string()):
            return param.get_name() + "_length"
        elif param.is_string():
            return "((" + param.get_name() + " == NULL) ? 0 : strlen(&" + param.get_name() + "[0]))"
        return "sizeof(" + self.get_param_typename(protocol, param, CONST.TYPENAME_CASE_SIZEOF) + ")"

    def get_message_flags_func(self, func):
        if func.is_synchronous():
            return "0"
        return "MESSAGE_FLAG_ASYNC"

    def get_message_size_string(self, params):
        message_size = "sizeof(struct gracht_message)"
        if len(params) > 0:
            message_size = message_size + " + (" + str(len(params)) + " * sizeof(struct gracht_param))"
        return message_size
    
    def define_message_struct(self, protocol, action_id, params_in, params_out, flags, is_response, outfile):
        params_all = params_in + params_out

        # define variables
        if len(params_all) > 0:
            outfile.write("    struct {\n")
            outfile.write("        struct gracht_message_header __base;\n")
            outfile.write("        struct gracht_param          __params[" + str(len(params_all)) + "];\n")
            outfile.write("    } __message = { .__base = { \n")
            outfile.write("        .length = sizeof(struct gracht_message)")
            outfile.write(" + (" + str(len(params_in)) + " * sizeof(struct gracht_param))")
            for param in params_in:
                if not param.is_value():
                    size_function = self.get_size_function(protocol, param, is_response)
                    outfile.write(" + " + size_function)
        else:
            outfile.write("    struct gracht_message __message = {\n")
            outfile.write("        .length = sizeof(struct gracht_message)")
        outfile.write(",\n")

        outfile.write("        .param_in = " + str(len(params_in)) + ",\n")
        outfile.write("        .param_out = " + str(len(params_out)) + ",\n")
        outfile.write("        .flags = " + flags + ",\n")
        outfile.write("        .protocol = " + str(protocol.get_id()) + ",\n")
        outfile.write("        .action = " + str(action_id))
        if len(params_all) > 0:
            outfile.write("\n    }, .__params = {\n")
            for index, param in enumerate(params_in):
                size_function = self.get_size_function(protocol, param, is_response)
                if param.is_value():
                    outfile.write("            { .type = GRACHT_PARAM_VALUE, .data.value = (size_t)" + param.get_name() + ", .length = " + size_function + " }")
                elif param.is_buffer() or param.is_string():
                    outfile.write("            { .type = GRACHT_PARAM_BUFFER, .data.buffer = " + param.get_name() + ", .length = " + size_function + " }")
                elif param.is_shm():
                    outfile.write("            { .type = GRACHT_PARAM_SHM, .data.buffer = " + param.get_name() + ", .length = " + size_function + " }")
                
                if index + 1 < len(params_all):
                    outfile.write(",\n")
                else:
                    outfile.write("\n")
            for index, param in enumerate(params_out):
                size_function = self.get_size_function(protocol, param, is_response)
                buffer_variable = param.get_name() + "_out"
                if "MESSAGE_FLAG_ASYNC" in flags:
                    buffer_variable = "NULL"

                if param.is_value():
                    outfile.write("            { .type = GRACHT_PARAM_VALUE, .data.buffer = " + buffer_variable + ", .length = " + size_function + " }")
                elif param.is_buffer() or param.is_string():
                    outfile.write("            { .type = GRACHT_PARAM_BUFFER, .data.buffer = " + buffer_variable + ", .length = " + size_function + " }")
                elif param.is_shm():
                    outfile.write("            { .type = GRACHT_PARAM_SHM, .data.buffer = " + buffer_variable + ", .length = " + size_function + " }")
                
                if index + 1 < len(params_out):
                    outfile.write(",\n")
                else:
                    outfile.write("\n")
            outfile.write("        }")
        outfile.write("\n    };\n\n")
        return

    def define_function_body(self, protocol, func, outfile):
        flags = self.get_message_flags_func(func)
        self.define_message_struct(protocol, func.get_id(), func.get_request_params(), func.get_response_params(), flags, False, outfile)
        outfile.write("    return gracht_client_invoke(client, (struct gracht_message*)&__message, context);\n")
        return

    def define_event_body_single(self, protocol, evt, outfile):
        self.define_message_struct(protocol, evt.get_id(), evt.get_params(), [], "0", False, outfile)
        outfile.write("    return gracht_server_send_event(client, (struct gracht_message*)&__message, 0);\n")
        return

    def define_event_body_all(self, protocol, evt, outfile):
        self.define_message_struct(protocol, evt.get_id(), evt.get_params(), [], "0", False, outfile)
        outfile.write("    return gracht_server_broadcast_event((struct gracht_message*)&__message, 0);\n")
        return

    def define_response_body(self, protocol, func, flags, outfile):
        self.define_message_struct(protocol, func.get_id(), func.get_response_params(), [], flags, True, outfile)
        outfile.write("    return gracht_server_respond(message, (struct gracht_message*)&__message);\n")
        return
    
    def define_functions(self, protocol, outfile):
        for func in protocol.get_functions():
            outfile.write(self.get_function_prototype(protocol, func, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
            outfile.write("{\n")
            self.define_function_body(protocol, func, outfile)
            outfile.write("}\n\n")
        return

    def define_server_responses(self, protocol, outfile):
        for func in protocol.get_functions():
            if len(func.get_response_params()) > 0:
                outfile.write(self.get_response_prototype(protocol, func, CONST.TYPENAME_CASE_FUNCTION_RESPONSE) + "\n")
                outfile.write("{\n")
                self.define_response_body(protocol, func, "0", outfile)
                outfile.write("}\n\n")

    def define_events(self, protocol, outfile):
        for evt in protocol.get_events():
            outfile.write(self.get_protocol_event_prototype_name_single(protocol, evt, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
            outfile.write("{\n")
            self.define_event_body_single(protocol, evt, outfile)
            outfile.write("}\n\n")
            
            outfile.write(self.get_protocol_event_prototype_name_all(protocol, evt, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
            outfile.write("{\n")
            self.define_event_body_all(protocol, evt, outfile)
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
        outfile.write("    " + self.get_protocol_event_prototype_name_single(protocol, evt, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        outfile.write("    " + self.get_protocol_event_prototype_name_all(protocol, evt, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
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
    
    def write_client_protocol_prototype(self, protocol, outfile):
        if len(protocol.get_events()) > 0:
            outfile.write("    extern gracht_protocol_t " + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol;\n")
        return

    def write_server_protocol_prototype(self, protocol, outfile):
        if len(protocol.get_functions()) > 0:
            outfile.write("    extern gracht_protocol_t " + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol;\n")
        return
    
    def write_client_protocol_prototypes(self, protocol, outfile):
        if len(protocol.get_events()) > 0:
            for evt in protocol.get_events():
                self.write_protocol_event_callback(protocol, evt, outfile)
            outfile.write("\n")
        return
    
    def write_client_protocol(self, protocol, outfile):
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
            self.define_headers(["<gracht/types.h>"], f)
            self.define_protocol_headers(protocol, f)
            self.define_shared_ids(protocol, f)
            self.define_message_sizes(protocol, f)
            self.define_enums(protocol, f)
            self.define_protocol_values(protocol, f)
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
            self.write_client_protocol_prototypes(protocol, f)
            self.write_client_protocol_prototype(protocol, f)
            self.write_c_guard_end(f)
            self.write_header_guard_end(file_name, f)
        return

    def generate_client_impl(self, protocol, directory):
        file_name = protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_client.c"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            self.write_header(f)
            self.define_headers(["\"" + protocol.get_namespace() + "_" + protocol.get_name() + "_protocol_client.h\"", "<string.h>"], f)
            self.write_client_protocol(protocol, f)
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
            self.write_server_protocol_prototype(protocol, f)
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
            self.define_server_responses(protocol, f)
            self.define_events(protocol, f)
        return
    
    def generate_client_files(self, out, protocols, include_protocols):
        for proto in protocols:
            if (len(include_protocols) == 0) or (proto.get_name() in include_protocols):
                self.generate_shared_header(proto, out)
                self.generate_client_header(proto, out)
                self.generate_client_impl(proto, out)
        return

    def generate_server_files(self, out, protocols, include_protocols):
        for proto in protocols:
            if (len(include_protocols) == 0) or (proto.get_name() in include_protocols):
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
    include_protocols = []
    generator = None
    
    if args.include:
        include_protocols = args.include.split(',')

    if args.lang_c:
        generator = CGenerator()

    if generator is not None:
        if args.client:
            generator.generate_client_files(output_dir, protocols, include_protocols)
        if args.server:
            generator.generate_server_files(output_dir, protocols, include_protocols)
    return

if __name__== "__main__":
    parser = argparse.ArgumentParser(description='Optional app description')
    parser.add_argument('--protocol', type=str, help='The protocol that should be parsed')
    parser.add_argument('--include', type=str, help='The protocols that should be generated from the file, comma-seperated list, default is all')
    parser.add_argument('--out', type=str, help='Protocol files output directory')
    parser.add_argument('--client', action='store_true', help='Generate client side files')
    parser.add_argument('--server', action='store_true', help='Generate server side files')
    parser.add_argument('--lang-c', action='store_true', help='Generate c-style headers and implementation files')
    parser.add_argument('--trace', action='store_true', help='Trace the protocol parsing process to debug')
    args = parser.parse_args()
    if not args.protocol or not os.path.isfile(args.protocol):
        parser.error("a valid protocol path must be specified")
    main(args)
