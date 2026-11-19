"""Validate syscall table errors and safe regeneration of the checked-in files."""

from contextlib import redirect_stdout
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import generate_syscalls


class SyscallGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="vali-syscalls-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.table = self.root / "syscalls.table"
        self.table.write_text(
            "# Deliberately out of order; numbers determine dispatch.\n"
            "1 Send ScSend oserr_t A, B, C, D, E # five arguments\n"
            "0 ThreadId ScThreadGetCurrentId uuid_t -\n",
            encoding="utf-8",
        )
        self.kernel = self.root / "kernel/api/entry.c"
        self.header = self.root / "librt/libc/include/internal/_syscalls.h"
        self.kernel.parent.mkdir(parents=True)
        self.header.parent.mkdir(parents=True)
        self.kernel.write_text(
            "// Handwritten declarations.\n"
            "// BEGIN GENERATED SYSCALL COUNT - tools/generate_syscalls.py\n"
            "old count\n"
            "// END GENERATED SYSCALL COUNT\n"
            "// Handwritten descriptor.\n"
            "// BEGIN GENERATED SYSCALL TABLE - tools/generate_syscalls.py\n"
            "old table\n"
            "// END GENERATED SYSCALL TABLE\n"
            "// Handwritten dispatcher.\n",
            encoding="utf-8",
        )
        self.header.write_text(
            "// Handwritten syscall declarations.\n"
            "// BEGIN GENERATED SYSCALL WRAPPERS - tools/generate_syscalls.py\n"
            "old wrappers\n"
            "// END GENERATED SYSCALL WRAPPERS\n"
            "// Handwritten footer.\n",
            encoding="utf-8",
        )

    def generate(self, check=False):
        with redirect_stdout(io.StringIO()):
            return generate_syscalls.generate(self.root, check=check)

    def test_generation_preserves_numbers_signatures_and_surrounding_code(self):
        self.assertEqual(self.generate(), 0)
        self.assertEqual(
            self.kernel.read_text(),
            "// Handwritten declarations.\n"
            "// BEGIN GENERATED SYSCALL COUNT - tools/generate_syscalls.py\n"
            "#define SYSTEM_CALL_COUNT 2\n"
            "// END GENERATED SYSCALL COUNT\n"
            "// Handwritten descriptor.\n"
            "// BEGIN GENERATED SYSCALL TABLE - tools/generate_syscalls.py\n"
            "    DefineSyscall(0, ScThreadGetCurrentId),\n"
            "    DefineSyscall(1, ScSend),\n"
            "// END GENERATED SYSCALL TABLE\n"
            "// Handwritten dispatcher.\n",
        )
        self.assertEqual(
            self.header.read_text(),
            "// Handwritten syscall declarations.\n"
            "// BEGIN GENERATED SYSCALL WRAPPERS - tools/generate_syscalls.py\n"
            "#define Syscall_ThreadId() (uuid_t)syscall0(0)\n"
            "#define Syscall_Send(A, B, C, D, E) (oserr_t)syscall5(1, "
            "SCPARAM(A), SCPARAM(B), SCPARAM(C), SCPARAM(D), SCPARAM(E))\n"
            "// END GENERATED SYSCALL WRAPPERS\n"
            "// Handwritten footer.\n",
        )

    def test_check_reports_stale_output_without_writing(self):
        before = [path.read_bytes() for path in (self.kernel, self.header)]
        self.assertEqual(self.generate(check=True), 1)
        self.assertEqual(before, [path.read_bytes() for path in (self.kernel, self.header)])
        self.generate()
        self.assertEqual(self.generate(check=True), 0)
        for path, old, new in (
            (self.kernel, b"SYSTEM_CALL_COUNT 2", b"SYSTEM_CALL_COUNT 3"),
            (self.header, b"syscall5(1,", b"syscall5(0,"),
        ):
            with self.subTest(path=path):
                original = path.read_bytes()
                path.write_bytes(original.replace(old, new))
                self.assertEqual(self.generate(check=True), 1)
                path.write_bytes(original)

    def test_repeated_generation_does_not_rewrite_files(self):
        self.generate()
        before = [path.stat().st_mtime_ns for path in (self.kernel, self.header)]
        self.generate()
        self.assertEqual(before, [path.stat().st_mtime_ns for path in (self.kernel, self.header)])

    def test_crlf_line_endings_are_preserved(self):
        for path in (self.kernel, self.header):
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.generate()
        for path in (self.kernel, self.header):
            self.assertNotIn(b"\n", path.read_bytes().replace(b"\r\n", b""))
        self.assertEqual(self.generate(check=True), 0)

    def test_bad_markers_do_not_partially_update_outputs(self):
        original = self.header.read_text()
        malformed = (
            original.replace("// END GENERATED SYSCALL WRAPPERS", "// Missing marker"),
            original + "// END GENERATED SYSCALL WRAPPERS\n",
            "// END GENERATED SYSCALL WRAPPERS\n"
            "// BEGIN GENERATED SYSCALL WRAPPERS - tools/generate_syscalls.py\n",
        )
        for content in malformed:
            with self.subTest(content=content):
                self.header.write_text(content, encoding="utf-8")
                before = self.kernel.read_bytes()
                with self.assertRaisesRegex(ValueError, "markers"):
                    self.generate()
                self.assertEqual(self.kernel.read_bytes(), before)
                self.assertEqual(self.header.read_text(), content)

    def test_invalid_tables_do_not_change_outputs(self):
        invalid = (
            ("# no entries\n", "empty"),
            ("0 Yield ScYield oserr_t\n", "expected number"),
            ("-1 Yield ScYield oserr_t -\n", "non-negative decimal"),
            ("0x0 Yield ScYield oserr_t -\n", "non-negative decimal"),
            ("1 Yield ScYield oserr_t -\n", "missing syscall number 0"),
            ("0 A ScA oserr_t -\n2 B ScB oserr_t -\n", "missing syscall number 1"),
            ("0 A ScA oserr_t -\n0 B ScB oserr_t -\n", "duplicate syscall number"),
            ("0 A ScA oserr_t -\n1 A ScB oserr_t -\n", "duplicate wrapper name"),
            ("0 A ScA oserr_t A,B,C,D,E,F\n", "at most five"),
            ("0 A ScA oserr_t A,A\n", "duplicate argument"),
            ("0 A ScA oserr_t SCPARAM\n", "conflicts with a syscall helper"),
            ("0 A ScA oserr_t syscall1\n", "conflicts with a syscall helper"),
            ("0 A ScA oserr_t oserr_t\n", "conflicts with a syscall helper"),
            ("0 A ScA oserr_t A,\n", "invalid C identifier"),
            ("0 A ScA oserr_t A B\n", "invalid C identifier"),
            ("0 bad-name ScA oserr_t -\n", "invalid C identifier"),
            ("0 A ScA() oserr_t -\n", "invalid C identifier"),
            ("0 A ScA int* -\n", "invalid C identifier"),
        )
        before = [path.read_bytes() for path in (self.kernel, self.header)]
        for content, message in invalid:
            with self.subTest(content=content):
                self.table.write_text(content, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, message):
                    self.generate()
                self.assertEqual(before, [path.read_bytes() for path in (self.kernel, self.header)])


if __name__ == "__main__":
    unittest.main()
