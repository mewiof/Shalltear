#pragma once

#if defined(SLT_ANIMATION_PUBLIC_API_ONLY)
#error "AnimatorController is a CLIENT of this package's public API (no private headers). Fix the public path if it's awkward."
#endif

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	namespace AnimationImpl {

		Handle<SceneObject> ResolvePath(Handle<SceneObject> root, std::string_view path);

		Handle<Component> FindComponentOfType(SceneObject* object, const TypeInfo* type);

		u32 ScalarCount(const TypeInfo* type);

		bool WriteAnimatedField(void* instance, const FieldInfo* field, const Vec4& value, u8 component);

		bool IsCallableFromClip(const TypeInfo* type, StringHash name);

		bool CallAnimatedMethod(void* instance, const TypeInfo* type, StringHash name);

	} // namespace AnimationImpl

} // namespace Shalltear
