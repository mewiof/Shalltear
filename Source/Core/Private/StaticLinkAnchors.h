#pragma once

// types a load path builds by hash (a linker would drop them)

#define SLT_ANCHOR_TYPE(TypeName) (void)::Shalltear::TypeName::GetReflectType();

#define SLT_SCENE_SYSTEM_ANCHORS(X) \
	X(SceneObject)                  \
	X(SceneAsset)                   \
	X(PrefabAsset)

#define SLT_ASSET_DATABASE_ANCHORS(X) \
	X(TextAsset)                      \
	X(TextureAsset)                   \
	X(MeshAsset)                      \
	X(MaterialAsset)                  \
	X(ShaderAsset)                    \
	X(SkeletonAsset)                  \
	X(SkinAsset)                      \
	X(GraphicsProfile)                \
	X(FontAsset)                      \
	X(StyleSheetAsset)                \
	X(UIDocumentAsset)                \
	X(VectorAsset)                    \
	X(UIPanelSettings)                \
	X(ObjectPass)                     \
	X(FullScreenPass)

#define SLT_RENDER_SYSTEM_ANCHORS(X) \
	X(Camera)                        \
	X(Light)                         \
	X(MeshRenderer)                  \
	X(Environment)                   \
	X(ReflectionProbe)               \
	X(Skeleton)                      \
	X(SkinnedMeshRenderer)

#define SLT_UI_SYSTEM_ANCHORS(X) \
	X(UIPanelRenderer)

#define SLT_SETTINGS_ANCHORS(X) \
	X(GraphicsSettings)         \
	X(PlayerSettings)           \
	X(ScreenSettings)
