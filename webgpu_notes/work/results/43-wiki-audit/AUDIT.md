# 43 wiki audit: every user-facing addition against the wiki

Wiki commit (local, not pushed): `823092f` on `godotwebgpu.wiki` master, on top of the lead's `4a4898d` (Camera).

Method: README.md feature sections and TASKS.md §4 (plus §2 items marked DONE) listed as features; each wiki page read
in full; every backticked identifier on the Engine-Additions and Box3D pages (332) grepped in the code/class reference
(all found, except three that are paths or examples, not settings); export options checked against
`platform/web/export/export_plugin.cpp`, Box3D settings against `modules/box3d_physics/` and Box3D's `types.c`
defaults, MetaHuman import options against `modules/metahuman/`, build flags against `build-linux.sh`, editor and
TextEdit names against `editor_settings.cpp` and `doc/classes/TextEdit.xml`, WebRTC against `modules/webrtc/` and its
commits.

| Feature | Source | Wiki page | Stale facts / action |
|---|---|---|---|
| WebGPU web export itself (Forward+/Mobile in the browser, setup, browsers, limits, async readback) | README "Browser support", "Compatibility", "Rendering features"; TASKS §2.31, §4.4, §4.12; FAQ §5 | **was missing** → Engine-Additions-WebGPU-Export (new) | New page: setup, `rendering_method.web`, browser table, 48-texture Forward+ → Mobile fallback, differences table (MSAA 4x, wireframe, HDR, XR/VRS, lost device, border color, alpha_to_one, stencil masks, texture budget, 16-bit unorm), readback polling |
| Release downloads: double-precision Linux editor, macOS Gatekeeper | TASKS §4.35, `linux_builds.yml` | Home (partial) | Added install step, double editor, macOS first-launch note |
| WebRTC web/native parity fixes (F1-F5, F7, F8, F10; 50 ms `unreliable_lifetime`) | TASKS §4.20, commits 4e7919e241, 5398356e71, 15c7a1ccc3, 5a00ef9df3, d0fe1460eb | **was missing** → Notable Fixes, new WebRTC section | Added |
| Shader baking, separate pack, variant pruning, WGSL cache, prunes | README; TASKS §4.10, §4.23, §2.27 | Shader-Baking | "Two caches..." list had the bake-isolation item as a misplaced first bullet; moved to its own section |
| Bake isolation (capability answers stay in the baker) | TASKS §4.43 | Shader-Baking, Notable Fixes | OK (restructured as above) |
| Shader-bake reporting names shader and stage; unsupported variants verbose | TASKS §4.27 | **was missing** → Shader-Baking "Export log" | Added (messages checked in `wgsl_bake_subprocess.cpp`) |
| Per-family texture packs | TASKS §2.18 | Web-Export-Options | OK |
| `variant/separate_render_thread`, threaded rendering | TASKS §2.5 | Threaded-Rendering-on-the-Web | OK |
| `html/orientation`, `html/fullscreen_on_first_tap`, wake lock, high-performance adapter | TASKS §4.37 | Web-Export-Options, Web-Platform | OK |
| `rendering/occlusion_culling`, Embree in web templates | TASKS §4.38 | Web-Export-Options | OK; Building lacked `--no-web-raycast` (added) |
| Loading screen per-stage text | TASKS §4.24, §2.49 | Web-Export-Options | Not touched (task 42) |
| Loading bar covers engine startup | TASKS §4.11 | Web-Platform | OK |
| Warning channel, verbose readback note, pointer lock | TASKS §4.28 | Web-Platform, Notable Fixes | OK |
| Web audio parity, export/runtime warnings | README "Web audio"; TASKS §4.30 | Web-Audio | OK |
| Editor default folders | TASKS §4.34 | Editor-Default-Folders | OK |
| Drag and drop onto lists | TASKS §4.31 | Drag-and-Drop-onto-Lists | OK |
| Mouse-button shortcuts, multiple-caret modifier | README | Script-Editor-Input | OK (setting and `caret_multiple_modifier` verified) |
| Update check against the fork's releases | TASKS §4.45 | Engine-Additions (inline) | OK |
| Camera white balance | TASKS §4.42 | White-Balance | OK (camera batch not re-audited, per instructions) |
| Camera grain, LUTs + `.cube` importer, lens, bokeh/glare, motion, exposure, finish/monitoring, autofocus/shake | TASKS §4.47-4.55 | Camera* pages (lead, 4a4898d) | Not audited beyond the identifier grep (all found) |
| NaN guards (DOF, glow, SSR, auto exposure), MetaHuman strand NaN | TASKS §4.41, §4.44 | Notable Fixes | OK |
| PrimitiveMesh / gradient / CompressedTexture readback fixes | TASKS trap 54 | Notable Fixes | OK |
| MetaHuman importer | TASKS §2.33, §2.46 | MetaHuman-Importer | OK; all 23 import options match the code; cloth normal z still open (§2.46), so "a fix is planned" stays |
| Box3D physics | TASKS §4.6 | Box3D-* (6 pages) | OK; all project settings and defaults match the code and Box3D's world defaults |
| Braced GDScript | TASKS §4.33, docs/braced_gdscript/ | Braced-GDScript-* (7 pages) | Identifier grep only; not re-read against the developer docs |
| Build scripts, ccache, Emscripten patches, AccessKit | TASKS §4.5, §4.22, §4.26, §4.32 | Building | `--no-web-raycast` and the `--skip-tint-cli` alias were missing; added |
| Light projectors, positional shadows, MSAA/TAA/DOF fixes, Forward+ support, SDFGI | TASKS §4.4, §4.7, §4.12, §4.29 | README rendering table (linked from the new page) | Renderer parity rather than additions; limits now on the new page |
| Internal work (converter swap, spec constants, lineage repair, CI, per-variant gen sources, toolchain) | TASKS §4.9, §4.16-4.18, §4.21, §4.25, §4.36, §4.46 | none | Not user-facing |

## Not verified

- The Camera pages beyond identifier existence (task scope: the lead drafted them).
- Browser support statements (copied from README, not re-run).
- The macOS Gatekeeper steps (from TASKS §4.35; no Mac).
- Whether `RenderingDevice.*_get_data_async()` work on WebGPU; the new page documents only the polling pattern the FAQ gives.
