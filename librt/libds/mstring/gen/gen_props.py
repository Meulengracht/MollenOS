#!/bin/python3
# We download the following files:
# - https://unicode.org/Public/UNIDATA/extracted/DerivedNumericType.txt
# - https://unicode.org/Public/UNIDATA/extracted/DerivedNumericValues.txt
# and then use those to generate a way of checking number information

from urllib.request import urlopen
import argparse


def get_data(url):
    """
    Download the data from the given url.
    """
    return urlopen(url).read().decode("utf-8")


def download_props_table():
    """
    Download the properties tables data from the unicode.org website.
    """
    props = get_data("https://unicode.org/Public/UNIDATA/PropList.txt")
    return props


def parse_value_selector(s):
    s = s.strip(";")
    if ".." in s:
        parts = s.split("..")
        start = int(parts[0], 16)
        end = int(parts[1], 16)
        return start, end
    else:
        v = int(s, 16)
        return v, v


def add_string_list(s, c):
    types = s.split("|")
    if c in types:
        return s
    return s + "|" + c


def parse_props_table(props_table):
    """
    Parse the unicode properties.

    We need the range, the type and the category
    0009..000D    ; White_Space # Cc   [5] <control-0009>..<control-000D>
    """
    props_map = {}

    for line in props_table.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue

        parts = list(filter(None, line.split(" ")))
        if len(parts) < 3:
            continue

        # 0 => range
        # 1 => ';' ignore
        # 2 => type
        # 3 => '#' ignore
        # 4 => category
        vs, ve = parse_value_selector(parts[0].strip())
        t = parts[2].strip()
        category = parts[4].strip()

        for i in range(vs, ve+1):
            if i in props_map:
                vals = props_map[i]
                vals["type"] = add_string_list(vals["type"], t)
                vals["category"] = add_string_list(vals["category"], category)
                props_map[i] = vals
            else:
                props_map[i] = {
                    "code": vs,
                    "type": t,
                    "category": category
                }

    # sort mappings by key
    descs = sorted(props_map.items(), key=lambda x: x[0])
    return descs


def has_category(cats, find):
    tokens = find.split("|")
    for token in tokens:
        if token in cats:
            return True
        if "*" in token:
            token = token[:-1]
            for cat in cats:
                if cat.startswith(token):
                    return True
    return False


# Uppercase  = Lu + Other_Uppercase
def is_uppercase(cats, types):
    if "Lu" in cats:
        return True
    elif "Other_Uppercase" in types:
        return True
    return False


# Lowercase  = Ll + Other_Lowercase
def is_lowercase(cats, types):
    if "Ll" in cats:
        return True
    elif "Other_Lowercase" in types:
        return True
    return False


# Alphabetic = Uppercase + Lowercase + Lt + Lm + Lo + Nl + Other_Alphabetic
def is_alpha(cats, types):
    if is_lowercase(cats, types) or is_uppercase(cats, types):
        return True
    elif has_category(cats, "Lt|Lm|Lo|Nl"):
        return True
    elif "Other_Alphabetic" in types:
        return True
    return False


def code_in_ranges(code, ranges):
    tokens = ranges.split("|")
    for token in tokens:
        vs, ve = parse_value_selector(token)
        if vs <= code <= ve:
            return True
    return False


def add_enum(enums, enum):
    if enums == "":
        return enum
    return enums + "|" + enum


def determine_enum_props(code, cats, types):
    """
    __UNICODE_PROP_WHITESPACE  = property White_Space
    __UNICODE_PROP_LOWERCASE   = property Lowercase
    __UNICODE_PROP_UPPERCASE   = property Uppercase
    __UNICODE_PROP_TITLECASE   = category Lt
    __UNICODE_PROP_LETTER      = property Alphabetic
    __UNICODE_PROP_DIGIT       = category Nd
    __UNICODE_PROP_GRAPHIC     = categories L*|N*|S*|P*
    __UNICODE_PROP_PRINTABLE   = __UNICODE_PROP_GRAPHIC|__UNICODE_PROP_WHITESPACE
    __UNICODE_PROP_ISOCONTROL  = 0000..001F + 007F..009F
    __UNICODE_PROP_PUNCTUATION = category P*
    __UNICODE_PROP_SYMBOL      = category S*
    __UNICODE_PROP_HEXDIGIT    = 0030..0039 + 0041..0046 + 0061..0066
    __UNICODE_PROP_BLANK       = category Zs + 0009
    __UNICODE_PROP_ASCII       = 0000..007F
    """
    enums = ""
    if "White_Space" in types:
        enums = add_enum(enums, "__UNICODE_PROP_WHITESPACE")
    if is_lowercase(cats, types):
        enums = add_enum(enums, "__UNICODE_PROP_LOWERCASE")
    if is_uppercase(cats, types):
        enums = add_enum(enums, "__UNICODE_PROP_UPPERCASE")
    if has_category(cats, "Lt"):
        enums = add_enum(enums, "__UNICODE_PROP_TITLECASE")
    if is_alpha(cats, types):
        enums = add_enum(enums, "__UNICODE_PROP_LETTER")
    if has_category(cats, "L*|N*|S*|P*"):
        enums = add_enum(enums, "__UNICODE_PROP_GRAPHIC")
    if code_in_ranges(code, "0000..001F|007F..009F"):
        enums = add_enum(enums, "__UNICODE_PROP_ISOCONTROL")
    if has_category(cats, "P*"):
        enums = add_enum(enums, "__UNICODE_PROP_PUNCTUATION")
    if has_category(cats, "S*"):
        enums = add_enum(enums, "__UNICODE_PROP_SYMBOL")
    if code_in_ranges(code, "0030..0039|0041..0046|0061..0066"):
        enums = add_enum(enums, "__UNICODE_PROP_HEXDIGIT")
    if has_category(cats, "Zs") or code_in_ranges(code, "0009"):
        enums = add_enum(enums, "__UNICODE_PROP_BLANK")
    if code_in_ranges(code, "0000..007F"):
        enums = add_enum(enums, "__UNICODE_PROP_ASCII")
    return enums


