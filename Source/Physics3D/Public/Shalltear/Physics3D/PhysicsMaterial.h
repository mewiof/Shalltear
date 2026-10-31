#pragma once

#include "Shalltear/Core/Asset/DataAsset.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Physics3D/Physics3DAPI.h"

namespace Shalltear {

	enum class PhysicsMaterialCombine : u8 {
		Average = 0,

		Minimum = 1,

		Multiply = 2,

		Maximum = 3,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::PhysicsMaterialCombine)
SLT_ENUM_VALUE(Average)
SLT_ENUM_VALUE(Minimum)
SLT_ENUM_VALUE(Multiply)
SLT_ENUM_VALUE(Maximum)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_PHYSICS3D_API PhysicsMaterial : public DataAsset {
	public:
		SLT_REFLECT_CLASS(PhysicsMaterial)
		SLT_REFLECT_PARENT(DataAsset)
		SLT_CREATE_ASSET_MENU("Physics Material", ".physmat")
		SLT_REFLECT_FIELD(friction, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(bounciness, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(rollingResistance, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(frictionCombine)
		SLT_REFLECT_FIELD(bounceCombine)
		SLT_END_REFLECT()

		PhysicsMaterial() = default;
		~PhysicsMaterial() override;

		f32 friction = 0.6f;

		f32 bounciness = 0.0f;

		// spheres/capsules only
		f32 rollingResistance = 0.0f;

		PhysicsMaterialCombine frictionCombine = PhysicsMaterialCombine::Average;
		PhysicsMaterialCombine bounceCombine = PhysicsMaterialCombine::Average;
	};

	inline constexpr u32 PHYSICS_COMBINE_FRICTION_SHIFT = 0;
	inline constexpr u32 PHYSICS_COMBINE_BOUNCE_SHIFT = 2;

	inline u64 PackPhysicsMaterialCombine(PhysicsMaterialCombine friction, PhysicsMaterialCombine bounce) {
		return (static_cast<u64>(friction) << PHYSICS_COMBINE_FRICTION_SHIFT) |
			   (static_cast<u64>(bounce) << PHYSICS_COMBINE_BOUNCE_SHIFT);
	}

	inline f32 CombinePhysicsMaterialValue(f32 a, u64 bitsA, f32 b, u64 bitsB, u32 shift) {
		const u64 modeA = (bitsA >> shift) & 0x3ull;
		const u64 modeB = (bitsB >> shift) & 0x3ull;
		switch (static_cast<PhysicsMaterialCombine>(modeA > modeB ? modeA : modeB)) {
		case PhysicsMaterialCombine::Minimum:
			return a < b ? a : b;
		case PhysicsMaterialCombine::Multiply:
			return a * b;
		case PhysicsMaterialCombine::Maximum:
			return a > b ? a : b;
		case PhysicsMaterialCombine::Average:
		default:
			return (a + b) * 0.5f;
		}
	}

} // namespace Shalltear
