# Third-party notices

The template implementation is original MIT-licensed code using the plugin
interfaces illustrated by [ascurl](https://github.com/metamod-fallguys/ascurl).
Dependencies retain their original source, copyright notices and licenses.
The template license does not replace dependency or individual-file licenses.

- [Metamod](https://github.com/metamod-fallguys/metamod): see
  `metamod/LICENSE` and individual source/header notices. Legacy Metamod files
  credit Will Day and contain GPL version 2-or-later notices with a Half-Life
  engine linking exception. HLSDK files contain Valve/Id notices.
- [ASExt](https://github.com/metamod-fallguys/asext): see `asext/LICENSE`
  (MIT, copyright hzqst) and individual legacy file notices.
- Transitive dependencies include the customized AngelScript SDK, Capstone,
  procmap, RapidJSON, Chocobo1Hash and FormatValidation. Their notices remain
  in the dependency source trees fetched by Git or CMake.

Binary packages include dependency license/notice files under `build/licenses/`
and record the exact source commits in `build/dependencies.json`. These commits
identify the corresponding complete source, including per-file notices.
