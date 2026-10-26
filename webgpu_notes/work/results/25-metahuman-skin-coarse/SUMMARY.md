# 25: MetaHuman skin looks coarse (worker-25, 2026-10-26)

## Cause

The skin shader's tiling pore normal (`detail_normal_map`) was too large: it was tiled at a fixed **16 repeats per
UV unit** on every skin mesh. The importer never scaled it to the mesh. It is **not web-only**: Vulkan and WebGPU
renders of the "before" cheek close-up differ by a mean of 0.0008 luma levels out of 255
(`looks/before_web_vs_native.jpg`).

Evidence (Ava, `~/Downloads/Ava.zip`):
- **The map is a generic, seamless micro-skin tile, not Ava's own.** `Head_DetailNormal.png` and
  `Body_DetailNormal.png` are byte-identical (same md5), 1024². Its wrap seam differs by 8.7 levels against 4.1
  between neighboring pixels, so it tiles. It holds ~406 strong pore marks (3σ divergence peaks) and ~1,273 weaker
  ones per repeat. `ExportPlan.json` lists Unreal's `Micro Skin Tiling` / `Micro Skin Normal Strength` parameters
  for `M_skin_unified_baked` by name only, with **no values**, and Epic's docs give none. So the tiling has to
  come from the mesh and from skin, not from the export.
- **UV density, measured on the imported LOD 0 meshes** (`uvdensity.gd`, `sqrt(3D area / UV area)` per triangle):
  - head: median **0.395 m per UV unit** (cheek 0.382, nose 0.338, forehead 0.375);
  - body: median **1.341 m per UV unit**, 3.4x coarser.

  At tiling 16, one repeat therefore covered 2.5 cm of face and 8.4 cm of arm. The face's pore marks landed
  ~1.2 mm apart, the big ones 2.5 mm apart; the arm's were 3.4x larger still (`looks/native_arm_0.5m_before_after.jpg`
  is the clearest "huge pores" picture).
- **Suspects ruled out:**
  - **The 2048 texture size limit.** In the 8K `Head_Normal`, the slope energy between 0.2 and 5 mm has a median
    wavelength of 1.0-1.2 mm (cheek, nose, forehead), and the 2048 import keeps 92% of its rms (0.022 against 0.024).
    Rendered with the 8K normal and SRMF, the cheek's high-pass luma std goes from 1.49 to 1.58, against 1.49 to 0.86
    with the detail map off (`looks/suspect_8k_maps.jpg`).
  - **Web filtering, SamplerLodBias and mips.** WebGPU matches Vulkan (above); every map has mipmaps.
  - **Not coarseness, but a real bug:** both normal maps are RG-compressed and sample blue as 0, so the old shader's
    `base_n.z` was -1, not the reconstructed z. Godot rebuilds z from `NORMAL_MAP.xy` anyway, so this only weakened
    steep normals by `1/sqrt(1+|xy|²)`. Fixed in passing.

## What changed (`work/25`)

- `modules/metahuman/editor/editor_scene_importer_metahuman.cpp`: new `_fit_skin_detail()`, called for the LOD 0 head
  and body skin. It sets `detail_normal_tiling = median m-per-UV / skin_detail_size`, so one repeat covers the same
  skin on every mesh. With Ava, that gives **head 32.88, body 111.71**. It also sets `detail_fade_begin/end` to
  0.5x and 1x the mesh's LOD 0 range: head 0.75-1.5 m, body 1.5-3 m. A single-LOD mesh gets no fade.
  - New import option `metahuman/materials/skin_detail_size`, default **0.012 m**. Why 1.2 cm:
    - the map's ~1,273 pore marks then land at ~880 per cm², inside the face's sebaceous follicle density
      (~400-900 per cm²);
    - the map's slope-energy median wavelength becomes 0.35 mm (`spectra.py`), below the ~1 mm the baked 8K normal
      already carries, so the detail adds what the scan lacks instead of competing with it.
  - Strength stays 0.5: nothing in the export says otherwise.
