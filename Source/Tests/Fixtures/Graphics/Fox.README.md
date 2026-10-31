# `Fox` - the rigged fixture that makes the glTF bind-pose conversion falsifiable

`Fox.glb` is copied verbatim from the Khronos glTF Sample Assets repository, `Models/Fox/glTF-Binary/Fox.glb` (162,852 bytes, texture included in the `BIN` chunk).

## Licence, and the obligation it carries

Khronos records **three** copyrights on this asset and they are not all the same licence:

| Part | Holder | Licence |
| --- | --- | --- |
| Model | PixelMannen, © 2014 | **CC0 1.0 Universal** |
| **Rigging & animation** | tomkranis, © 2014 | **CC BY 4.0 International** |
| Conversion to glTF | @AsoboStudio and @scurest, © 2017 | **CC BY 4.0 International** |

The half this fixture exists for - the rig - is the **CC BY** half, so unlike `BoomBoxWithAxes` and `SimpleSkin` this file _does_ carry an obligation: attribution, a link to the licence, and an indication of changes. **This file discharges it**: the table above is the attribution, the licence is [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/legalcode), and the asset is committed **unmodified**. The repository already carries CC BY 4.0 obligations of the same shape for Font Awesome's icons ([Docs/ICON_LICENSING.md](../../../../Docs/ICON_LICENSING.md) §4), so the mechanism is inherited rather than new.

## Why this file and not a CC0 one

`ModelSkinTest` has to be able to **fail** when the glTF parser stops converting an inverse-bind matrix into the engine's frame ([FBXImport.md](../../../../Docs/Design/FBXImport.md) §9.3 names that as M3's obligation). The assertion that catches it is that a joint's world transform composed with its inverse bind pose is the **identity** - which is only a falsifiable statement about a rig that is (a) authored _at_ its bind pose and (b) has joints the `x` mirror actually moves.

Measured over every skinned model in the Khronos corpus, `Fox` is the only one that is both:

| Model | at bind? `max abs(G·B − I)` | mirror-sensitive? `max abs(B − S·B·S)` | Licence |
| --- | --- | --- | --- |
| `SimpleSkin` | **0.000000** | 0.0000 - invariant | CC0 |
| `RiggedSimple` | 1.000000 | 2.0 | CC BY |
| `RiggedFigure` | 1.000001 | 2.0 | CC BY |
| `CesiumMan` | 1.000000 | 2.4 | CC BY + trademark limits |
| **`Fox`** | **0.000008** | **105.4** | CC0 model + **CC BY rig** |
| `RecursiveSkeletons` | 25.000000 | 0.0 | CC BY |
| `BrainStem` | - | - | **Poser EULA** - not vendorable |

So `Fox` reads `0.000008` when the conversion is right and **`104.5`** when the inverse-bind matrices are left in the file's frame. Nothing else in the corpus gives a signal of that size, and the three `1.0` rows are rigs posed away from bind, where the invariant is not the identity to begin with.

**Its 24 joints and its three animation takes** (`Survey`, `Walk`, `Run`) make it the natural fixture for `FBXImport.md` M4 and `Animation.md` M3 as well, which is the other reason to pay 162 KB once rather than 15 KB twice.
