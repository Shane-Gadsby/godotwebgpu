# 24-white-balance: text for the shared docs

## Wiki: new page `Engine-Additions-White-Balance.md`

Add to `_Sidebar.md` under **Engine Additions** (after Script Editor Input, or in a new "Rendering" group), and to
`Engine-Additions.md`:

```
## Rendering

- [Camera White Balance](Engine-Additions-White-Balance): a color temperature and green-magenta tint on `CameraAttributes`, as on a real camera, in every rendering method and on the web.
```

Page text:

```markdown
A white balance setting on `CameraAttributesPractical` and `CameraAttributesPhysical`: a color temperature in Kelvin and a green-magenta tint, with the meaning they have on a real camera.

## Overview

The temperature is the light the camera is balanced for. At the default, 6500 K, nothing changes. A lower value tells the camera the light is warmer than daylight, so it compensates by turning the image blue: at 3200 K a scene lit by tungsten light looks neutral, and a daylight scene looks blue. A higher value does the opposite, for overcast or shade, and turns a daylight scene orange.

The tint corrects a green or magenta cast that the temperature alone cannot. A positive tint compensates for a green light (fluorescent tubes, foliage bounce) and turns the image magenta; a negative tint turns it green. This is the direction the tint slider of Lightroom and most raw converters uses.

White balance works in Forward+, Mobile and Compatibility, natively and on the web (WebGPU), with or without physical light units.

## Properties

Both are in the **White Balance** group of the inspector, on the shared `CameraAttributes` base class, so `CameraAttributesPractical` and `CameraAttributesPhysical` both have them.

| Property | Type | Default | Range | Effect |
|---|---|---|---|---|
| `white_balance_temperature` | float (K) | `6500.0` | 1000 to 15000 | Color temperature of the light the camera is balanced for. |
| `white_balance_tint` | float | `0.0` | -1 to 1 | Green-magenta correction, 0.05 Duv per unit. Positive turns the image magenta. |

From code, `RenderingServer.camera_attributes_set_white_balance(camera_attributes, temperature, tint)` sets both on a camera attributes RID.

As with exposure, a `CameraAttributes` on a `Camera3D` overrides the one on the `WorldEnvironment`, which is used by every camera without its own, including the editor viewport.

## Matching lights

The temperature is read on the same curve `Light3D.light_temperature` uses (with physical light units enabled). So a light set to a temperature, seen through a white balance of the same temperature, looks exactly like a default 6500 K light at the default white balance. To make a tungsten-lit interior look neutral, set the lights to 3200 K and the camera's white balance to 3200 K.

## How it works

- The light's color is taken on the Planckian (black body) locus at the given temperature, in CIE 1960 UCS, with the Krystek approximation `Light3D` uses. The tint moves it perpendicular to the locus by `tint * 0.05` Duv, the scale Unreal Engine's `WhiteTint` uses.
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
```

## `doc/classes`

Done on the branch (`CameraAttributes.xml`: the two members and the description; `RenderingServer.xml`:
`camera_attributes_set_white_balance`). `--doctool` leaves them unchanged.

## `webgpu_tests/README.md` (Forward+ matrix section)

The matrix has 83 arms: `white_balance` (category post) added by task 24, 3200 K and tint 0.2 against the default.

## README.md

If the README lists engine additions, one line: "Camera white balance (temperature and tint) on `CameraAttributes`, every renderer, web included."
