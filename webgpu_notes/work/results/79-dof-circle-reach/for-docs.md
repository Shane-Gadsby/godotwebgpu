# For the docs (work task 79)

Wiki, Engine Additions, **Bokeh and Glare** page, its depth of field section. Replace task 70's cost table and the
"Circle (and custom aperture shapes) costs the square of the reach" bullet with:

## Maximum blur (project setting)

`Rendering > Camera > Depth of Field > Depth of Field Physical Max Blur Size` is how far a physically sized blur may
reach with **Box and Hexagon**, as a percentage of the image height (**6% by default**: enough for a long lens wide
open, 200 mm at f/2.8 focused at 5 m blurs the background by 5.6% of the frame). A larger blur is clamped to it.

**Circle and custom apertures reach at most 3%** of the image height, whatever the setting (a smaller setting still
lowers it). Their cost grows with the square of the reach, so 6% would cost them four times as much. A larger blur
clamps visibly at 3%: for big background blur with a long lens, use Box or Hexagon.

What it costs (WebGPU, 2560x1440, desktop GPU, the default setting):

| Shape and quality | Reach | Cost |
|---|---|---|
| Hexagon Low | 6% | 0.38 ms |
| Hexagon High | 6% | 1.54 ms (task 70; path unchanged) |
| Circle Medium | 3% | 7.8 ms |
| Custom aperture (6 blades), Medium | 3% | 8.4 ms |
| upstream Circle Medium (32 px) | | 4.0 ms |

- Box and Hexagon cost about the same at any value but spread their samples wider: at 6% each quality draws a small
  blur about as the next lower quality does at 3%, and Very Low draws a blur under about 1% of the image height almost
  sharp. Use Low or higher.
- With **Legacy Gather** every shape reaches upstream's fixed 32 pixels, as before.

Example: the same camera with a 200 mm lens at f/2.8 at 1440p blurs the background by 80 pixels; Hexagon draws all
80, Circle draws 43 (3% of 1440).

Renderers: Forward+ and Mobile (Compatibility has no depth of field), web included.
