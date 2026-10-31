# `SimpleSkin` - the skinned fixture whose every number is stated in prose

`SimpleSkin.gltf` is copied verbatim from the Khronos glTF Sample Assets repository, `Models/SimpleSkin/glTF-Embedded/SimpleSkin.gltf`.

**Licence: CC0 1.0 Universal** (SPDX `CC0-1.0`), © 2017 Public, "Marco Hutter (https://github.com/javagl/) for Everything" - public domain, so vendoring it carries no obligation, which is the same reason `BoomBoxWithAxes` was chosen.

**Why the `glTF-Embedded` variant and not `glTF/`.** The loose form is five files - the document plus four `.bin` buffers - and `ParseGLTF` reads _every_ declared buffer up front, so all four would have to be committed even though one of them holds only the animation. The embedded variant is **one 3,566-byte file** with the same accessors behind `data:` URIs, so it also exercises the base64 path on the way past.

**Why this file.** It is the model the glTF tutorial's skinning chapter is written around, so its numbers are published prose rather than something read out of a binary:

- **two joints**, nodes 1 and 2, node 2 being node 1's child at `(0, 1, 0)`;
- **inverse bind matrices of exactly two values** - the identity for joint 0, and a translation of `(0, -1, 0)` for joint 1;
- **ten vertices** in a 1 × 2 rectangle, whose `JOINTS_0` is `USHORT` at a **byte stride of 16** (an 8-byte element in a 16-byte slot), which is the strided-accessor case a reader gets wrong by assuming a natural stride;
- **weights that are not all `(1,0,0,0)`** - the middle rows blend `0.75/0.25` and `0.5/0.5` between the two joints.

**What it deliberately cannot settle.** Both of its joints' bind transforms are pure `y` translations, and a mirror through `x` leaves those unchanged - so `SimpleSkin` passes whether or not the inverse-bind matrices are converted. `Fox.glb` beside it is what makes that half falsifiable; see its README.
