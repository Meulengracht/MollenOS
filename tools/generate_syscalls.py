#!/usr/bin/env python3
"""Generates the kernel syscall table and libc wrappers from syscalls.table."""

import argparse
from dataclasses import dataclass
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


@dataclass
class Syscall:
    """Number, handler, result, and arguments"""

    number: int
    name: str
    handler: str
    result: str
    arguments: list


def parse_table(path):
    calls = []
    numbers = set()
    names = set()
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.partition("#")[0].strip()
        if not line:
            continue

        location = f"{path}:{line_number}"
        fields = line.split(maxsplit=4)
        if len(fields) != 5:
            raise ValueError(f"{location}: expected number, name, handler, result, arguments")
        
        number, name, handler, result, arguments = fields
        if not re.fullmatch(r"0|[1-9][0-9]*", number):
            raise ValueError(f"{location}: syscall number must be a non-negative decimal integer")
        
        number = int(number)
        if number in numbers:
            raise ValueError(f"{location}: duplicate syscall number {number}")
        if name in names:
            raise ValueError(f"{location}: duplicate wrapper name {name}")

        arguments = [] if arguments == "-" else [arg.strip() for arg in arguments.split(",")]
        for identifier in [name, handler, result] + arguments:
            if not IDENTIFIER.fullmatch(identifier):
                raise ValueError(f"{location}: invalid C identifier {identifier!r}")
        
        if len(arguments) > 5:
            raise ValueError(f"{location}: syscalls support at most five arguments")
        
        if len(set(arguments)) != len(arguments):
            raise ValueError(f"{location}: duplicate argument name")
        
        reserved = {"SCPARAM", result} | {f"syscall{count}" for count in range(6)}
        if reserved.intersection(arguments):
            raise ValueError(f"{location}: argument name conflicts with a syscall helper or return cast")

        calls.append(Syscall(number, name, handler, result, arguments))
        numbers.add(number)
        names.add(name)

    if not calls:
        raise ValueError(f"{path}: syscall table is empty")
    
    calls.sort(key=lambda call: call.number)
    for expected, call in enumerate(calls):
        if call.number != expected:
            raise ValueError(f"{path}: missing syscall number {expected}; numbers must be contiguous from zero")
    return calls


def render_wrappers(calls):
    lines = []
    for call in calls:
        parameters = ", ".join(call.arguments)
        arguments = [str(call.number)] + [f"SCPARAM({arg})" for arg in call.arguments]
        lines.append(
            f"#define Syscall_{call.name}({parameters}) "
            f"({call.result})syscall{len(call.arguments)}({', '.join(arguments)})"
        )
    return "\n".join(lines)


def replace_section(source, section, content):
    begin = f"// BEGIN GENERATED {section} - tools/generate_syscalls.py"
    end = f"// END GENERATED {section}"
    
    if source.count(begin) != 1 or source.count(end) != 1:
        raise ValueError(f"expected one pair of {section} markers")
    
    start = source.index(begin) + len(begin)
    stop = source.index(end)
    if stop < start:
        raise ValueError(f"reversed {section} markers")
    
    newline = "\r\n" if source[start:start + 2] == "\r\n" else "\n"
    return source[:start] + newline + content.replace("\n", newline) + newline + source[stop:]


def generate(root, check=False):
    calls = parse_table(root / "syscalls.table")
    entries = "\n".join(f"    DefineSyscall({call.number}, {call.handler})," for call in calls)
    outputs = [
        (root / "kernel/api/entry.c", [
            ("SYSCALL COUNT", f"#define SYSTEM_CALL_COUNT {len(calls)}"),
            ("SYSCALL TABLE", entries),
        ]),
        (root / "librt/libc/include/internal/_syscalls.h", [
            ("SYSCALL WRAPPERS", render_wrappers(calls)),
        ]),
    ]

    # Validate every line before writing the files.
    changes = []
    for path, sections in outputs:
        original = path.read_bytes().decode("utf-8")
        updated = original
        for section, content in sections:
            try:
                updated = replace_section(updated, section, content)
            except ValueError as error:
                raise ValueError(f"{path}: {error}") from error
        if updated != original:
            changes.append((path, updated))

    for path, updated in changes:
        if check:
            print(f"out of date: {path.relative_to(root)}")
        else:
            path.write_bytes(updated.encode("utf-8"))
            print(f"updated: {path.relative_to(root)}")
    
    if check and changes:
        print("Run python3 tools/generate_syscalls.py to regenerate the syscall definitions.")
        return 1
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if generated sections are out of date")
    args = parser.parse_args()

    try:
        return generate(ROOT, check=args.check)
    except (OSError, ValueError) as error:
        parser.exit(1, f"generate-syscalls: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
