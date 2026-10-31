#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Animation/AnimationClip.h"

namespace Shalltear {

	class JSONValue;

	namespace AnimationEditor {

		enum class ModelAnimationType : u8 {
			Generic = 0,
			None = 1,
		};

		// ranges in seconds (not frames)
		struct ModelClipRange {
			str name;
			f32 start = 0.0f;
			f32 end = 0.0f;
			AnimationLoopMode loop = AnimationLoopMode::None;

			bool mirror = false;
		};

		enum class ModelRigPose : u8 {
			FromModel = 0,
			BindPose = 1,
			CopyFrom = 2,
		};

		struct ModelAnimationSettings {
			ModelAnimationType type = ModelAnimationType::Generic;

			ModelRigPose rigPose = ModelRigPose::FromModel;

			Guid stanceSource;

			Vector<ModelClipRange> clips{GetMemoryResource(MemTag::Editor)};
		};

		ModelAnimationSettings ReadModelAnimationSettings(const JSONValue& importerSettings);

		ModelAnimationSettings LoadModelAnimationSettings(const Path& modelVirtualPath);

		bool ApplyModelAnimationSettings(const Path& modelVirtualPath, const ModelAnimationSettings& settings);

	} // namespace AnimationEditor

} // namespace Shalltear
