# Task 40: wiki changes

Written against the wiki pages as they are now (`/mnt/109313D2109313D2/godot-editors/godotwebgpu.wiki`, not edited).

## Engine-Additions-Camera-Exposure.md

**Intro, second paragraph**: replace

> Both default to the old behavior (Average metering, auto white balance off), so existing projects render exactly as before.

with

> Both default to the old behavior (Average metering, auto white balance off). One deliberate exception: Average now
> really meters the whole frame (see the note under the modes table), so an auto-exposed scene can come out slightly
> brighter or darker than before.

**Modes table, Average row**: replace the "What it meters" cell with

> The whole frame, every pixel weighted alike.

**New paragraph right after the modes table** (before the property table):

> **Average changed in this engine.** Godot's original auto exposure reduction dropped the right and bottom edges of
> the frame: the right 20% of a 1080p frame and the bottom 27% of a 720p one never reached the meter, because each
> step of the reduction rounded its size down. Average now meters every pixel. A project auto-exposed with an earlier
> version, whose edges are brighter or darker than the rest of the frame, is exposed differently: in our test scene
> by 0.07 stop at 1080p and 0.32 stop at 720p. As a side effect, the same scene now meters the same at every
> resolution (it did not before).

**Property table, new row** (after `auto_exposure_metering_highlight_percentile`):

> | `auto_exposure_metering_log_average` | `false` | Average, Center-Weighted and Spot take the log-average of the frame (with their weights) instead of the arithmetic mean. Highlight-Weighted is a percentile, the same either way. |

**New section after the "backlit interior" example** (before "## Auto white balance"):

> ### Arithmetic or log-average
>
> A light meter adds up light, so by default every mode takes the **arithmetic mean** of the frame (with its weights).
> Many games instead expose for the **log-average** (the geometric mean, `exp(mean(log(value + 0.0001)))`, the scene
> "key" of Reinhard et al.'s photographic tone reproduction, 2002). Turn on `auto_exposure_metering_log_average` to use
> it. The difference shows in contrasty frames: a lamp, the sun or a window raises the mean a lot but the log-average
> very little, so with the log-average the frame is exposed brighter and the mid-tones stay put when a bright light
> enters the picture. In the test scene (a dark room, a window, four lamps) the log-average reads 4 stops below the
> mean for the whole frame and 2 stops below it center-weighted; for a spot on an evenly lit subject the two agree.
> Large black areas pull the log-average down hard; the max sensitivity (Practical) or max exposure value (Physical)
> limits how far that brightens the frame.
>
> ```gdscript
> attributes.auto_exposure_enabled = true
> attributes.auto_exposure_metering_log_average = true	# Expose for the scene's key, not its mean.
> ```

**Renderer support table**: add a row

> | Log-average metering | yes | no (no auto exposure) | no (no auto exposure) | yes |

## Engine-Additions-Camera-Bokeh-and-Glare.md

**Starburst property table**, replace the `starburst_strength` row with

> | `starburst_strength` | `20.0` | `1.0` is the physical amount, which only something as bright as the sun shows. The default `20` makes ordinary game lights show spikes (they usually want `10` to `50`). At most 90% of a highlight's light goes into spikes. |

(The example below it, `starburst_strength = 30`, stays.)

**Halation property table**, add a row after `halation_radius`:

> | `halation_high_quality` | `false` | Builds the glare at full resolution instead of half, so halos smaller than the half-resolution blur keep their size: at 720p the radius-1 halo measures 3.4 px against the physical 3.4 px (3.95 px without it), radius 0.5 measures 1.9 px against 1.7 px (3.95 px without it, the floor). Costs a full-resolution blur and a 4x larger glare chain while halation is on (about 0.2 ms more at 1080p on a desktop GPU). Forward+ and web only; Mobile's chain is already sharp enough at radius 1. |

**Performance section**: after "...builds one shared half-resolution blur chain (quarter resolution on Mobile) for that
frame;", add

> with `halation_high_quality` the chain starts at full resolution instead (Forward+), and every glare effect of that
> camera reads it; their sizes stay the same.

> On an RTX 4080 SUPER at 1920x1080, halation costs about 0.08 ms of GPU time with the standard chain and 0.26 ms
> with `halation_high_quality`.