def write_header(out_path):
    """
    Write the header file to a file.
    """
    with open(out_path.replace(".c", ".h"), "w") as f:
        f.write("""\
/*
 * This file is generated by gen_props.py.
 * Do not edit this file directly.
 */
""")
        f.write("""\

#ifndef __UNICODE_PROPS_H__
#define __UNICODE_PROPS_H__
""")
        f.write("""\

#include <stddef.h>
#include <stdint.h>
""")
        f.write("""\

typedef enum {
    __UNICODE_PROP_WHITESPACE = 0x1,
    __UNICODE_PROP_LOWERCASE = 0x2,
    __UNICODE_PROP_UPPERCASE = 0x4,
    __UNICODE_PROP_TITLECASE = 0x8,
    __UNICODE_PROP_LETTER = 0x10,
    __UNICODE_PROP_DIGIT = 0x20,
    __UNICODE_PROP_GRAPHIC = 0x40,
    __UNICODE_PROP_PRINTABLE = __UNICODE_PROP_GRAPHIC|__UNICODE_PROP_WHITESPACE,
    __UNICODE_PROP_ISOCONTROL = 0x80,
    __UNICODE_PROP_PUNCTUATION = 0x100,
    __UNICODE_PROP_SYMBOL = 0x200,
    __UNICODE_PROP_HEXDIGIT = 0x400,
    __UNICODE_PROP_BLANK = 0x800,
    __UNICODE_PROP_ASCII = 0x1000
} __unicode_props_t;

typedef struct {
    uint32_t          code_start;
    uint32_t          code_end;
    __unicode_props_t props;
} __unicode_ctype_t;

""")

        f.write("""\
extern const size_t g_unicodePropsCount;
""")
        f.write("""\
extern const __unicode_ctype_t g_unicodePropsTable[];

""")
        f.write("""\
#endif //!__UNICODE_PROPS_H__
""")


def write_table(props, out_path):
    """
    Write the table of property mappings to a file.
    """

    with open(out_path, "w") as f:
        f.write("""\
/*
 * This file is generated by gen_props.py.
 * Do not edit this file directly.
 */

""")
        headerName = out_path.replace(".c", ".h")
        f.write(f"#include \"{headerName}\"\n\n")

        f.write("const __unicode_ctype_t g_unicodePropsTable[] = {\n")
        f.write("    // uppercase, lowercase\n")

        prev_enums = ""
        code_start = 0
        code_previous = 0
        num_elements = 0

        def write_element(e):
            nonlocal num_elements
            if e == "":
                return
            f.write("    { ")
            f.write("{0}, {1}, {2}".format(code_start, code_previous, e))
            f.write(" },\n")
            num_elements += 1

        for code, vals in props:
            enums = determine_enum_props(code, vals["category"], vals["type"]).strip()
            if code_start == 0:
                code_start = code
                code_previous = code
                prev_enums = enums

            # Can we consolidate?
            # Code must match next
            if code == code_start or code == code_previous + 1:
                if enums != "" and enums == prev_enums:
                    code_previous = code
                    continue

            write_element(prev_enums)
            code_start = code
            code_previous = code
            prev_enums = enums

        f.write("};\n")
        f.write(f"const size_t g_unicodePropsCount = {num_elements};\n", )


def main(args):
    """
    Generate the table of unicode properties.
    """
    props_table = download_props_table()
    props = parse_props_table(props_table)

    # Write the table to a file.
    write_header(args.out)
    write_table(props, args.out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Unicode helper script to generate properties.')
    parser.add_argument('--out', default="mstr_props.c", help='Where to write the output file.')
    args = parser.parse_args()
    main(args)
