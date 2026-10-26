# For TASKS.md (§2.33 follow-up, and a §3 trap)

**§2.33, 2026-10-26: MetaHuman skin looked coarse (task 25).** The tiling pore normal was tiled at a fixed 16 repeats
per UV unit, so pores were ~2x too large on the face and ~7x too large on the body.
- **The map:** `Head_DetailNormal` = `Body_DetailNormal`, a generic 1024² micro-skin tile. The export names Unreal's
  `Micro Skin Tiling` but carries no value.
- **The UVs:** Ava's body UVs are 3.4x coarser than the face's (median 1.34 against 0.395 m per UV unit).
- **The fix:** the importer now scales the tiling from each skin mesh's UV density, so one repeat covers 1.2 cm of skin
  (`metahuman/materials/skin_detail_size`). With Ava that is head 32.9, body 111.7. The detail fades out over the
  LOD 0 range, as Unreal turns it off from face LOD 1.
- **Also fixed:** the skin shader used blue = 0 of the RG-compressed normal maps as z.
- **Ruled out by measurement:** the 2048 size limit (it keeps 92% of the 8K normal's mm-scale slope energy) and
  anything web-only (WebGPU = Vulkan to 0.0008 levels).
- **Open:** the lips get pores (Unreal's Micro MaskA-D region masks are not exported), and the cloth shader has the
  same z = -1 pattern.

**§3 trap (proposed): an RG-compressed normal map samples blue as 0.** Godot imports normal maps as RG (BC5 / RG8)
with swizzle b = ZERO. A custom shader that reads `.rgb * 2 - 1` gets z = -1, not the normal's z. Rebuild z from xy, or
pass xy to `NORMAL_MAP`, which Godot completes itself.

## Questions for the user

- **Look: MetaHuman skin pores, before/after (task 25).** Is the new pore size right, and is the strength right?
  Frames, all native Vulkan:
  - `results/25-metahuman-skin-coarse/looks/native_cheek_0.25m_before_after.jpg` (before / after / detail off);
  - `native_face_0.45m_before_after.jpg`;
  - `native_arm_0.5m_before_after.jpg`.

  WebGPU matched Vulkan in the "before" pair (`before_web_vs_native.jpg`); the WebGPU "after" frames come from the
  lead's capture. Choices:
  - **keep (default):** 1.2 cm repeat, strength 0.5;
  - finer: 0.8 cm;
  - coarser: 1.6 cm;
  - weaker: strength 0.3;
  - other (type it).
