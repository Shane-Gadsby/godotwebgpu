# 60-web-swizzle-answers: user answers on task 58's two questions

State: integrated (TASKS §4.76)
Owner: worker-60
Claimed: 2026-10-28
Needs: build, gpu
Branch: work/60
Results: webgpu_notes/work/results/60-web-swizzle-answers/

User answers on the hand-offs page, 2026-10-28 (task 58 asked; read `results/58-small-web-correctness/SUMMARY.md` and
`for-tasks.md` first, and TASKS §3 trap 55):

1. **`texture2drd-r8-web`: "Report it as R8/RG8 on the web".** A `Texture2DRD` wrapping an R8 or RG8 RenderingDevice
   texture is treated as L8 / LA8 (R,R,R,1 / R,R,R,G) through a view swizzle WebGPU does not have, so on the web it
   shows red (red/green) while natively gray. On the web, describe such textures as R8 / RG8 instead, so the format the
   engine reports (`get_format()`, `get_image()`) and what is drawn agree, and the one-time swizzle warning no longer
   fires for them. Natively nothing changes. Document it in the `Texture2DRD` class reference (`[b]Note:[/b]` for the
   web) and check a project that samples one in a material, native vs web.
2. **`opaque-subviewport-alpha-web`: "Write alpha 1 on the web".** Natively an opaque viewport's texture (no
   `transparent_bg`) reads alpha 1 through a swizzle; on the web it carries the colour buffer's alpha. Make the last
   pass that writes an opaque viewport's final target write alpha 1 on the web: the 3D tonemap pass (Forward+ and
   Mobile) and whatever finishes a 2D-only viewport (find it; the blit to the target or the canvas's final pass). Gate
   it so native output and transparent viewports are bit-identical. Prefer a uniform/flag the driver-agnostic code
   sets from `API_TRAIT_*` or the viewport's transparency over `#ifdef WEB_ENABLED` (CLAUDE.md, "Buffer mapping"
   paragraph). Test: a SubViewport whose scene lowers alpha (clear colour alpha 0.5, and a material writing ALPHA)
   drawn with blending in a parent scene, native vs web, 3D and 2D-only, opaque and transparent.

Then: remove "One difference remains" from the wiki-facing text in your for-docs.md, verify defaults bit-identical
(3 renderers natively), WebGPU vs native, smoke.sh (references regenerated if the tonemap interface changes), and the
Forward+ matrix arms touching the tonemap (`tonemap`, `tonemap_custom`, `glow`, `local_exposure`) with `--parity`.
