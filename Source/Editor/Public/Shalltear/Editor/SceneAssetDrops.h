#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	class Scene;
	class SceneObject;
	namespace UI {
		struct DragPayload;
	} // namespace UI

	namespace SceneAssetDrops {

		SHALLTEAR_EDITOR_API bool Register(StringHash assetType, StringHash component, StringHash assetField, const void* owner);
		SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_EDITOR_API bool Accepts(const UI::DragPayload& payload, Handle<Scene> scene);
		SHALLTEAR_EDITOR_API Handle<SceneObject> Drop(const UI::DragPayload& payload, Handle<Scene> scene, Handle<SceneObject> parent,
													  const Vec3* worldPosition = nullptr);

		SHALLTEAR_EDITOR_API void Terminate();

	} // namespace SceneAssetDrops

} // namespace Shalltear
