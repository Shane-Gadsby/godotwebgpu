# 29-white-balance-units: text for the shared docs

## TASKS.md §4.42 (replace these lines)

Header paragraph, the property sentence becomes:

> lives there): `white_balance_temperature` (Kelvin, 2000–10000, default 6500) and `white_balance_tint` (-150..150,
> default 0), plus `RenderingServer.camera_attributes_set_white_balance(rid, temperature, tint)`. User wiki page: Engine
> Additions → Camera White Balance.

The **Model** bullet, the tint clause becomes:

> `light_temperature`, tint along the isotherm at 1/3000 Duv per unit, so ±150 is ±0.05 Duv (the extent of Unreal's
> `WhiteTint` at ±1; Lightroom-style numbers; positive = magenta image, as in Lightroom), von Kries adaptation in ...

The **Verified** bullet: "Forward+ arm `white_balance` (3200 K, tint 0.2)" becomes "(3200 K, tint 30; tint 0.2 before
task 29, the same frame)", and "±0.5 tint" becomes "±75 tint (±0.5 before task 29)".

The **User decisions** bullet becomes:

> - **User decisions** on the hand-offs page (`wb-tint-units`, `wb-model`, `wb-range`, `look-white-balance`): first
>   shipped with the defaults (-1..1 at 0.05 Duv, Planckian everywhere, 1000–15000 K); then the user picked
>   (2026-10-26) Planckian everywhere, **2000–10000 K** and tint **-150..150** keeping the physical extent (±150 = ±0.05
>   Duv, 1/3000 Duv per unit). Work task 29 (`work/29`) changed the inspector hints, the RenderingServer clamp
>   (`white_balance_compute_gains()`), the scale constant `WHITE_BALANCE_DUV_PER_TINT`, the class docs and the Forward+
>   arm (tint 0.2 → 30, frame unchanged). The old units were never released, so nothing converts them.

## Wiki: Engine-Additions-White-Balance.md (full revised text)

````markdown
A white balance setting on `CameraAttributesPractical` and `CameraAttributesPhysical`: a color temperature in Kelvin and a green-magenta tint, with the meaning they have on a real camera.

## Overview

The temperature is the light the camera is balanced for. At the default, 6500 K, nothing changes. A lower value tells the camera the light is warmer than daylight, so it compensates by turning the image blue: at 3200 K a scene lit by tungsten light looks neutral, and a daylight scene looks blue. A higher value does the opposite, for overcast or shade, and turns a daylight scene orange.

The tint corrects a green or magenta cast that the temperature alone cannot. A positive tint compensates for a green light (fluorescent tubes, foliage bounce) and turns the image magenta; a negative tint turns it green. This is the direction and the -150 to 150 scale of the tint slider in Lightroom and most raw converters.

White balance works in Forward+, Mobile and Compatibility, natively and on the web (WebGPU), with or without physical light units.

## Properties

Both are in the **White Balance** group of the inspector, on the shared `CameraAttributes` base class, so `CameraAttributesPractical` and `CameraAttributesPhysical` both have them.

| Property | Type | Default | Range | Effect |
|---|---|---|---|---|
| `white_balance_temperature` | float (K) | `6500.0` | 2000 to 10000 | Color temperature of the light the camera is balanced for. |
| `white_balance_tint` | float | `0.0` | -150 to 150 | Green-magenta correction, 1/3000 Duv per unit (0.05 Duv at either end). Positive turns the image magenta. |

From code, `RenderingServer.camera_attributes_set_white_balance(camera_attributes, temperature, tint)` sets both on a camera attributes RID. Values outside the ranges are clamped.

As with exposure, a `CameraAttributes` on a `Camera3D` overrides the one on the `WorldEnvironment`, which is used by every camera without its own, including the editor viewport.

## Matching lights

The temperature is read on the same curve `Light3D.light_temperature` uses (with physical light units enabled). So a light set to a temperature, seen through a white balance of the same temperature, looks exactly like a default 6500 K light at the default white balance. To make a tungsten-lit interior look neutral, set the lights to 3200 K and the camera's white balance to 3200 K.

## How it works

- The light's color is taken on the Planckian (black body) locus at the given temperature, in CIE 1960 UCS, with the Krystek approximation `Light3D` uses. The tint moves it perpendicular to the locus by `tint / 3000` Duv, so the full range of ±150 spans ±0.05 Duv, the extent Unreal Engine's `WhiteTint` has at ±1.
- A von Kries chromatic adaptation in Bradford cone space maps that white onto the white of the same model at 6500 K and tint 0. Using the model's own 6500 K, rather than D65, is what makes the default exactly neutral.
- The CPU computes the three cone gains when the property changes. The tonemapper applies them in linear scene-referred color: after exposure, FXAA and the glow that is added before tonemapping, before the tonemapper itself. So white balance happens before the tonemapping curve, the way a camera balances the sensor's raw data before rendering the picture.
- At exactly 6500 K and tint 0 the step is skipped, so the default costs nothing and the image is identical to one without the feature.

Unreal applies its post-process white balance at the same point (CAT02 rather than Bradford, which differ little in this range); Blender's color management offers the same two controls.

## Limitations

- Values far from 6500 K (below about 2500 K) push saturated colors out of the sRGB gamut; they are clamped to zero before tonemapping.
- In Compatibility, the background of a `BG_CANVAS` environment is not white balanced (it is copied with the exposure only), unlike in Forward+ and Mobile.
- Auto exposure measures the scene before white balance, as before.

## See also

- [Engine Additions](Engine-Additions)
````

Nothing else in the shared docs names the ranges: README.md's bullet, webgpu_site/FAQ.md and the wiki's
Engine-Additions.md index mention white balance without numbers, so they stay as they are.
