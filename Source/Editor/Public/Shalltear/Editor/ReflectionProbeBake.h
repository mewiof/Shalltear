#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear {

	class ReflectionProbe;

	namespace ReflectionProbeBakeOps {

		SHALLTEAR_EDITOR_API Path BakePath(Handle<ReflectionProbe> probe);

		SHALLTEAR_EDITOR_API Vector<u8> EncodeStrip(std::span<const u8> faces, u32 faceSize);

	} // namespace ReflectionProbeBakeOps

	namespace ReflectionProbeBake {

		SHALLTEAR_EDITOR_API void Initialize();
		SHALLTEAR_EDITOR_API void Terminate();

		SHALLTEAR_EDITOR_API str WhyNotBakeable(Handle<ReflectionProbe> probe);

		SHALLTEAR_EDITOR_API bool Request(Handle<ReflectionProbe> probe);

		SHALLTEAR_EDITOR_API void Drain();

		SHALLTEAR_EDITOR_API u32 GetPendingCount();

	} // namespace ReflectionProbeBake

} // namespace Shalltear
