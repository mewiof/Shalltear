#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>
#include <span>
#include <string_view>

namespace Shalltear {

	class Component;
	class Object;
	namespace UI {
		class Context;
	} // namespace UI

	namespace SceneViewOverlays {

		using RefreshFn = std::function<void(UI::Context& context)>;
		using BuildFn = std::function<RefreshFn(UI::Context& context, UI::ElementHandle body, std::span<const Handle<Component>> components)>;

		SHALLTEAR_EDITOR_API bool Register(std::string_view title, StringHash component, BuildFn build, const void* owner);
		SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_EDITOR_API u64 KeyFor(std::span<const Handle<Object>> selection);
		SHALLTEAR_EDITOR_API u32 Build(UI::Context& context, UI::ElementHandle parent, std::span<const Handle<Object>> selection, const void* view);
		SHALLTEAR_EDITOR_API void Refresh(UI::Context& context, const void* view);
		SHALLTEAR_EDITOR_API void Release(const void* view);

		SHALLTEAR_EDITOR_API void Terminate();

	} // namespace SceneViewOverlays

} // namespace Shalltear
