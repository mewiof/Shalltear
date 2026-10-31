#include "Shalltear/PostProcessing/PostProcessPass.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include "Shalltear/PostProcessing/PostProcessVolume.h"

#include "PostProcessBlend.h"
#include "PostProcessShader.h"

#include <array>
#include <cmath>

namespace Shalltear {

	namespace {

		// must match ShalltearPostProcessPush in PostProcess.hlsl
		struct PostProcessPush {
			f32 colorFilterAndContrast[4];
			f32 hueSaturationAndFlags[4];
			f32 vignetteColorAndIntensity[4];
			f32 vignetteCenterSmoothnessRounded[4];
			// xy = step extent px, z = step kind, w = post exposure multiplier
			f32 extentAndStep[4];
			f32 bloomTintAndIntensity[4];
			// x = linear threshold, y = knee, z = scatter, w = clamp
			f32 bloomParams[4];
		};
		static_assert(sizeof(PostProcessPush) == 112, "the push block is 7 float4s (under the RHI's 128 B budget)");

		// shader reads these
		enum class BloomStep : u32 {
			Prefilter = 0,
			Downsample = 1,
			Upsample = 2,
			Composite = 3
		};

		constexpr i32 BLOOM_MIN_LEVEL_TEXELS = 2;

		// 2 chains of this many lvls must fit PassPlan::MAX_TARGETS
		constexpr u32 MAX_BLOOM_LEVELS = 10;

		constexpr RHI::Format BLOOM_FORMAT = RHI::Format::RGBA16Float;

		// piecewise sRGB (not a 2.2 power)
		f32 GammaToLinear(f32 value) {
			return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
		}

	} // anonymous namespace

	struct PostProcessPass::Resolved {
		PostProcessOps::ResolvedPostProcess values{};
		Vec3 at{};
		bool valid = false;
	};

	PostProcessPass::PostProcessPass() {
		point = PassPoint::Post;
		_resolved = MakeUnique<Resolved>();
	}

	PostProcessPass::~PostProcessPass() = default;

	const PostProcessPass::Resolved& PostProcessPass::ResolveFor(const Vec3& cameraPosition, bool refresh) {
		if (!refresh && _resolved->valid && _resolved->at == cameraPosition) {
			return *_resolved;
		}
		_resolved->at = cameraPosition;
		_resolved->valid = true;

		const u32 volumeCount = PostProcessVolume::GetEnabledCount();

		constexpr u32 MAX_SAMPLES = 16;
		std::array<PostProcessOps::VolumeSample, MAX_SAMPLES> samples{};
		u32 sampleCount = 0;

		for (u32 index = 0; index < volumeCount; ++index) {
			PostProcessVolume* volume = PostProcessVolume::GetEnabled(index);
			if (!volume || !volume->profile.Get()) {
				continue;
			}
			const SceneObject* owner = volume->GetOwner().Get();
			const Mat4 localToWorld = owner ? owner->GetWorldMatrix() : Mat4::Identity();
			const f32 contribution = VolumeContribution(
				volume->isGlobal, localToWorld, volume->size, volume->blendDistance, volume->weight, cameraPosition);
			if (contribution <= 0.0f) {
				continue;
			}
			if (sampleCount == MAX_SAMPLES) {
				SLT_LOG_WARN("PostProcessing", "more than {} volumes contribute to one view (rest ignored this frame)", MAX_SAMPLES);
				break;
			}
			samples[sampleCount].priority = volume->priority;
			samples[sampleCount].contribution = contribution;
			samples[sampleCount].profile = volume->profile.Get();
			++sampleCount;
		}

		_resolved->values = PostProcessOps::Blend(std::span<PostProcessOps::VolumeSample>(samples.data(), sampleCount));
		return *_resolved;
	}

	void PostProcessPass::DeclarePlan(PassPlan& plan) {
		const PostProcessOps::ResolvedPostProcess& resolved = ResolveFor(plan.GetCameraPosition(), true).values;
		if (!resolved.BloomContributes()) {
			return;
		}

		const Vec2I extent = plan.GetExtent();
		const i32 shortAxis = Math::Min(extent.x, extent.y);
		const i32 cap = Math::Clamp(resolved.bloomMaxIterations, 1, static_cast<i32>(MAX_BLOOM_LEVELS));
		i32 levels = 1;
		while (levels < cap && (shortAxis >> (levels + 1)) >= BLOOM_MIN_LEVEL_TEXELS) {
			++levels;
		}

		u8 down[MAX_BLOOM_LEVELS] = {};
		u8 up[MAX_BLOOM_LEVELS] = {};
		for (i32 level = 0; level < levels; ++level) {
			down[level] = plan.AddTarget(BLOOM_FORMAT, static_cast<u16>(1 << (level + 1)));
		}
		for (i32 level = 0; level + 1 < levels; ++level) {
			up[level] = plan.AddTarget(BLOOM_FORMAT, static_cast<u16>(1 << (level + 1)));
		}

		plan.AddStep(down[0]);
		for (i32 level = 1; level < levels; ++level) {
			plan.AddStep(down[level], down[level - 1]);
		}
		for (i32 level = levels - 2; level >= 0; --level) {
			const u8 lower = level == levels - 2 ? down[levels - 1] : up[level + 1];
			plan.AddStep(up[level], lower, down[level]);
		}
		plan.AddStep(PASS_SCENE_TARGET, levels >= 2 ? up[0] : down[0]);
	}