- `modules/metahuman/editor/metahuman_shaders.h`, `SKIN` only:
  - fades the detail with view depth: Unreal turns Micro Skin Detail normals off from face LOD 1 (Epic, "MetaHuman
    Materials and Textures");
  - rebuilds z for both RG normals before the whiteout blend.

  It still takes one sample per map. The fade is a multiply, not a branch: a `textureSample` in non-uniform control
  flow is invalid WGSL. Per the lead's note, the cloth shader is untouched. It has the same `.z = -1` pattern, noted
  for later.
- The `EditorSceneFormatImporterMetaHuman.xml` description mentions the option.

## Verification (actual numbers)

- `webgpu_tests/metahuman/run.sh ~/Downloads/Ava.zip --shots` with the work/25 editor: **DONE failures=0** (13
  PASS), all 4 shots saved.
- Native Vulkan (RTX 4080 SUPER) cheek close-up at 0.25 m. The pore detail is the render minus the same render with
  the detail off:
  - before: rms 1.54, median feature wavelength 9.6 px (~1.1 mm);
  - after: rms 1.27, 5.5 px (~0.6 mm).

  Arm at 0.5 m: before rms 1.71 at 7.1 px, after 0.70 at 2.9 px.
- The imported materials read back as `MI_Face_Skin_Baked_LOD0_VT` tiling 32.88, fade 0.75-1.50, and
  `MI_Body_Baked_VT` tiling 111.71, fade 1.50-3.00.
- The web export with the work/25 editor and the lead's nothreads release template produced no errors.
- `webgpu_notes/work/smoke.sh`:
  ```
  identical interface: 276/276, mismatched: 0, missing in candidate: 0, not in reference: 0
  against the stored reference: 552/552 identical, 0 differ, 0 missing, 0 not in reference
  validation: 276 modules, 0 invalid, 0 of them expected
  SMOKE OK (smoke-20261026-163408-2736298)
  ```
  The change does not touch the converter or the renderer.
- One slip: two short native "after" renders (about 20 s, no timing) ran while worker-26 held `gpu`. A failed
  `lease.sh take` did not stop the command chain. Every other GPU run was under my lease.

## What the lead must still run

- **WebGPU "after" close-ups.** The `gpu` lease was held by worker-26 and then worker-24 (until 19:00) once the fix
  was built. The export is ready at
  `/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/86fd2a38-7e49-49e0-a3fd-d31362379e4b/scratchpad/web_after`
  (scratch project `.../scratchpad/after`, scene `webclose.tscn`). Run it under the `gpu` lease:
  `node results/25-metahuman-skin-coarse/capture_web.mjs <web_after> <out> web_after`
  (headed system Chrome; it prints the adapter, which must not be the fallback). Compare against
  `.../scratchpad/close/web_before_*.png`, which matches native.
- **Re-import Ava in cameraSim.** The existing `head.dna` import keeps the old materials until it is re-imported.
- **The user's look** on the hand-offs page (`for-tasks.md`).

## Risks

- The 1.2 cm repeat is derived, not Unreal's own value, which the export does not carry. It is an import option, and
  `detail_normal_tiling` / `detail_normal_strength` stay editable on the material.
- No region mask: Unreal's `Micro MaskA-D` masks are not exported, so the lips still get pores, as they did before.
  The SRMF alpha (black on lips and eyelids) looks like a fuzz mask and is deliberately not used as a pore mask.
- The UV density is a median over the mesh, so the scalp and neck (0.5-0.7 m per UV unit) get a repeat ~1.5x larger
  than the face's. That is the same as Unreal's UV-space tiling.
- Projects that already set `detail_normal_tiling` on an extracted material keep their value. Materials embedded in
  the import are overwritten on re-import, as before.

State: ready-for-integration
