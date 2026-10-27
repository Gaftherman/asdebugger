import os
import sys


def patch_cppdap(cppdap_dir):
    socket_cpp = os.path.join(cppdap_dir, "src", "socket.cpp")
    if not os.path.exists(socket_cpp):
        print(f"cppdap socket.cpp not found at: {socket_cpp}")
        return

    with open(socket_cpp, "r", encoding="utf-8") as f:
        content = f.read()

    old_pattern = """  if (shared) {
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
  }"""

    new_pattern = """  if (shared) {
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
  }"""

    content_normalized = content.replace("\r\n", "\n")
    if old_pattern in content_normalized:
        content_patched = content_normalized.replace(old_pattern, new_pattern)
        with open(socket_cpp, "w", encoding="utf-8", newline="\n") as f:
            f.write(content_patched)
        print("Successfully patched cppdap socket.cpp to eliminate self-deadlock.")
    else:
        print("cppdap socket.cpp already patched or pattern not found.")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        patch_cppdap(sys.argv[1])
    else:
        print("Usage: patch_cppdap.py <cppdap_source_dir>")
