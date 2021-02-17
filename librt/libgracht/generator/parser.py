#!/usr/bin/env python
import argparse
import copy
import os
import sys
import xml.etree.ElementTree as ET

from languages.langc import CGenerator
from languages.shared import *

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


def get_dir_or_default(path):
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


def parse_param(xml_param, is_response):
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

        if is_valid_int(count):
            if int(count) < 1:
                raise Exception(name + ": count can not be less than 1")

            if int(count) > 1 and (p_type == "buffer" or p_type == "shm"):
                raise Exception(name + ": count is not supported for buffer or shm types")

        if is_response and p_type == "shm":
            raise Exception(name + ": shm response arguments not supported")

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
        request_params = []
        response_params = []

        trace("parsing function: " + name)

        # validation
        if name is None:
            raise Exception("name attribute of <function> tag must be specified")
        if name == "subscribe" or name == "unsubscribe":
            raise Exception("subscribe and unsubscribe are reserved function names")

        # parse parameters
        for xml_param in xml_function.findall("request/param"):
            request_params.append(parse_param(xml_param, False))
        for xml_param in xml_function.findall("response/param"):
            response_params.append(parse_param(xml_param, True))

        return Function(name, str(get_id()), request_params, response_params)
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
            params.append(parse_param(xml_param, False))
        return Event(name, str(get_id()), params)
    except Exception as e:
        error("could not parse event: " + str(e))
    return None


def parse_protocol(xml_dir, global_types, global_enums, namespace, xml_protocol):
    try:
        enums = copy.copy(global_enums)
        types = copy.copy(global_types)
        functions = []
        events = []
        name = xml_protocol.get("name")
        p_id = xml_protocol.get("id")

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
        types.extend(parse_types(xml_dir, xml_protocol, False))
        enums.extend(parse_enums(xml_dir, xml_protocol, False))

        for xml_function in xml_protocol.findall('functions/function'):
            functions.append(parse_function(xml_function))

        for xml_event in xml_protocol.findall('events/event'):
            events.append(parse_event(xml_event))
        return Protocol(namespace, p_id, name, types, enums, functions, events)
    except Exception as e:
        error("could not parse protocol: " + str(e))


def parse_protocols(xml_dir, global_types, global_enums, root):
    protocols = []
    xml_protocols_header = root.find("protocols")
    if xml_protocols_header is None:
        error("could not parse protocol: missing <protocols> tag")

    namespace = xml_protocols_header.get("namespace")
    if namespace is None:
        error("could not parse protocol: namespace attribute was not defined in the <protocols> tag")

    trace("parsed namespace: " + namespace)
    for xml_protocol in root.findall('protocols/protocol'):
        reset_id()
        protocol_src = xml_protocol.get("src")
        if protocol_src is not None:
            protocol_path = os.path.join(xml_dir, protocol_src)
            protocol_et = ET.parse(protocol_path)
            if protocol_et is None:
                continue
            protocols.append(parse_protocol(os.path.dirname(protocol_path), global_types, global_enums,
                                            namespace, protocol_et.getroot().find('protocol')))
        else:
            protocols.append(parse_protocol(xml_dir, global_types, global_enums, namespace, xml_protocol))
    return protocols


def parse_types(xml_dir, root, global_scope):
    types = []
    for xml_types in root.findall('types'):
        types_src = xml_types.get("src")
        if types_src is not None:
            types_path = os.path.join(xml_dir, types_src)
            types_et = ET.parse(types_path)
            if types_et is None:
                continue
            types.extend(parse_types(os.path.dirname(types_path), types_et.getroot(), global_scope))
        else:
            for xml_type in xml_types.findall('type'):
                types.append(create_type_from_xml(xml_type, global_scope))
    return types


def parse_enums(xml_dir, root, global_scope):
    enums = []
    for xml_enums in root.findall('enums'):
        enums_src = xml_enums.get("src")
        if enums_src is not None:
            enums_path = os.path.join(xml_dir, enums_src)
            enums_et = ET.parse(enums_path)
            if enums_et is None:
                continue
            enums.extend(parse_enums(os.path.dirname(enums_path), enums_et.getroot(), global_scope))
        else:
            for xml_enum in xml_enums.findall('enum'):
                enums.append(create_enum_from_xml(xml_enum, global_scope))
    return enums


def parse_protocol_xml(protocol_xml_path):
    root = ET.parse(protocol_xml_path).getroot()
    if root is None:
        return []

    xml_dir_path = os.path.dirname(os.path.abspath(protocol_xml_path))
    global_types = parse_types(xml_dir_path, root, True)
    global_enums = parse_enums(xml_dir_path, root, True)
    protocols = parse_protocols(xml_dir_path, global_types, global_enums, root)
    return protocols


##########################################
# Argument parser and orchestration code
##########################################
def main(args):
    global trace_enabled
    if args.trace:
        trace_enabled = 1

    output_dir = get_dir_or_default(args.out)
    protocols = parse_protocol_xml(args.protocol)
    include_protocols = []
    generator = None

    if args.include:
        include_protocols = args.include.split(',')

    if args.lang_c:
        generator = CGenerator()

    if generator is not None:
        generator.generate_shared_files(output_dir, protocols, include_protocols)
        if args.client:
            generator.generate_client_files(output_dir, protocols, include_protocols)
        if args.server:
            generator.generate_server_files(output_dir, protocols, include_protocols)
    return


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Optional app description')
    parser.add_argument('--protocol', type=str, help='The protocol that should be parsed')
    parser.add_argument('--include', type=str,
                        help='The protocols that should be generated from the file, comma-seperated list, default is all')
    parser.add_argument('--out', type=str, help='Protocol files output directory')
    parser.add_argument('--client', action='store_true', help='Generate client side files')
    parser.add_argument('--server', action='store_true', help='Generate server side files')
    parser.add_argument('--lang-c', action='store_true', help='Generate c-style headers and implementation files')
    parser.add_argument('--trace', action='store_true', help='Trace the protocol parsing process to debug')
    args = parser.parse_args()
    if not args.protocol or not os.path.isfile(args.protocol):
        parser.error("a valid protocol path must be specified")
    main(args)
