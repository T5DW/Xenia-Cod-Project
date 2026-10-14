"""Black Ops II July 20 prototype: fastfile container, menu parsing and in-place edits.

Set BO2_UI_FF to Wyatt's ui_mp.ff (TAffx100) to run the file tests."""

import os
import struct
import sys
import unittest

from t5menu import opcodes
from t5menu.bo2ff import BO2FastFile, is_bo2
from t5menu.parser import struct_size
from t5menu.schema import STRUCTS_BO2

FF = os.environ.get("BO2_UI_FF")
sys.setrecursionlimit(20000)


class Bo2SchemaTest(unittest.TestCase):
    def test_struct_sizes_match_the_zone_loader(self):
        # li r5, <size> in Load_menuDef_t / Load_itemDef_s / Load_textDef_s etc. of the xex
        for name, size in {"menuDef_t": 0x1A8, "windowDef_t": 0xB0, "itemDef_s": 0x120,
                           "textDef_s": 0xD4, "listBoxDef_s": 0x2BC, "multiDef_s": 0x18C,
                           "editFieldDef_s": 0x30, "UIAnimInfo": 0xEC, "animParamsDef_t": 0x6C,
                           "GenericEventScript": 0x2C, "MenuRow": 0x18}.items():
            self.assertEqual(struct_size(name, STRUCTS_BO2), size, name)

    def test_function_names(self):
        self.assertEqual(opcodes.command_name(64, "bo2"), "locstring()")
        self.assertEqual(opcodes.command_name(200, "bo2"), "cmd#200")
        self.assertEqual(opcodes.command_index("cmd#200", "bo2"), 200)
        self.assertEqual(opcodes.command_index("dvarint()", "bo2"), 30)

    def test_detects_bo2_magic(self):
        self.assertTrue(is_bo2(b"TAffx100" + struct.pack(">I", 0x81) + b"PHEEBs71"))
        self.assertFalse(is_bo2(b"IWff0100" + struct.pack(">I", 0x1D7) + b"IWffs101"))


@unittest.skipUnless(FF, "set BO2_UI_FF to a BO2 July 20 ui_mp.ff")
class Bo2FileTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = open(FF, "rb").read()
        cls.ff = BO2FastFile.parse(cls.raw)

    def test_rebuild_keeps_the_zone(self):
        out = self.ff.build()
        self.assertEqual(out[:8], b"TAff0100")
        again = BO2FastFile.parse(out)
        self.assertEqual(again.zone, self.ff.zone)
        self.assertEqual(again.build(), out)  # unchanged zlib blocks are kept

    def test_menus_parse_to_the_next_asset(self):
        from t5menu.menufile import find_menu_lists, menu_entries
        lists = find_menu_lists(self.ff.zone, "bo2")
        self.assertEqual(len(lists), 3)
        self.assertEqual(bytes(lists[0]["name"]), b"ui_mp/menus.txt")
        self.assertEqual(lists[1].pos, lists[0].end)  # the first list ends exactly where the next starts
        names = [m["window"]["name"] for _, _, m in menu_entries(lists)]
        self.assertEqual(bytes(names[0]), b"main")
        self.assertEqual(len(names), 137)

    def test_edit_round_trip(self):
        import tempfile
        from pathlib import Path
        from t5menu.workspace import compile_workspace, decompile
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "ui_mp.ff"
            src.write_bytes(self.raw)
            ws = decompile(src, log=lambda *_: None)
            main = ws / "menus" / "000_main.menu"
            text = main.read_text(encoding="utf-8")
            self.assertIn('action "exec \\"checkfastfilebudgets 1\\" ;"', text)
            main.write_text(text.replace('checkfastfilebudgets 1', 'checkfastfilebudgets 0'), encoding="utf-8")
            out = compile_workspace(ws, Path(d) / "out.ff", log=lambda *_: None)
            new = BO2FastFile.load(out).zone
            old = self.ff.zone
            diff = [i for i in range(len(old)) if old[i] != new[i]]
            self.assertEqual(len(diff), 1)
            self.assertEqual(new[diff[0]:diff[0] + 1], b"0")


if __name__ == "__main__":
    unittest.main()


CODE_FF = os.environ.get("BO2_CODE_FF")


@unittest.skipUnless(CODE_FF, "set BO2_CODE_FF to a BO2 July 20 code_post_gfx_mp.ff")
class CodePostGfxTest(unittest.TestCase):
    def test_menu_list(self):
        from t5menu.menufile import find_menu_lists
        from t5menu.parser import Node
        with open(CODE_FF, "rb") as f:
            ff = BO2FastFile.parse(f.read())
        lists = find_menu_lists(ff.zone, "bo2")
        self.assertEqual([bytes(l["name"]) for l in lists], [b"ui_mp/code.txt"])
        self.assertEqual(sum(isinstance(m, Node) for m in lists[0]["menus"]), 38)
