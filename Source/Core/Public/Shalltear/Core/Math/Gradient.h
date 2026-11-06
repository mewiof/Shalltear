#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include <initializer_list>

namespace Shalltear {

	enum class GradientMode : u8 {
		Blend = 0,
		Fixed = 1,
		PerceptualBlend = 2,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::GradientMode)
SLT_ENUM_VALUE(Blend)
SLT_ENUM_VALUE(Fixed)
SLT_ENUM_VALUE(PerceptualBlend)
SLT_END_ENUM()

namespace Shalltear {

	struct SHALLTEAR_CORE_API GradientColorKey {
		SLT_REFLECT_VALUE(GradientColorKey)
		SLT_REFLECT_FIELD(color)
		SLT_REFLECT_FIELD(time)
		SLT_END_REFLECT()

		Color color = Color::White();
		f32 time = 0.0f;

		friend bool operator==(const GradientColorKey&, const GradientColorKey&) = default;
	};

	struct SHALLTEAR_CORE_API GradientAlphaKey {
		SLT_REFLECT_VALUE(GradientAlphaKey)
		SLT_REFLECT_FIELD(alpha)
		SLT_REFLECT_FIELD(time)
		SLT_END_REFLECT()

		f32 alpha = 1.0f;
		f32 time = 0.0f;

		friend bool operator==(const GradientAlphaKey&, const GradientAlphaKey&) = default;
	};

	struct SHALLTEAR_CORE_API Gradient {
		SLT_REFLECT_VALUE(Gradient)
		SLT_REFLECT_ARRAY(colorKeys)
		SLT_REFLECT_ARRAY(alphaKeys)
		SLT_REFLECT_FIELD(mode)
		SLT_END_REFLECT()

		static constexpr u32 MAX_KEYS = 8;

		Gradient() = default;
		explicit Gradient(std::pmr::memory_resource* resource)
			: colorKeys({GradientColorKey{}, GradientColorKey{Color::White(), 1.0f}}, resource),
			  alphaKeys({GradientAlphaKey{}, GradientAlphaKey{1.0f, 1.0f}}, resource) {}
		Gradient(std::initializer_list<GradientColorKey> colors, std::initializer_list<GradientAlphaKey> alphas,
				 std::pmr::memory_resource* resource = GetMemoryResource(MemTag::Core))
			: colorKeys(colors, resource), alphaKeys(alphas, resource) {}

		Gradient(const Gradient& other)
			: colorKeys(other.colorKeys, other.colorKeys.get_allocator().resource()),
			  alphaKeys(other.alphaKeys, other.alphaKeys.get_allocator().resource()), mode(other.mode) {}
		Gradient& operator=(const Gradient&) = default;
		Gradient(Gradient&&) = default;
		Gradient& operator=(Gradient&&) = default;

		// expects keys sorted by time
		[[nodiscard]] Color Evaluate(f32 time) const;

		friend bool operator==(const Gradient& a, const Gradient& b) {
			return a.mode == b.mode && a.colorKeys == b.colorKeys && a.alphaKeys == b.alphaKeys;
		}

		Vector<GradientColorKey> colorKeys{{GradientColorKey{}, GradientColorKey{Color::White(), 1.0f}}, GetMemoryResource(MemTag::Core)};
		Vector<GradientAlphaKey> alphaKeys{{GradientAlphaKey{}, GradientAlphaKey{1.0f, 1.0f}}, GetMemoryResource(MemTag::Core)};
		GradientMode mode = GradientMode::Blend;
	};

} // namespace Shalltear
