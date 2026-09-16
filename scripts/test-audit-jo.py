#!/usr/bin/env python3
"""Check source-based JO behavior audit boundaries and dispatch classification."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("audit", Path(__file__).with_name("audit-jo.py"))
assert spec and spec.loader
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class BehaviorAuditTests(unittest.TestCase):
    def test_comment_mask_keeps_source_line_numbers(self):
        source = '/* first\n second */\nvoid f() { print("// literal"); }'
        masked = audit.uncomment(source)
        self.assertEqual(len(masked), len(source))
        self.assertEqual(masked.count("\n"), source.count("\n"))
        self.assertIn('"// literal"', masked)

    def test_braces_in_strings_do_not_end_a_function(self):
        text = '{ call("}"); if (flag) { action(); } } next()'
        self.assertEqual(audit.braced_body(text, 0).strip(), 'call("}"); if (flag) { action(); }')

    def test_registry_dispatch_fallthrough_and_stub_are_separate(self):
        source = '''
ENUM2STRING(SET_PRESENT), ENUM2STRING(SET_ALIAS), ENUM2STRING(SET_STUB), ENUM2STRING(SET_REGISTERED_ONLY)
void CQuake3GameInterface::Set(int id) {
 const char *message = "case SET_FAKE: }";
 switch(id) {
 case SET_PRESENT:
 case SET_ALIAS: act(); break;
 case SET_STUB: /* missing */ break;
 default: break;
 }
}
int CQuake3GameInterface::GetFloat(int id) { switch(id) { case SET_PRESENT: return 1; default: return 0; } }
int CQuake3GameInterface::GetString(int id) { return 0; }
int CQuake3GameInterface::GetVector(int id) { return 0; }
'''
        with tempfile.TemporaryDirectory() as temp, patch.object(audit, "ROOT", Path(temp)):
            path = Path(temp) / "interface.cpp"
            path.write_text(source)
            registry, dispatch = audit.script_interface(path)
        self.assertIn("SET_REGISTERED_ONLY", registry)
        self.assertNotIn("SET_REGISTERED_ONLY", dispatch["set"])
        self.assertNotIn("SET_FAKE", dispatch["set"])
        self.assertFalse(dispatch["set"]["SET_PRESENT"]["stub_review"])
        self.assertFalse(dispatch["set"]["SET_ALIAS"]["stub_review"])
        self.assertTrue(dispatch["set"]["SET_STUB"]["stub_review"])
        self.assertIn("SET_PRESENT", dispatch["get-float"])
        self.assertNotIn("SET_PRESENT", dispatch["get-string"])


if __name__ == "__main__":
    unittest.main()
