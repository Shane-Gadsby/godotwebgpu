# Task 55: text for the docs

## Wiki, Engine Additions, "Bokeh and Glare" page

Replace or extend the halation / glare-size paragraphs with:

### Glare sizes are exact at every resolution

Halation, the diffusion filter and the lens flare ghosts each blur the frame by a size given relative to the frame
height, so they look the same at 720p and at 4K. The renderer measures how much each level of its glare chain (a mip
chain of the frame) blurs, and picks and blends levels to match the size you set. Since this release the sizes are
within a few percent of their model at every resolution, in Forward+, Mobile and on the web. Before, they were right
at 720p only: at 1080p and above, halation (radius 1 and up), the diffusion filter and the flare ghosts came out about
10% small.

**This changes the look of existing scenes that use glare**: halos, diffusion and flare ghosts get up to about 10%
larger (at 1080p and up), and on Mobile, halos and diffusion at large sizes now sit on their light instead of a few
pixels below and right of it. Scenes with no glare effect enabled render exactly as before.

**The smallest halo** a renderer can draw is its glare chain's sharpest level. A smaller halo (a low
`halation_radius`, or a large sensor format) is drawn at that size:

| Renderer | Smallest halo (Gaussian sigma) |
|---|---|
| Forward+ | about 4 pixels |
| Forward+ with `halation_high_quality` | about 1.9 pixels |
| Mobile | about 2 pixels |

The physical halo (`halation_radius` 1.0 on a 24 mm frame) is 0.47% of the frame height: 3.4 pixels at 720p, 5 at
1080p, 10 at 2160p. So `halation_high_quality` matters for small halos at low resolutions (720p and below, or radius
below 1); from 1080p up, radius 1 is already above the standard floor.

Example: a film look at 1080p, `CameraAttributesPhysical` with `halation_enabled = true`, `halation_radius = 1.0`:
the halo's sigma is 5.0 pixels, as modeled. At 720p the same camera draws it at about 4 pixels unless
`halation_high_quality` is on, which draws the modeled 3.4.

## Class reference

Changed in this branch (doc/classes/CameraAttributes.xml): `halation_high_quality` now states the floors in pixels
(about 4 standard, 1.9 high quality, 2 on Mobile) instead of "about 0.5% of the frame height" (true at 720p only), and
`halation_radius` says a halo below the floor is drawn at the floor's size.