	bool PostProcessPass::Cook(CookWriter& descriptor, CookWriter& payload) const {
		if (!RenderPass::Cook(descriptor, payload)) {
			return false;
		}
		descriptor.NoteAssetRef(PostProcessingImpl::POST_PROCESS_SHADER);
		return true;
	}

	void PostProcessPass::Record(PassContext& context) {
		const PostProcessOps::ResolvedPostProcess& resolved = ResolveFor(context.GetCameraPosition(), false).values;

		if (!resolved.AnyEnabled()) {
			return;
		}

		if (!_shaderResolved) {
			_shaderResolved = true;
			if (Asset* asset = AssetDatabase::LoadAssetByGuid(PostProcessingImpl::POST_PROCESS_SHADER)) {
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
				SLT_LOG_WARN("PostProcessing", "'{}' can't resolve engine://Shaders/PostProcess.hlsl (row draws nothing)", virtualPath);
			}
			return;
		}

		const u32 stepCount = context.GetStepCount();
		const u32 step = context.GetStep();
		const u32 levels = stepCount / 2;
		BloomStep kind = BloomStep::Composite;
		if (levels > 0 && step + 1 < stepCount) {
			kind = step == 0 ? BloomStep::Prefilter
							 : (step < levels ? BloomStep::Downsample : BloomStep::Upsample);
		}

		PostProcessPush push{};
		push.colorFilterAndContrast[0] = resolved.colorFilter.r;
		push.colorFilterAndContrast[1] = resolved.colorFilter.g;
		push.colorFilterAndContrast[2] = resolved.colorFilter.b;
		push.colorFilterAndContrast[3] = 1.0f + resolved.contrast * 0.01f;
		push.hueSaturationAndFlags[0] = resolved.hueShift * (3.14159265358979323846f / 180.0f);
		push.hueSaturationAndFlags[1] = 1.0f + resolved.saturation * 0.01f;
		push.hueSaturationAndFlags[2] = resolved.colorGrading ? 1.0f : 0.0f;
		push.hueSaturationAndFlags[3] = resolved.vignette ? 1.0f : 0.0f;

		push.vignetteColorAndIntensity[0] = resolved.vignetteColor.r;
		push.vignetteColorAndIntensity[1] = resolved.vignetteColor.g;
		push.vignetteColorAndIntensity[2] = resolved.vignetteColor.b;
		push.vignetteColorAndIntensity[3] = Math::Clamp(resolved.vignetteIntensity, 0.0f, 1.0f) * 3.0f;
		push.vignetteCenterSmoothnessRounded[0] = resolved.vignetteCenter.x;
		push.vignetteCenterSmoothnessRounded[1] = resolved.vignetteCenter.y;
		push.vignetteCenterSmoothnessRounded[2] = 1.0f / Math::Clamp(resolved.vignetteSmoothness, 0.01f, 1.0f);
		push.vignetteCenterSmoothnessRounded[3] = resolved.vignetteRounded ? 1.0f : 0.0f;

		const Vec2I extent = context.GetExtent();
		push.extentAndStep[0] = static_cast<f32>(extent.x);
		push.extentAndStep[1] = static_cast<f32>(extent.y);
		push.extentAndStep[2] = static_cast<f32>(static_cast<u32>(kind));
		push.extentAndStep[3] = std::exp2(resolved.postExposure);

		const bool hasPyramid = levels > 0;
		push.bloomTintAndIntensity[0] = resolved.bloomTint.r;
		push.bloomTintAndIntensity[1] = resolved.bloomTint.g;
		push.bloomTintAndIntensity[2] = resolved.bloomTint.b;
		push.bloomTintAndIntensity[3] = hasPyramid ? Math::Clamp(resolved.bloomIntensity, 0.0f, 1.0f) : 0.0f;
		const f32 thresholdLinear = GammaToLinear(Math::Max(resolved.bloomThreshold, 0.0f));
		push.bloomParams[0] = thresholdLinear;
		push.bloomParams[1] = thresholdLinear * 0.5f;
		push.bloomParams[2] = 0.05f + Math::Clamp(resolved.bloomScatter, 0.0f, 1.0f) * 0.9f;
		push.bloomParams[3] = Math::Max(resolved.bloomClamp, 0.0f);

		context.DrawProcedural(_shader,
							   std::span<const u8>(reinterpret_cast<const u8*>(&push), sizeof(push)),
							   3);
	}

} // namespace Shalltear
