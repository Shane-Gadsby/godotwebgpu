# For the docs (tasks 64 and 65): wiki text

Both tasks' user-facing text is here; task 64's folder points to it.

## Engine Additions → Camera White Balance: add a section "Lights that match the white balance"

### Lights that match the white balance

The white balance and light temperatures use one model of the color of a blackbody: the Planckian locus as
approximated by Krystek (1985), the same one Filament uses. A light at a temperature, under a white balance of that
temperature, renders exactly like a 6500 K light under the default white balance. So a tungsten-lit set lit with
3200 K lights looks right with the camera at 3200 K, and the daylight through the window turns blue, as it does on a
real camera.

There are two ways to give a light a temperature:

- **`Light3D.light_temperature_enabled`** (new, off by default). Turn it on and `light_temperature` tints the light,
  with or without physical light units. The tint keeps the light's luminance constant: changing the temperature
  changes the color, never the brightness, so your exposure stays right when you swap a 5600 K light for a 3200 K
  one. Leave `light_color` white for the pure blackbody color; any other `light_color` is multiplied in on top.
- **`Color.from_temperature(kelvin)`** (new) returns the same color, linear, for anything that is not a light:
  emissive materials, a swatch in your UI, a "color of this light" readout. Convert it with `.linear_to_srgb()`
  before assigning it to a color property.

```gdscript
# A tungsten key light and a daylight fill, camera balanced for tungsten.
$KeyLight.light_temperature_enabled = true
$KeyLight.light_temperature = 3200.0
$FillLight.light_temperature_enabled = true
$FillLight.light_temperature = 5600.0
$Camera3D.attributes.white_balance_temperature = 3200.0

# The same color on a lamp's emissive bulb (BaseMaterial3D.emission is sRGB-encoded).
$Bulb.material_override.emission = Color.from_temperature(3200.0).linear_to_srgb()
```

`Color.from_temperature(kelvin, normalization)` takes `Color.TEMPERATURE_NORMALIZATION_LUMINANCE` (the default:
luminance 1.0, channels may exceed 1.0) or `Color.TEMPERATURE_NORMALIZATION_MAX_CHANNEL` (brightest channel 1.0).
The temperature is clamped to 1000-15000 K, the range of the approximation.

Note: 6500 K is not exactly white. The D65 white point is slightly off the blackbody curve, so a 6500 K light is a
touch magenta (`Color.from_temperature(6500.0)` is about `Color(1.044, 0.983, 1.036)`). Under a white balance of T, a
light of T looks like that 6500 K light, not like a pure white one.

Works in Forward+, Mobile, Compatibility and on the web: the tint is applied to the light's color on the CPU, so
every renderer sees the same light.

## A note for the lights documentation (wherever lights are described on the wiki)

`light_temperature` used to do nothing unless the project setting
`rendering/lights_and_shadows/use_physical_light_units` was on, and the inspector hid it. Each light now has
**Temperature Enabled** (`light_temperature_enabled`) in its Light section:

- **Off** (default): exactly as before. Without physical light units the temperature is ignored; with them it tints
  the light, normalized so the brightest channel is 1, which makes temperatures far from 6500 K darker.
- **On**: the temperature tints the light with or without physical light units, at constant luminance. With physical
  light units this also switches the light to the constant-luminance tint, so its lumens or lux no longer depend on
  its color.

`get_correlated_color()`, the light's editor gizmo and LightmapGI bakes follow the setting.
