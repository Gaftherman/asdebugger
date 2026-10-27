# astemplate

A minimal native AngelScript plugin for Sven Co-op using
[metamod-fallguys](https://github.com/hzqst/metamod-fallguys).
Use GitHub's **Use this template** button to start your own plugin repository.
The pinned `metamod` and `asext` submodules are built from source alongside
`astemplate`; installing the resulting package requires no separate DLL download.

## Build

Requirements: Git, CMake 3.21+, Python 3.8+ (3.12 for repository tests), internet
access for the first dependency/gamedata download, and a 32-bit toolchain.
Windows requires Visual Studio 2022 with Desktop development with C++.
Linux requires GCC/G++ multilib, for example on Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y cmake git python3 gcc-multilib g++-multilib
```

```sh
git clone --recursive https://github.com/metamod-fallguys/astemplate.git
cd astemplate
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

For Debug, replace every `Release` with `Debug`. Use a separate build tree for
each configuration. Installation defaults to `install/x86/<Config>`; an explicit
`CMAKE_INSTALL_PREFIX` or `cmake --install --prefix` overrides it. The packaging
script uses the default prefix. Windows installs matching PDBs for all three DLLs.
Only x86 is supported; Linux platform flags are set before compiler detection.

Metamod resolves its transitive dependencies from recursive submodules or pinned
FetchContent revisions, including the customized AngelScript headers. Existing
clones can be selected with upstream `*_SOURCE_PATH` variables such as
`ANGELSCRIPT_SOURCE_PATH` and `FORMAT_VALIDATION_SOURCE_PATH`. Metamod and ASExt
themselves always build from this repository's submodules.

ASExt's gamedata is downloaded, pruned, validated and installed by its upstream
build target. `MMFG_SYNC_GAMEDATA=OFF` is suitable only when the build tree already
contains a compatible catalog; the package verifier rejects missing gamedata.

## Install and run

1. Stop the Sven Co-op server. Extract a release archive and merge its
   `build/addons/` directory into the server's `svencoop/addons/` directory.
   For a local build, copy `install/x86/Release/addons/` instead.
2. Configure `svencoop/liblist.gam` to load Metamod for your platform:
   `gamedll "addons/metamod/dlls/metamod.dll"` on Windows or
   `gamedll_linux "addons/metamod/dlls/metamod.so"` on Linux.
3. Merge the appropriate lines from `addons/metamod/plugins.ini.example` into
   your existing `addons/metamod/plugins.ini`. Load asext before astemplate.
   Preserve your existing plugin entries and server configuration.
4. Copy the package's `build/scripts/plugins/astemplate.as` (or the local install's
   `scripts/plugins/astemplate.as`) to `svencoop/scripts/plugins/astemplate.as`.
   Add this block to the existing `svencoop/default_plugins.txt`:

   ```text
   "plugin"
   {
       "name" "ASTemplate example"
       "script" "astemplate"
   }
   ```

5. Start the server. `meta list` should show asext and astemplate running.
   The example prints `ASTemplate API version: 1` in the server console.

The native plugin registers the global AngelScript function
`int ASTemplateVersion()` through ASExt's documentation initialization callback,
using `asCALL_CDECL`, and defines `METAMOD_PLUGIN_ASTEMPLATE` for script builds.
It must load once at server startup, before AngelScript initializes. Restart the
server to replace it: ASExt retains documentation callbacks and native function
addresses, so runtime unload/reload is deliberately disabled.

The ABI smoke test loads the actual native library with mock Metamod/ASExt APIs.
It checks registration, the returned version, exported entrypoints and failure
paths. It does not replace a test on a real Sven Co-op server. Missing asext,
missing exports and rejected initialization callbacks produce errors in the
Metamod log. Missing private game symbols require a supported server build and
matching ASExt gamedata.

## Customize

Rename the CMake project/target, plugin metadata and paths in `src/plugin.cpp`,
the sample API/macro and script, loading configuration, and package/artifact names
in `scripts/package.py` and the workflows. Update URLs, authors and this README.
Add your native functions alongside `ASTemplateVersion()` and register them in
`RegisterDocumentation()`. Keep public dependency includes under their SDK targets;
avoid depending on `metamod/src` or `asext/src` internals.

## CI and releases

- Branch pushes and pull requests build Windows and Linux x86 Debug/Release,
  run tests and formatting checks, then upload complete packages as artifacts.
- Pushing any new tag builds both platforms in Release and publishes a GitHub
  Release. For example, `git tag v0.1.0` followed by `git push origin v0.1.0`.
  Rerun a failed release workflow to retry uploading its packages.
- Packages retain the `build/addons/` layout and include example scripts,
  licenses, documentation and `build/dependencies.json` with exact Git revisions.
  Initial repository setup does not create a release tag.

`build.yml` is reusable: `ref` selects an immutable source revision,
`asext_sha`/`metamod_sha` together select candidate dependency commits, and
`release_only` limits the configuration matrix to Release. Normal builds use the
committed gitlinks. These workflows operate on the current repository, including
repositories generated from this template.

## Upstream updates

`update-upstream.yml` runs daily at 02:17 UTC and can be started manually.
To enable writes, create the repository secret **UPSTREAM_UPDATE_TOKEN** using a
fine-grained personal access token restricted to your template-derived repository
with **Contents: read and write**. Your organization may require approval for
the token. Branch/tag rules must permit the token owner to push updates; the
workflow never bypasses protection or force-pushes. The token is used only by the
publication job, after validation; it is not passed to candidate builds.

The updater finds ASExt's latest published non-draft, non-prerelease release,
resolves its actual tag to a commit (including annotated tags), and selects the
current Metamod `main` commit. It builds that exact combination on both platforms
before committing the submodule changes and atomically pushing the default branch
and `asext-release-<release-id>` tag. The tag triggers the ordinary release workflow.
A PAT is used because pushes using `GITHUB_TOKEN` do not trigger downstream push
workflows. No release, an already handled release, or a missing secret is a normal
no-op. API/build errors fail the run without pushing. If the default branch moves
during validation, the update is deferred until the next check. The GitHub job
concurrency group serializes scheduled/manual updates.

To disable automatic writes, remove the secret. To update dependencies manually:

```sh
git -C asext fetch origin main
git -C asext checkout --detach origin/main
git -C metamod fetch origin main
git -C metamod checkout --detach origin/main
git submodule update --init --recursive
# Build and test both platforms before committing:
git add asext metamod
git commit -m "chore(deps): update metamod and asext"
```

## Development checks

```sh
python -m unittest discover -s tests -p "test_*.py" -v
python -m pip install clang-format==23.1.3
cmake -S . -B build/format -DFORMAT_VALIDATION_ONLY=ON
cmake --build build/format --target format-check
# Apply formatting when needed:
cmake --build build/format --target format
```

Formatting uses the shared pinned FormatValidation tooling. Dependencies and
HLSDK are excluded from the template's formatting target. See [THIRD_PARTY.md](THIRD_PARTY.md)
for preserved dependency and SDK notices.
