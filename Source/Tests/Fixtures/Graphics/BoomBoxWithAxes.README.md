# `BoomBoxWithAxes` - the asymmetric fixture KNOWN_CONCERNS #26 said did not exist

`BoomBoxWithAxes.gltf` and `BoomBoxWithAxes.bin` are copied verbatim from the Khronos glTF Sample Assets repository, `Models/BoomBoxWithAxes/glTF/`.

**Licence: CC0 1.0 Universal** (SPDX `CC0-1.0`), © 2018 Public, "Microsoft for Everything" - public domain, so vendoring it carries no obligation. Khronos's own `LICENSE.md` for the model states it directly.

**Why this file and not a smaller one.** #26 stood open for 17 days because _"the two in-repo fixtures cannot settle it: `cube.glb` and `cube_external.gltf` are symmetric, so every candidate answer renders identically"_. This asset is the opposite: a radio with an axis tripod above it whose `+X`, `+Y` and `+Z` labels are **extruded 3D geometry**, so the `Z` glyph and the `+` placement are chiral and a mirror is visible in the geometry itself rather than only in a texture. Its `X_axis`, `Y_axis` and `Z_axis` nodes are separately named, which is what lets `ModelHandednessTest` pin the _direction_ of the conversion rather than only its presence.

**The five PNG textures are deliberately NOT copied** - they are 9.0 MB of the model's 9.3 MB and no test reads them. `ParseGLTF` records an image's `uri` as an `externalPath` without opening it, so the parse is unaffected; only a full `AssetDatabase` import would notice, and no test here does one.

**The ground truth these tests assert against** is Khronos's own published render, `Models/BoomBoxWithAxes/screenshot/screenshot_large.jpg` in that repository: viewed from the asset's front, `+X` reads left-to-right with its arrow pointing right, `+Y` points up, and the boombox's antenna is on the right. FBXImport.md §9.3 records the readback that compared our output against it.
