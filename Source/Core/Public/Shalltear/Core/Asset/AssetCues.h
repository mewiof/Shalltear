#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class Asset;
	class SceneObject;
	struct TypeInfo;

	struct AssetCue {
		Handle<Asset> asset;
		Handle<SceneObject> object;
		f32 volume = 1.0f;
	};

	using AssetCueFn = bool (*)(const AssetCue& cue);

	namespace AssetCues {

		SHALLTEAR_CORE_API bool Register(const TypeInfo* type, AssetCueFn fn, const void* owner);

		SHALLTEAR_CORE_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_CORE_API bool CanCue(const TypeInfo* type);

		SHALLTEAR_CORE_API bool Cue(const AssetCue& cue);

	} // namespace AssetCues

} // namespace Shalltear
