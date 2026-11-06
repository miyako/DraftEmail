"""Unattended native protocol tests: never invoke interactive compose."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def request(**changes):
    value = dict(protocol=1, client="apple-mail", to=[], cc=[], bcc=[],
                 subject="", body="", attachments=[])
    value.update(changes)
    return value


class CppCLI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        selected = os.environ.get("TEST_CLI_LANGUAGE")
        if selected == "none":
            raise unittest.SkipTest("native CLI explicitly disabled")
        if selected not in (None, "cpp"):
            raise RuntimeError("DraftEmail requires cpp")
        if not shutil.which("cmake"):
            raise RuntimeError("required CMake missing")
        cls.scratch = tempfile.TemporaryDirectory(prefix=".mail-cli-test ", dir=ROOT / "tests")
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.directory = Path(cls.scratch.name)
        cls.build = cls.directory / "build"
        for args in (
            ["cmake", "-S", str(ROOT / "cli"), "-B", str(cls.build),
             "-DCMAKE_BUILD_TYPE=Release", *(["-A", "x64"] if os.name == "nt" else [])],
            ["cmake", "--build", str(cls.build), "--config", "Release"],
            ["ctest", "--test-dir", str(cls.build), "-C", "Release", "--output-on-failure"],
        ):
            result = subprocess.run(args, capture_output=True, timeout=180)
            if result.returncode:
                raise RuntimeError(result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
        executable = cls.build / ("draft-email-cli.exe" if os.name == "nt" else "draft-email-cli")
        if not executable.exists():
            executable = cls.build / "Release" / "draft-email-cli.exe"
        target = cls.directory / "executables 世界 with spaces"
        target.mkdir()
        cls.executable = target / executable.name
        shutil.copy2(executable, cls.executable)

    def run_cli(self, args):
        return subprocess.run([os.fspath(self.executable), *args], capture_output=True, timeout=10)

    def validate(self, value, code=None, command="validate"):
        raw = value if isinstance(value, (str, bytes)) else json.dumps(value, ensure_ascii=False)
        result = self.run_cli([command, "--request", raw])
        if code is None:
            self.assertEqual((result.returncode, result.stdout, result.stderr),
                             (0, b'{"protocol":1,"status":"validated"}\n', b""))
        else:
            self.assertEqual(result.returncode, 2, result)
            self.assertEqual(result.stdout, b"")
            error = json.loads(result.stderr)
            self.assertEqual(error["protocol"], 1)
            self.assertEqual(error["error"]["code"], code, result.stderr)
            self.assertIsInstance(error["error"]["message"], str)
        return result

    def test_version(self):
        result = self.run_cli(["--version"])
        self.assertEqual((result.returncode, result.stdout, result.stderr),
                         (0, b'{"protocol":1,"version":"0.1.4"}\n', b""))

    def test_capabilities_no_ui(self):
        result = self.run_cli(["capabilities"])
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, b"")
        data = json.loads(result.stdout)
        self.assertEqual(data["protocol"], 1)
        self.assertFalse(data["deliveryConfirmed"])
        self.assertTrue(data["interactiveRequired"])
        self.assertEqual(data["timeoutSeconds"], 540)
        self.assertEqual(data["client"], "simple-mapi" if os.name == "nt" else "apple-mail")
        self.assertEqual(data["fields"]["cc"], os.name == "nt")
        self.assertEqual(data["fields"]["bcc"], os.name == "nt")
        if os.name == "nt":
            self.assertEqual(data["providerPolicy"], "registered-unicode-x64")
            self.assertFalse(data["newOutlook"])

    def test_unicode_and_literal_text(self):
        for body in ["世界 café 😀", "\"'\\;$(echo nope)\n\r\t\b\f", "\u007f\u0080\u07ff\u0800\uffff\U0010ffff"]:
            self.validate(request(subject="世界 'quoted' 😀", body=body,
                                  to=["person+tag@example.com", "x@xn--bcher-kva.example"]))
        self.validate(request(interactive=False))
        self.validate(request(client="simple-mapi", cc=["cc@example.com"], bcc=["bcc@example.com"]))
        self.validate(json.dumps(request(body="😀"), ensure_ascii=True))

    def test_all_required_keys_and_types(self):
        for key in request():
            value = request()
            del value[key]
            with self.subTest(missing=key):
                self.validate(value, "invalid_request")
            for wrong in [None, True, 1, [], {}]:
                value = request()
                if type(wrong) is type(value[key]):
                    continue
                value[key] = wrong
                with self.subTest(key=key, wrong=wrong):
                    self.validate(value, "invalid_request")
        for value in [True, None, 1, 1.0, "1", 2, False]:
            if type(value) is int and value == 1:
                continue
            self.validate(request(protocol=value), "invalid_request")
        for value in ["true", 1, None, [], {}]:
            self.validate(request(interactive=value), "invalid_request")
        for value in [None, False, [], "value", 1]:
            self.validate(value if isinstance(value, str) else json.dumps(value),
                          "invalid_json" if value == "value" else "invalid_request")

    def test_unknown_fields_and_duplicate_keys(self):
        self.validate(request(extra="no"), "unknown_field")
        self.validate(request(htmlBody="<p>not supported</p>"), "unknown_field")
        for raw in ['{"protocol":1,"protocol":1}', '{"x":1,"\\u0078":2}', '{"x":{"a":1,"a":2}}']:
            self.validate(raw, "invalid_json")

    def test_malformed_json(self):
        for raw in ["", " ", "{", "[] trailing", '{"x":}', '{"x":1,}', '{"x":[1,]}',
                    '{"x":01}', '{"x":NaN}', '{"x":Infinity}', '{"x":+1}', '{"x":1.}',
                    '{"x":1e}', '{"x":-}', '{"x":"\\x20"}', '{"x":"\\uD800"}',
                    '{"x":"\\uDC00"}', '{"x":"\\uD800\\u0000"}', '{"x":"\t"}',
                    "\ufeff{}", "{/* comment */}", '{"x":true false}']:
            with self.subTest(raw=raw):
                self.validate(raw, "invalid_json")
        self.validate("[" * 18 + "]" * 18, "limit_exceeded")

    def test_parser_legal_json_scalars(self):
        # Legal but schema-invalid JSON must reach schema validation.
        for scalar in ["null", "true", "false", "0", "-0", "1.25", "1e10", "-2E-3",
                       '""', '"\\uD83D\\uDE00"', '["x",null,{}]', '{"protocol":1.0}']:
            self.validate(scalar, "invalid_request")
        for raw in ['{"protocol":1e0}', '{"protocol":1E+0}']:
            self.validate(raw, "invalid_request")

    def test_nul(self):
        for key in ("subject", "body", "client"):
            self.validate(request(**{key: "x\0y"}), "invalid_request")
        for key in ("to", "cc", "bcc", "attachments"):
            self.validate(request(**{key: ["x\0y"]}), "invalid_request")
        self.validate('{"\\u0000":1}', "invalid_request")

    def test_mailbox_grammar(self):
        for mailbox in ["", "not-an-address", "Display <x@example.com>", "a,b@example.com",
                        "x@y\nBcc:z@z", "x y@example.com", "a@@b", ".a@b", "a.@b",
                        "a..b@c", "a@-example.com", "a@example-.com", "a@example..com",
                        "ü@example.com", "x@世界.example", "a@" + "x" * 64 + ".com"]:
            for key in ("to", "cc", "bcc"):
                self.validate(request(client="simple-mapi", **{key: [mailbox]}), "invalid_request")
        for key in ("to", "cc", "bcc", "attachments"):
            self.validate(request(**{key: [None]}), "invalid_request")

    def test_limits(self):
        self.validate(request(body="x" * 16384, subject="x" * 998))
        self.validate(request(body="x" * 16385), "limit_exceeded")
        self.validate(request(subject="x" * 999), "limit_exceeded")
        self.validate(request(to=["a@b"] * 101), "limit_exceeded")
        self.validate(request(client="simple-mapi", to=["a@b"] * 100, cc=["a@b"]), "limit_exceeded")
        self.validate(request(attachments=["/absent"] * 21), "limit_exceeded")
        self.validate(request(attachments=["/" + "x" * 4096]), "limit_exceeded")
        self.validate(" " * 24577, "limit_exceeded")
        self.validate(request(subject="header\r\ninjection"), "invalid_request")

    def test_apple_unsupported_fields(self):
        for key in ("cc", "bcc"):
            self.validate(request(**{key: ["a@example.com"]}), "unsupported_field")
        self.validate(request(client="unknown"), "invalid_request")

    def test_attachments(self):
        original = self.directory / "original 世界 'double\"quote.txt"
        if os.name == "nt":
            original = self.directory / "original 世界 spaces.txt"
        original.write_bytes(b"attachment\0binary")
        self.validate(request(attachments=[str(original)]))
        self.assertEqual(original.read_bytes(), b"attachment\0binary")
        for value in ["relative.txt", str(self.directory), str(self.directory / "missing")]:
            self.validate(request(attachments=[value]), "invalid_attachment")
        large = self.directory / "large"
        with large.open("wb") as stream:
            stream.truncate(25 * 1024 * 1024 + 1)
        self.validate(request(attachments=[str(large)]), "invalid_attachment")
        large.unlink()
        aggregate = self.directory / "aggregate"
        with aggregate.open("wb") as stream:
            stream.truncate(25 * 1024 * 1024)
        self.validate(request(attachments=[str(aggregate)] * 4))
        self.validate(request(attachments=[str(aggregate)] * 5), "limit_exceeded")
        aggregate.unlink()
        if os.name != "nt":
            link = self.directory / "symlink"
            link.symlink_to(original)
            self.validate(request(attachments=[str(link)]), "invalid_attachment")
            link.unlink()
            fifo = self.directory / "fifo"
            os.mkfifo(fifo)
            self.validate(request(attachments=[str(fifo)]), "invalid_attachment")
            fifo.unlink()
        original.unlink()

    def test_compose_preflight_never_opens_ui(self):
        self.validate(request(), "invalid_request", command="compose")
        self.validate(request(interactive=False), "invalid_request", command="compose")
        self.validate(request(interactive=True, cc=["a@b"]), "unsupported_field", command="compose")

    def test_cleanup_rejects_unsafe_tokens_without_io(self):
        for token in ["", ".", "..", "../anything", "A" * 32, "0" * 31, "/" + "0" * 32]:
            result = self.run_cli(["cleanup", "--staging", token])
            self.assertEqual(result.returncode, 2)
            self.assertEqual(json.loads(result.stderr)["error"]["code"], "invalid_staging")
            self.assertEqual(result.stdout, b"")

    def test_invalid_arguments(self):
        for args in [[], [""], ["unknown"], ["greet", "--name", "World"], ["--version", "extra"],
                     ["validate"], ["validate", "--request"], ["validate", "--request={}"],
                     ["compose", "--request", "{}", "extra"], ["capabilities", "--request", "{}"]]:
            result = self.run_cli(args)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, b"")
            self.assertEqual(json.loads(result.stderr)["error"]["code"], "invalid_arguments")

    @unittest.skipUnless(os.name == "posix", "byte argv requires POSIX")
    def test_malformed_utf8(self):
        for value in [b"\x80", b"\xff", b"\xc0\xaf", b"\xc2", b"\xe2\x82", b"\xed\xa0\x80",
                      b"\xf0\x80\x80\x80", b"\xf4\x90\x80\x80", b"\xf5\x80\x80\x80"]:
            for args in ([b"validate", b"--request", value], [value], [b"--version", value]):
                result = self.run_cli(args)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b"")
                self.assertEqual(json.loads(result.stderr)["error"]["code"], "invalid_utf8")


if __name__ == "__main__":
    unittest.main()
