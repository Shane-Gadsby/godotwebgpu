# 40-camera-answers-wave2: user answers on tasks 35/36 (Average metering fix, log-average option, sharper halation option, starburst default)

State: open
Owner: worker-40
Claimed:
Needs: build, gpu
Branch: work/40
Results: webgpu_notes/work/results/40-camera-answers-wave2/

Read `webgpu_notes/work/camera-batch.md` first. User answers on the hand-offs page (2026-10-28) to questions from tasks
35 and 36 (merged: TASKS.md §4.53, §4.54; their results folders hold the tools and numbers):

## Goal

1. **`average-dropped-edges` -> Fix it.** Godot's Average auto exposure reduction ignores the right ~20% of a 1080p
   frame (and the bottom ~27% at 720p) because each reduction level rounds its size down (task 36 SUMMARY). Make Average
   meter the whole frame (task 36 already has an exact chain for the new modes; reuse it or fix the original
   reduction). This deliberately changes existing auto-exposed projects (up to about a third of a stop); say so in the
   class docs' Average description and in for-tasks.md. Forward+ (and wherever else Average runs today).
2. **`metering-statistic` -> arithmetic stays the default; add log-average as an option.** A property on
   CameraAttributes (e.g. `auto_exposure_metering_log_average`, default false) that makes every metering mode use the
   weighted log-average (Reinhard 2002's key: exp(mean(log(L + eps)))) instead of the arithmetic mean. Defaults
   unchanged.
3. **`halation-min-size` -> add the sharper level as an option, current behavior default.** A CameraAttributes
   property (e.g. `halation_high_quality` or a quality enum, default the current chain) that adds a full-resolution
   first level so halos can be physically small (radius 1 is ~0.5% of frame height; today's minimum is ~1%). Measure the
   halo size at radius 1 against task 35's model (it read 6.82 px vs 3.36 px predicted at 720p) and the GPU cost.
4. **`starburst-default-strength` -> 20.** Default `starburst_strength` becomes 20 (starburst itself stays off by
   default, so defaults stay bit-identical). Class docs, and anything in task 35's tools that assumed 1.0.

## Verification

Defaults pixel-identical to the pre-change editor in all three renderers, **except** auto-exposed scenes, whose Average
change is intended: measure it (metered value vs a Python mean of the whole frame, task 36's `analyze_meter.py`
reference, at 1080p and 720p) and show the old/new difference. Log-average vs a Python reference per mode. Halation radius
1 with the option on vs the model, and its GPU cost. WebGPU vs native for each. Smoke. for-docs.md: the changed wiki
paragraphs (the wiki already has Camera pages: Engine-Additions-Camera-Exposure.md and
Engine-Additions-Camera-Bokeh-and-Glare.md in /mnt/109313D2109313D2/godot-editors/godotwebgpu.wiki; read them, don't edit
them).
