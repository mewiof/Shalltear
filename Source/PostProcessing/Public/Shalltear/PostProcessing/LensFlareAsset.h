#pragma once

#include "Shalltear/Core/Asset/DataAsset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Gradient.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/PostProcessing/PostProcessingAPI.h"

namespace Shalltear {

	enum class LensFlareShape : u8 {
		Image = 0,
		Circle = 1,
		Polygon = 2,
		Ring = 3
	};

	enum class LensFlareColorMode : u8 {
		Constant = 0,
		Radial = 1,
		Angular = 2
	};

	enum class LensFlareBlend : u8 {
		Additive = 0,
		Premultiplied = 1,
		Lerp = 2
	};

	enum class LensFlareDistribution : u8 {
		Uniform = 0,
		Curve = 1,
		Random = 2
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::LensFlareShape)
SLT_ENUM_VALUE(Image)
SLT_ENUM_VALUE(Circle)
SLT_ENUM_VALUE(Polygon)
SLT_ENUM_VALUE(Ring)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::LensFlareColorMode)
SLT_ENUM_VALUE(Constant)
SLT_ENUM_VALUE(Radial)
SLT_ENUM_VALUE(Angular)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::LensFlareBlend)
SLT_ENUM_VALUE(Additive)
SLT_ENUM_VALUE(Premultiplied)
SLT_ENUM_VALUE(Lerp)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::LensFlareDistribution)
SLT_ENUM_VALUE(Uniform)
SLT_ENUM_VALUE(Curve)
SLT_ENUM_VALUE(Random)
SLT_END_ENUM()

namespace Shalltear {

	struct LensFlareElement {
		bool enabled = true;
		LensFlareShape shape = LensFlareShape::Circle;

		Handle<TextureAsset> texture;
		bool preserveAspectRatio = false;

		f32 gradient = 0.1f;
		f32 falloff = 1.0f;
		bool inverse = false;
		i32 sideCount = 6;
		f32 roundness = 0.0f;
		f32 ringThickness = 0.5f;

		LensFlareColorMode colorMode = LensFlareColorMode::Constant;
		Color tint = Color::White();
		Gradient tintGradient{GetMemoryResource(MemTag::Asset)};
		bool modulateByLightColor = true;
		f32 intensity = 1.0f;
		LensFlareBlend blend = LensFlareBlend::Additive;

		// 0 at light, 1 at screen center, 2 mirrored
		f32 position = 0.0f;
		Vec2 positionOffset{};
		f32 angularOffset = 0.0f;
		Vec2 translationScale{1.0f, 1.0f};
		f32 rotation = 0.0f;
		bool autoRotate = false;
		// 1 spans 1/10 of view height
		Vec2 size{1.0f, 1.0f};
		f32 scale = 1.0f;

		bool multiple = false;
		i32 count = 5;
		LensFlareDistribution distribution = LensFlareDistribution::Uniform;
		f32 lengthSpread = 1.0f;
		Gradient colors{GetMemoryResource(MemTag::Asset)};
		f32 rotationStep = 0.0f;
		AnimationCurve positionCurve{{Keyframe{.time = 0.0f, .value = 0.0f, .inTangent = 1.0f, .outTangent = 1.0f},
									  Keyframe{.time = 1.0f, .value = 1.0f, .inTangent = 1.0f, .outTangent = 1.0f}},
									 GetMemoryResource(MemTag::Asset)};
		AnimationCurve scaleCurve{{Keyframe{.time = 0.0f, .value = 1.0f}, Keyframe{.time = 1.0f, .value = 1.0f}},
								  GetMemoryResource(MemTag::Asset)};
		// value 1 = 180 deg
		AnimationCurve rotationCurve{{Keyframe{.time = 0.0f, .value = 0.0f}, Keyframe{.time = 1.0f, .value = 0.0f}},
									 GetMemoryResource(MemTag::Asset)};
		i32 seed = 0;
		f32 intensityVariation = 0.75f;
		Vec2 positionVariation{1.0f, 0.0f};
		f32 rotationVariation = 180.0f;
		f32 scaleVariation = 1.0f;

		SLT_REFLECT_VALUE(LensFlareElement)
		SLT_REFLECT_FIELD(enabled)
		SLT_REFLECT_FIELD(shape)
		SLT_REFLECT_FIELD(texture)
		SLT_REFLECT_FIELD(preserveAspectRatio)
		SLT_REFLECT_FIELD(gradient, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(falloff, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(inverse)
		SLT_REFLECT_FIELD(sideCount, SLT_RANGE(3, 32))
		SLT_REFLECT_FIELD(roundness, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(ringThickness, SLT_RANGE(0.01f, 1.0f))
		SLT_REFLECT_FIELD(colorMode, SLT_HEADER("Color"))
		SLT_REFLECT_FIELD(tint)
		SLT_REFLECT_SUBOBJECT(tintGradient)
		SLT_REFLECT_FIELD(modulateByLightColor)
		SLT_REFLECT_FIELD(intensity)
		SLT_REFLECT_FIELD(blend)
		SLT_REFLECT_FIELD(position, SLT_HEADER("Transform"))
		SLT_REFLECT_FIELD(positionOffset)
		SLT_REFLECT_FIELD(angularOffset)
		SLT_REFLECT_FIELD(translationScale)
		SLT_REFLECT_FIELD(rotation)
		SLT_REFLECT_FIELD(autoRotate)
		SLT_REFLECT_FIELD(size)
		SLT_REFLECT_FIELD(scale)
		SLT_REFLECT_FIELD(multiple, SLT_HEADER("Multiple Elements"))
		SLT_REFLECT_FIELD(count, SLT_RANGE(1, 128))
		SLT_REFLECT_FIELD(distribution)
		SLT_REFLECT_FIELD(lengthSpread)
		SLT_REFLECT_SUBOBJECT(colors)
		SLT_REFLECT_FIELD(rotationStep)
		SLT_REFLECT_SUBOBJECT(positionCurve)
		SLT_REFLECT_SUBOBJECT(scaleCurve)
		SLT_REFLECT_SUBOBJECT(rotationCurve)
		SLT_REFLECT_FIELD(seed)
		SLT_REFLECT_FIELD(intensityVariation)
		SLT_REFLECT_FIELD(positionVariation)
		SLT_REFLECT_FIELD(rotationVariation)
		SLT_REFLECT_FIELD(scaleVariation)
		SLT_END_REFLECT()
	};

	class SHALLTEAR_POSTPROCESSING_API LensFlareAsset : public DataAsset {
	public:
		SLT_REFLECT_CLASS(LensFlareAsset)
		SLT_REFLECT_PARENT(DataAsset)
		SLT_CREATE_ASSET_MENU("Rendering/Lens Flare", ".slflare")
		SLT_REFLECT_ARRAY(elements)
		SLT_END_REFLECT()

		LensFlareAsset() = default;
		~LensFlareAsset() override;

		Vector<LensFlareElement> elements{GetMemoryResource(MemTag::Asset)};
	};

} // namespace Shalltear
