# SPDX-License-Identifier: MIT
import unittest

def path_equals_case_insensitive(a: str, b: str) -> bool:
    if len(a) != len(b):
        return False
    for ca, cb in zip(a, b):
        na = '/' if ca == '\\' else ca.lower()
        nb = '/' if cb == '\\' else cb.lower()
        if na != nb:
            return False
    return True

def path_ends_with_case_insensitive(full: str, suffix: str) -> bool:
    if len(full) < len(suffix):
        return False
    offset = len(full) - len(suffix)
    for i in range(len(suffix)):
        ca = '/' if full[offset + i] == '\\' else full[offset + i].lower()
        cb = '/' if suffix[i] == '\\' else suffix[i].lower()
        if ca != cb:
            return False
    return True

def path_matches_section(path_a: str, path_b: str) -> bool:
    if not path_a or not path_b:
        return False
    if path_equals_case_insensitive(path_a, path_b):
        return True
    if len(path_a) > len(path_b):
        offset = len(path_a) - len(path_b)
        if path_ends_with_case_insensitive(path_a, path_b):
            prev_char = path_a[offset - 1]
            if prev_char in ('/', '\\'):
                return True
    elif len(path_b) > len(path_a):
        offset = len(path_b) - len(path_a)
        if path_ends_with_case_insensitive(path_b, path_a):
            prev_char = path_b[offset - 1]
            if prev_char in ('/', '\\'):
                return True
    return False

class TestPathMatching(unittest.TestCase):
    def test_exact_match_casing(self):
        engine_path = "d:/steamlibrary/steamapps/common/sven co-op/svencoop/scripts/maps/hlsp.as"
        vscode_path = r"D:\SteamLibrary\steamapps\common\Sven Co-op\svencoop\scripts\maps\HLSP.as"
        self.assertTrue(path_matches_section(engine_path, vscode_path))
        self.assertTrue(path_matches_section(vscode_path, engine_path))

    def test_relative_suffix_match(self):
        engine_path = "d:/steamlibrary/steamapps/common/sven co-op/svencoop/scripts/maps/hlsp.as"
        rel_path = "maps/HLSP.as"
        self.assertTrue(path_matches_section(engine_path, rel_path))
        self.assertTrue(path_matches_section(rel_path, engine_path))

    def test_filename_only_match(self):
        engine_path = "d:/steamlibrary/steamapps/common/sven co-op/svencoop/scripts/maps/hlsp.as"
        filename = "HLSP.as"
        self.assertTrue(path_matches_section(engine_path, filename))
        self.assertTrue(path_matches_section(filename, engine_path))

    def test_non_boundary_substring_rejected(self):
        engine_path = "d:/steamlibrary/steamapps/common/sven co-op/svencoop/scripts/maps/super_hlsp.as"
        filename = "hlsp.as"
        self.assertFalse(path_matches_section(engine_path, filename))

    def test_different_extension_rejected(self):
        p1 = "scripts/maps/hlsp.as"
        p2 = "scripts/maps/hlsp.txt"
        self.assertFalse(path_matches_section(p1, p2))

if __name__ == '__main__':
    unittest.main()
