#pragma once

#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	using EditorExtensionRegisterFn = void (*)(const void* owner);

	namespace EditorExtensions {

		inline constexpr const char* REGISTER_SYMBOL = "ShalltearRegisterEditorExtensions";

		SHALLTEAR_EDITOR_API bool Load(const void* moduleBase);

		SHALLTEAR_EDITOR_API u32 Unload(const void* moduleBase);

	} // namespace EditorExtensions

} // namespace Shalltear

#define SLT_IMPLEMENT_EDITOR_EXTENSIONS(RegisterFunction)                                 \
	extern "C" SLT_API_EXPORT void ShalltearRegisterEditorExtensions(const void* owner) { \
		::Shalltear::EditorExtensionRegisterFn registerFn = (RegisterFunction);           \
		registerFn(owner);                                                                \
	}
