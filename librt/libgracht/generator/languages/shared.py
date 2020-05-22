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
