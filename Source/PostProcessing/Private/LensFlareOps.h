#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/PostProcessing/LensFlareAsset.h"
#include "Shalltear/PostProcessing/PostProcessingAPI.h"

namespace Shalltear::LensFlareOps {

	// NDC half height per unit of element size
	inline constexpr f32 SIZE_UNIT = 0.1f;
	inline constexpr u32 MAX_SAMPLES = 64;
	inline constexpr u32 MAX_COPIES = 128;

	struct ViewInput {
		Mat4 view = Mat4::Identity();
		Mat4 projection = Mat4::Identity();
		Vec3 cameraPosition{};
		Vec2I extent{1, 1};
	};

	struct FlareInput {
		Vec3 position{};
		Vec3 forward{0.0f, 0.0f, 1.0f};
		bool hasLight = false;
		LightType lightType = LightType::Point;
		Color lightColor = Color::White();
		f32 spotAngleDeg = 45.0f;
		f32 innerSpotAngleDeg = 0.0f;

		f32 intensity = 1.0f;
		f32 scale = 1.0f;
		bool attenuationByLightShape = true;
		f32 attenuationDistance = 100.0f;
		const AnimationCurve* attenuationDistanceCurve = nullptr;
		f32 scaleDistance = 100.0f;
		const AnimationCurve* scaleDistanceCurve = nullptr;
		const AnimationCurve* screenAttenuationCurve = nullptr;
		bool occlusion = false;
		f32 occlusionRadius = 0.1f;
		i32 sampleCount = 32;
		f32 occlusionOffset = 0.05f;
		bool allowOffScreen = false;
	};

	struct ProjectedFlare {
		// NDC, y up
		Vec2 screen{};
		f32 depth = 0.0f;
		// +1 for reverse Z
		f32 depthSign = 1.0f;
		Vec2 occlusionRadius{};
		u32 samples = 0;
		bool offScreenVisible = false;
		f32 fade = 1.0f;
		f32 intensity = 1.0f;
		f32 scale = 1.0f;
		bool hasLight = false;
		Color lightColor = Color::White();
	};

	struct Quad {
		Vec2 center{};
		// NDC half height units on both axes, aspect applied after rotation
		Vec2 halfSize{};
		// rads clockwise
		f32 rotation = 0.0f;
		Color color = Color::White();
	};

	SHALLTEAR_POSTPROCESSING_API bool Project(const ViewInput& view, const FlareInput& flare, ProjectedFlare& out);

	// aspect = width / height
	SHALLTEAR_POSTPROCESSING_API void ExpandElement(const LensFlareElement& element, const ProjectedFlare& flare, f32 aspect,
													f32 textureAspect, Vector<Quad>& out);

	// unit disk, must match ShalltearFlareSample in LensFlare.hlsl
	SHALLTEAR_POSTPROCESSING_API Vec2 OcclusionSample(u32 index, u32 count);

	SHALLTEAR_POSTPROCESSING_API f32 HashUnit(i32 seed, u32 index, u32 channel);

	SHALLTEAR_POSTPROCESSING_API f32 FalloffExponent(f32 falloff);

} // namespace Shalltear::LensFlareOps
