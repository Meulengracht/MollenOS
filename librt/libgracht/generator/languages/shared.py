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
    def __init__(self, name, typename, subtype="void*", count="1", values=[], default_value=None, hidden=False):
        self.name = name
        self.typename = typename
        self.subtype = subtype
        self.count = count
        self.output = False
        self.enum_ref = None
        self.values = values
        self.default_value = default_value
        self.hidden = hidden

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

    def get_default_value(self):
        return self.default_value

    def is_value(self):
        return not self.is_string() and not self.is_buffer() and not self.is_shm() and not self.is_array()

    def is_array(self):
        return self.get_count() != "1"

    def is_string(self):
        return self.typename == "string"

    def is_buffer(self):
        return self.typename == "buffer"

    def is_shm(self):
        return self.typename == "shm"

    def is_output(self):
        return self.output

    def is_hidden(self):
        return self.hidden


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
    def __init__(self, name, id, request_params, response_params):
        self.name = name
        self.id = id
        self.request_params = request_params
        self.response_params = response_params

    def get_name(self):
        return self.name

    def get_id(self):
        return self.id

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
        self.imports = []
        self.resolve_all_types()

    def resolve_param_enum_types(self, param):
        for enum in self.enums:
            if enum.get_name().lower() == param.get_typename().lower():
                param.set_enum(enum)
        return

    def resolve_type(self, param):
        value_type = [x for x in self.types if x.get_name() == param.get_typename() or
                      (param.is_buffer() and x.get_name() == param.get_subtype())]
        if value_type:
            self.imports.append(value_type[0].get_header())

    def resolve_all_types(self):
        for func in self.functions:
            for param in func.get_request_params():
                self.resolve_type(param)
                self.resolve_param_enum_types(param)
            for param in func.get_response_params():
                self.resolve_type(param)
                self.resolve_param_enum_types(param)
                param.set_output(True)
        for evt in self.events:
            for param in evt.get_params():
                self.resolve_type(param)
                self.resolve_param_enum_types(param)

        unique_imports = set(self.imports)
        self.imports = list(unique_imports)
        return

    def get_namespace(self):
        return self.namespace

    def get_name(self):
        return self.name

    def get_id(self):
        return self.id

    def get_types(self):
        return self.types

    def get_imports(self):
        return self.imports

    def get_enums(self):
        return self.enums

    def get_functions(self):
        return self.functions

    def get_events(self):
        return self.events
