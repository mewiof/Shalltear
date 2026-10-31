#include "Shalltear/PostProcessing/AmbientOcclusionPass.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Matrix.h"

#include "AmbientOcclusionShader.h"

namespace Shalltear {

	namespace {

		// must match ShalltearAmbientOcclusionPush in AmbientOcclusion.hlsl
		struct AmbientOcclusionPush {
			Mat4 invViewProjection;
			f32 projHalfAndLinearize[4];
			f32 radiusFalloffIntensityBias[4];
			// xy = step extent px, z = step kind, w = direction count
			f32 extentStepDirections[4];
		};
		static_assert(sizeof(AmbientOcclusionPush) == 112, "the push block is a matrix + 3 float4s (under the 128 B budget)");

		enum class AmbientOcclusionStep : u32 {
			Estimate = 0,
			BlurX = 1,
			BlurY = 2,
			Composite = 3,
			// shader branches on these
			Upsample = 4
		};

		constexpr i32 MIN_DIRECTIONS = 1;
		constexpr i32 MAX_DIRECTIONS = 16;

		constexpr f32 HORIZON_BIAS = 0.1f;

	} // namespace

	PassRequests AmbientOcclusionPass::GetRequests() const {
		const PassRequests base = PassRequests::SceneDepth | PassRequests::SceneNormals;
		return afterOpaque ? (base | PassRequests::SceneColor) : base;
	}

	AmbientOcclusionPass::AmbientOcclusionPass() {
		point = PassPoint::AfterPrepass;
	}
	AmbientOcclusionPass::~AmbientOcclusionPass() = default;

	void AmbientOcclusionPass::DeclarePlan(PassPlan& plan) {
		if (intensity <= 0.0f || radius <= 0.0f || falloffDistance <= 0.0f) {
			return;
		}

		const u16 divisor = downsample ? 2 : 1;

		const u8 estimate = plan.AddTarget(RHI::Format::R8Unorm, divisor);
		const u8 blurX = plan.AddTarget(RHI::Format::R8Unorm, divisor);
		const u8 blurY = plan.AddTarget(RHI::Format::R8Unorm, divisor);
		if (estimate == PASS_NO_READ || blurX == PASS_NO_READ || blurY == PASS_NO_READ) {
			return;
		}

		plan.AddStep(estimate);
		plan.AddStep(blurX, estimate);
		plan.AddStep(blurY, blurX);

		u8 result = blurY;
		if (downsample) {
			const u8 upsampled = plan.AddTarget(RHI::Format::R8Unorm, 1);
			if (upsampled == PASS_NO_READ) {
				return;
			}
			plan.AddStep(upsampled, blurY);
			result = upsampled;
		}

		if (afterOpaque && point < PassPoint::Opaque) {
			if (!_warnedPointDisagrees) {
				_warnedPointDisagrees = true;
				SLT_LOG_WARN("AmbientOcclusion",
							 "'{}' has afterOpaque set but runs at {} (opaque pass overwrites the composite). "
							 "Move the row to AfterOpaque (or clear afterOpaque to feed the shading)",
							 virtualPath, GetPassPointName(point));
			}
			plan.Produce(PassProduces::AmbientOcclusion, result);
			return;
		}

		if (afterOpaque) {
			plan.AddStep(PASS_SCENE_TARGET, result);
			return;
		}

		plan.Produce(PassProduces::AmbientOcclusion, result);
	}

	bool AmbientOcclusionPass::Cook(CookWriter& descriptor, CookWriter& payload) const {
		if (!RenderPass::Cook(descriptor, payload)) {
			return false;
		}
		descriptor.NoteAssetRef(AmbientOcclusionImpl::AMBIENT_OCCLUSION_SHADER);
		return true;
	}

	void AmbientOcclusionPass::Record(PassContext& context) {
		const u32 stepCount = context.GetStepCount();
		if (stepCount < 3) {
			return;
		}

		if (!_shaderResolved) {
			_shaderResolved = true;
			if (Asset* asset = AssetDatabase::LoadAssetByGuid(AmbientOcclusionImpl::AMBIENT_OCCLUSION_SHADER)) {
				if (asset->GetAssetTypeId() == StringHash("ShaderAsset")) {
					_shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(asset));
				} else {
					AssetDatabase::UnloadAsset(asset);
				}
			}
		}
		if (!_shader.Get()) {
			if (!_warnedNoShader) {
				_warnedNoShader = true;
				SLT_LOG_WARN("AmbientOcclusion", "'{}' can't resolve engine://Shaders/AmbientOcclusion.hlsl (row draws nothing)",
							 virtualPath);
			}
			return;
		}

		AmbientOcclusionPush push{};

		const Mat4 viewProjection = context.GetProjection() * context.GetView();
		push.invViewProjection = viewProjection.Inverse();

		const Mat4& projection = context.GetProjection();
		push.projHalfAndLinearize[0] = projection(0, 0) * 0.5f;
		push.projHalfAndLinearize[1] = projection(1, 1) * 0.5f;
		push.projHalfAndLinearize[2] = projection(2, 3);
		push.projHalfAndLinearize[3] = -projection(2, 2);

		const f32 clampedRadius = Math::Max(radius, 1.0e-4f);
		push.radiusFalloffIntensityBias[0] = clampedRadius;
		push.radiusFalloffIntensityBias[1] = Math::Max(falloffDistance, 0.0f);
		const bool perspective = projection(3, 2) != 0.0f;
		if (!perspective && !_warnedOrthographic) {
			_warnedOrthographic = true;
			SLT_LOG_WARN("AmbientOcclusion", "'{}' is on an orthographic camera (not modeled, no occlusion)",
						 virtualPath);
		}
		push.radiusFalloffIntensityBias[2] = perspective ? Math::Max(intensity, 0.0f) : 0.0f;
		push.radiusFalloffIntensityBias[3] = HORIZON_BIAS;

		const u32 step = context.GetStep();
		AmbientOcclusionStep kind = AmbientOcclusionStep::Estimate;
		if (step == 1) {
			kind = AmbientOcclusionStep::BlurX;
		} else if (step == 2) {
			kind = AmbientOcclusionStep::BlurY;
		} else if (step == 3) {
			kind = downsample ? AmbientOcclusionStep::Upsample : AmbientOcclusionStep::Composite;
		} else if (step >= 4) {
			kind = AmbientOcclusionStep::Composite;
		}

		const Vec2I extent = context.GetExtent();
		push.extentStepDirections[0] = static_cast<f32>(extent.x);
		push.extentStepDirections[1] = static_cast<f32>(extent.y);
		push.extentStepDirections[2] = static_cast<f32>(static_cast<u32>(kind));
		push.extentStepDirections[3] = static_cast<f32>(Math::Clamp(directions, MIN_DIRECTIONS, MAX_DIRECTIONS));

		context.DrawProcedural(_shader,
							   std::span<const u8>(reinterpret_cast<const u8*>(&push), sizeof(push)),
							   3);
	}

} // namespace Shalltear
