# 41-postprocess-survey: post-processing effects Godot lacks (TASKS.md §2.48)

State: integrated (lead: results merged; questions pp-* on the hand-offs page)
Owner: worker-41
Claimed: 2026-10-28
Needs: — (no build, no GPU; read code and docs)
Branch: work/41
Results: webgpu_notes/work/results/41-postprocess-survey/

## Goal

Do TASKS.md §2.48 as written there (read it first). Output in `results/41-postprocess-survey/`:
- `SURVEY.md`: the ranked gap list, each entry with what it is, what Unreal / Unity HDRP-URP / Blender (Eevee or
  compositor) offer for it (cite the docs page), what this fork's tree has today (grep evidence: file or "absent"),
  usefulness/realism, effort (sessions), per-renderer feasibility (Forward+, Mobile, Compatibility, WebGPU), web cost
  (passes, bindings, bandwidth; note WebGPU limits: 4 bind groups with set 3 shared by push-constant emulation, the full
  128-byte Forward+ tonemap push constant, task 33's CameraEffects UBO with its `reserved[15]` region), and where it
  belongs (Environment, CameraAttributes, Compositor effect, project settings).
- `for-tasks.md`: the "Questions for the user" section, one choice question per candidate group (build it / skip / later,
  with your recommended default), so the lead can put them on the hand-offs page.
Exclude everything the camera batch covers (TASKS.md §4.47-4.55 and tasks 40, 44). Check the tree before claiming a gap.
WebSearch/WebFetch are fine for engine docs. Commit only your results folder and the task file on work/41.
