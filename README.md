# asdebugger

A native AngelScript DAP (Debug Adapter Protocol) debugger plugin for Sven Co-op using
[metamod-fallguys](https://github.com/hzqst/metamod-fallguys) and
[cppdap](https://github.com/google/cppdap).

Developed by **Gaftherman** ([asdebugger](https://github.com/Gaftherman/asdebugger)).

The plugin integrates directly with Sven Co-op's AngelScript VM via Metamod and `asext`,
exposing a standards-compliant DAP TCP server (port 9002). This enables full source-level
interactive debugging directly inside Visual Studio Code (breakpoints, call stack, local/global
variables, compound object evaluation, stepping, and execution control) without crashes or server stalls.

The pinned `metamod` and `asext` submodules are built from source alongside `asdebugger`;
installing the resulting package requires no separate Metamod DLL download.

## Features

- **Debug Adapter Protocol (DAP)**: Standard TCP socket server on port `9002` compatible with VS Code.
- **Source-Level Breakpoints**: Set breakpoints in any `.as` plugin file before or during runtime.
- **Call Stack Navigation**: Complete stack trace inspection across AngelScript execution frames.
- **Variable Inspection**:
  - Primitive types (`int`, `float`, `bool`, `uint`, `double`, etc.).
  - Strings (`string`).
  - Compound objects and structs with member expansion (e.g., `Vector` with `x`, `y`, `z`).
  - Scopes: *Locals*, *Parameters*, *Registers*, and *Globals* (including engine subsystems).
- **Execution Flow Control**: Step over (`next`), Step in (`stepIn`), Step out (`stepOut`), Continue, and Pause.
- **Non-blocking Server**: Safe thread suspension and resume using condition variables; dedicated server remains responsive.
- **Native Macro & Helpers**: Injects preprocessor definition `METAMOD_PLUGIN_ASDEBUGGER` and native script function `DebugBreak()`.

## Build

Requirements: Git, CMake 3.21+, Python 3.8+ (3.12 for repository tests), internet
access for the first dependency/gamedata download, and a 32-bit toolchain.
Windows requires Visual Studio 2022 with Desktop development with C++.
Linux requires GCC/G++ multilib, for example on Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y cmake git python3 gcc-multilib g++-multilib
```

Clone recursively to fetch the submodules:

```sh
git clone --recursive https://github.com/Gaftherman/asdebugger.git
cd asdebugger
# Also needed after pulling a change to the submodules:
git submodule update --init --recursive
```

Windows (PowerShell or a developer command prompt):

```sh
cmake -S . -B build/x86/Release -A Win32
cmake --build build/x86/Release --config Release --parallel 4
ctest --test-dir build/x86/Release -C Release --output-on-failure
cmake --install build/x86/Release --config Release
python scripts/package.py --config Release --platform windows
```

Linux:

```sh
cmake -S . -B build/x86/Release -DCMAKE_BUILD_TYPE=Release
cmake --build build/x86/Release --parallel 4
ctest --test-dir build/x86/Release --output-on-failure
cmake --install build/x86/Release
python3 scripts/package.py --config Release --platform linux
```

For Debug, replace every `Release` with `Debug`. Use a separate build tree for each
configuration. Installation defaults to `install/x86/<Config>`; an explicit
`CMAKE_INSTALL_PREFIX` overrides it. The packaging script creates clean archives
in `build/packages/`.

## Install and run

1. Stop the Sven Co-op server. Extract a release archive (`asdebugger-windows-x86-Release.zip`
   or `asdebugger-linux-x86-Release.tar.gz`) and merge its `build/addons/` directory into the
   server's `svencoop/addons/` directory. For a local build, copy `install/x86/Release/addons/`.
2. Configure `svencoop/liblist.gam` to load Metamod for your platform:
   - Windows: `gamedll "addons/metamod/dlls/metamod.dll"`
   - Linux: `gamedll_linux "addons/metamod/dlls/metamod.so"`
3. Merge the appropriate lines from `addons/metamod/plugins.ini.example` into
   your existing `addons/metamod/plugins.ini`. Load `asext` before `asdebugger`:
   ```ini
   win32 addons/metamod/dlls/asext.dll
   win32 addons/metamod/dlls/asdebugger.dll
   ```
   (On Linux, replace `.dll` with `.so`).
4. Copy the sample script `examples/asdebugger_example.as` to `svencoop/scripts/plugins/asdebugger_example.as`
   and add it to `svencoop/default_plugins.txt`:
   ```text
   "plugin"
   {
       "name" "ASDebugger Example"
       "script" "asdebugger_example"
   }
   ```
5. Start the server. Run `meta list` in the server console to confirm `asext` and `asdebugger`
   are loaded and `RUNNING`. The server will output:
   ```text
   [ASDEBUG] Metamod AngelScript DAP Debugger initialized.
   [ASDEBUG] DAP listening on 0.0.0.0:9002
   ```

## Debugging with Visual Studio Code

### 1. Launch Configuration (`.vscode/launch.json`)

Create `.vscode/launch.json` in your workspace or game scripts folder:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Attach to Sven Co-op DAP Server",
      "type": "asdebugger",
      "request": "attach",
      "server": "127.0.0.1",
      "port": 9002,
      "stopOnEntry": false,
      "trace": true
    }
  ]
}
```

### 2. Debugging Workflow

1. Open your AngelScript scripts folder in VS Code.
2. Set a breakpoint on any line of code (press <kbd>F9</kbd>).
3. Press <kbd>F5</kbd> to attach to the server.
4. When the game engine executes that line of script:
   - Execution pauses automatically.
   - VS Code highlights the active statement.
   - The **Variables pane** displays all *Locals*, *Parameters*, *Registers*, and *Globals*.
   - Expand objects (e.g. `Vector`) to view inner variables (`x`, `y`, `z`).
   - Use <kbd>F10</kbd> (Step Over), <kbd>F11</kbd> (Step Into), or <kbd>F5</kbd> (Continue).

## Native Plugin & Macro Support

The plugin defines the preprocessor macro `METAMOD_PLUGIN_ASDEBUGGER`:

```angelscript
#if METAMOD_PLUGIN_ASDEBUGGER
    // Code executed only when the debugger plugin is active
#endif
```

It also provides a native breakpoint function callable from AngelScript:

```angelscript
DebugBreak();
```

## CI and releases

- Branch pushes and pull requests build Windows and Linux x86 Debug/Release,
  run smoke tests and formatting checks, and package complete release artifacts.
- Pushing any new tag (e.g., `git tag v1.4.0 && git push origin v1.4.0`) triggers
  the release workflow and publishes a GitHub Release with prebuilt binaries.
- Packages retain the `build/addons/` layout with `dependencies.json` recording
  exact Git submodule revisions.

## Upstream updates

`update-upstream.yml` checks daily for updates to upstream `metamod` and `asext`
releases and performs automated validation before publishing.

## Development checks

```sh
python -m unittest discover -s tests -p "test_*.py" -v
python -m pip install clang-format==23.1.3
cmake -S . -B build/format -DFORMAT_VALIDATION_ONLY=ON
cmake --build build/format --target format-check
```

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
See [THIRD_PARTY.md](THIRD_PARTY.md) for preserved dependency notices (Metamod, ASExt, cppdap, HLSDK).
