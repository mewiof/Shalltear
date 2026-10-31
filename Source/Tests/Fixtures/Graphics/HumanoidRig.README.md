# `HumanoidRig` - the rig that makes auto-mapping falsifiable end to end

`HumanoidRig.gltf` is **generated, not vendored**, and it exists for one reason: [Animation.md](../../../../Docs/Design/Animation.md) M6 auto-maps a humanoid rig during import, and nothing else in this repository's fixtures - or in the whole Khronos corpus - has bone names an English-common-name matcher can recognize.

| model | joints | joint names |
| --- | --- | --- |
| `Fox.glb` | 24 | `b_Root_00`, `b_Hip_01`, `b_Spine01_02`, `b_Tail01_012` … |
| `CesiumMan` | 19 | `Skin_arm_joint_L_2`, `Skin_torso_joint_1` … |
| `RiggedFigure` | 19 | `Bone`, `Bone.001`, `Bone.002` … |
| `SimpleSkin` | 2 | unnamed |
| **`HumanoidRig.gltf`** | **22** | `mixamorig:Hips`, `mixamorig:LeftForeArm` … |

`Fox` is the useful _negative_ - a quadruped whose names must **not** produce a humanoid profile - and `RiggedFigure`'s `Bone.001` is what a rig with no recognizable names looks like. Neither can show that the mapper works.

## What it contains

Twenty-two bones in Mixamo's spelling under an `Armature` root, a one-triangle skinned mesh weighted entirely to the hips, and one two-key rotation take called `Idle` so the model also states an animation. The rig is authored **at** its bind pose, so every inverse-bind matrix is a pure translation by the negated world position and `G·B = I` holds - the same property [`Fox.README.md`](Fox.README.md) measures, arrived at by construction here rather than by luck.

Proportions are a plausible adult human in metres: hips at 1.0 m, a 0.45 m thigh, a 0.28 m forearm. That matters because §3.9 step 3 scales a retargeted root's translation by the ratio of two rigs' reference scales, and a fixture whose bones were all at the origin would make that ratio 1 and the rule untestable.

## Licence

**None to discharge.** No part of this file is copied from anywhere: the geometry is one triangle, the skeleton is a table of names and offsets typed into a generator, and the bone names are Mixamo's _naming convention_ rather than any Mixamo asset. `mixamorig:` appears because that prefix is what the matcher has to survive - a rig in the wild spells every bone `mixamorig:LeftForeArm`, and a fixture that omitted the namespace would test a matcher that does not exist.

## Regenerating it

The generator lives outside the repository rather than in `Scripts/`, deliberately: this file is a fixture and not a build input, and a script in `Scripts/` implies something re-runs it. If the fixture needs to change, the table of bones is at the top of the generated document - the `nodes` array - and editing the JSON directly is a supported way to do it, since the buffer holds only mesh data, inverse binds and the take, none of which depend on the bone _names_.
