import os
import shutil
import tempfile
import unittest

from scripts.patch_cppdap import patch_cppdap


class TestPatchCppDAP(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.src_dir = os.path.join(self.test_dir, "src")
        os.makedirs(self.src_dir)
        self.socket_cpp = os.path.join(self.src_dir, "socket.cpp")

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_patches_unpatched_socket(self):
        sample = """
Socket::Socket(const char* address, const char* port)
    : shared(Shared::create(address, port)) {
  if (shared) {
    shared->lock([&](SOCKET socket, const addrinfo* info) {
      if (bind(socket, info->ai_addr, (int)info->ai_addrlen) != 0) {
        shared.reset();
        return;
      }

      if (listen(socket, 0) != 0) {
        shared.reset();
        return;
      }
    });
  }
}
"""
        with open(self.socket_cpp, "w", encoding="utf-8") as f:
            f.write(sample)

        patch_cppdap(self.test_dir)

        with open(self.socket_cpp, "r", encoding="utf-8") as f:
            patched = f.read()

        self.assertIn("bool failed = false;", patched)
        self.assertIn("failed = true;", patched)
        self.assertNotIn("shared.reset();\n        return;", patched)

    def test_idempotent_on_already_patched(self):
        sample = """
  if (shared) {
    bool failed = false;
    shared->lock([&](SOCKET socket, const addrinfo* info) {
      if (bind(socket, info->ai_addr, (int)info->ai_addrlen) != 0) {
        failed = true;
        return;
      }

      if (listen(socket, 0) != 0) {
        failed = true;
        return;
      }
    });
    if (failed) {
      shared.reset();
    }
  }
"""
        with open(self.socket_cpp, "w", encoding="utf-8") as f:
            f.write(sample)

        patch_cppdap(self.test_dir)

        with open(self.socket_cpp, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertEqual(content, sample)


if __name__ == "__main__":
    unittest.main()
