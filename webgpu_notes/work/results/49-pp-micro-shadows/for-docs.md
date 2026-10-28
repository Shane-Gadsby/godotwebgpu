# Wiki text: Micro Shadows (Engine Additions)

Suggested page: `Engine-Additions-Micro-Shadows.md`, linked from Engine Additions (rendering).

---

Micro shadows on `Environment`: directional lights are shadowed by the fine detail baked into each material's ambient occlusion and normal maps, the way HDRP's Micro Shadows override does.

## Overview

A shadow map cannot see the bumps, cracks and pores that exist only in a texture, so a brick wall or a rocky ground lit by a low sun looks flatter than it should: the mortar lines and crevices that are dark in the AO map still receive full sunlight. Micro shadows read each texel's ambient occlusion as how open that point is to the sky, and its (normal-mapped) normal as the direction of that opening. Light that arrives from outside the opening is shadowed. The darker the AO and the lower the light, the more of it is shadowed.

Micro shadows work in Forward+, Mobile and Compatibility, natively and on the web (WebGPU). They are off by default, and a scene without them renders exactly as before.

## Properties

In the **Micro Shadows** group of the `Environment` inspector:

| Property | Type | Default | Range | Effect |
|---|---|---|---|---|
| `micro_shadows_enabled` | bool | `false` | | Turns micro shadows on. |
| `micro_shadows_opacity` | float | `1.0` | 0 to 1 | Strength: 0 is no effect, 1 the full effect. |

From code: `RenderingServer.environment_set_micro_shadows(environment, enable, opacity)`.

## Example

```gdscript
var env := $WorldEnvironment.environment
env.micro_shadows_enabled = true
env.micro_shadows_opacity = 0.8

# The materials need an AO map (and usually a normal map) for anything to happen:
var mat := StandardMaterial3D.new()
mat.ao_enabled = true
mat.ao_texture = preload("res://rock_ao.png")
mat.normal_enabled = true
mat.normal_texture = preload("res://rock_normal.png")
```

Try it with a `DirectionalLight3D` low over the horizon: crevices in the AO map stay dark on the lit side.

## How it works

For each directional light and pixel, the light is multiplied by

`mix(1, clamp(N·L + 2·AO² − 1, 0, 1), opacity)`

where `N` is the normal-mapped normal, `L` the direction to the light and `AO` the material's ambient occlusion. It is the formula of "The Technical Art of Uncharted 4" (Brinck and Maximov, SIGGRAPH 2016) that HDRP uses: the AO is read as a cone of aperture `2·AO²` around the normal. Points facing away from the light are left alone (they get no direct light anyway).

- With `AO = 1` (no AO map, or a fully open texel) nothing is shadowed, at any light angle. A material without an AO map is unchanged.
- The AO is the material's own (its `ao_texture`, a shader's `AO`, decals), not SSAO, which already darkens indirect light separately. `ao_light_affect` does not change it.
- It multiplies the light together with its shadow map, so it affects diffuse and specular alike.
- It costs a few instructions per pixel per directional light, in the lighting loop of the scene shaders: no extra pass, no extra texture and no extra shader variants.

## Limitations

- Directional lights only (as in HDRP); omni and spot lights are not affected.
- Not with per-vertex lighting (`shading_mode` per vertex, or the project's vertex-shading setting): there is no per-pixel normal to test.
- A material with a shadow-to-opacity flag also turns micro shadows into opacity, as it does with any shadow.

## See also

- [Engine Additions](Engine-Additions)
- HDRP's [Micro Shadows](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Override-Micro-Shadows.html)
