#pragma once

#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>

namespace Shalltear {

	struct ProfilerModule {
		str title;
		std::function<void(UI::Context& context, UI::ElementHandle host)> build;
		std::function<void(UI::Context& context, UI::ElementHandle host)> refresh;
		std::function<void()> sample;
	};

	namespace ProfilerModules {

		SHALLTEAR_EDITOR_API bool Register(StringHash id, ProfilerModule module, const void* owner = nullptr);

		SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_EDITOR_API void ForEach(const std::function<void(StringHash id, const ProfilerModule& module)>& visit);

		SHALLTEAR_EDITOR_API const ProfilerModule* Find(StringHash id);

		SHALLTEAR_EDITOR_API u64 GetVersion();

		SHALLTEAR_EDITOR_API void Terminate();

	} // namespace ProfilerModules

} // namespace Shalltear
