# Icon bake inputs

SVG sources for editor's chrome icons. One per entry in `EditorIcons.h`'s icon table (under `Icons/`) named like the entry (`Icons/PanelScene.svg` is `Glyph::PanelScene`).

## What uses them

Only `Scripts/bake_icon_glyphs.py`. Reads icon table from `EditorIcons.h` -> draws each entry from its SVG -> writes the geometry to `Source/Editor/Private/EditorIconGlyphs.generated.cpp` (committed).

Re-run it after adding/changing an entry or a source:

```sh
python -m pip install fonttools
python Scripts/bake_icon_glyphs.py
```

`--check` fails without rewriting.

## Where a source may come from

The bake will refuse anything else:

| Kind | How it gets here | License |
| --- | --- | --- |
| **Font Awesome Free** icon | Copied unmodified from `svgs/<style>/<icon>.svg` in the Free download. Style = the entry's face (`solid/` for `Face::Solid`, `regular/` for `Face::Regular`) | CC BY 4.0, attributed in `THIRD-PARTY-NOTICES.txt` + the generated file |
| **Original artwork** | Drawn for Shalltear, carrying `Original artwork drawn for Shalltear` | The repository's `LICENSE.txt` |

## How a source is drawn

Bake reads small subset. An `<svg>` with only a `viewBox`, holding `<path d>` elements with no attribute but `fill="currentColor"`. No transforms/groups/strokes/fill rules.

The viewBox is the em: `0 0 <advance> 512`, y = 0 at the em's top, y = 448 the baseline (Font Awesome's grid -> a Free icon don't need a conversion). Ink may stray outside the em by the bake's `PAD_X`/`PAD_Y` (no further).

Originals follow the Free set's Regular weight. 48-unit strokes with round caps/joins, rectangles at a 64-unit outer + 16-unit inner corner radius, a 32-unit gap where a badge cuts what it overlaps. Expand strokes to filled outlines before saving (the subset has no `stroke`).
