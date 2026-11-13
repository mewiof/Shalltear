#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class TonemapCurve : u8 {
		None = 0,
		Neutral = 1,
		ACES = 2,
		AgX = 3
	};

	enum class TextureAnisotropy : u8 {
		Disabled = 0,
		PerTexture = 1,
		ForcedOn = 2
	};

	enum class ShadowCascadeFit : u8 {
		Fixed = 0,
		VisibleDepth = 1,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::TonemapCurve)
SLT_ENUM_VALUE(None)
SLT_ENUM_VALUE(Neutral)
SLT_ENUM_VALUE(ACES)
SLT_ENUM_VALUE(AgX)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::TextureAnisotropy)
SLT_ENUM_VALUE(Disabled)
SLT_ENUM_VALUE(PerTexture)
SLT_ENUM_VALUE(ForcedOn)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::ShadowCascadeFit)
SLT_ENUM_VALUE(Fixed)
SLT_ENUM_VALUE(VisibleDepth)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_CORE_API GraphicsProfile : public Asset {
	public:
		SLT_REFLECT_CLASS(GraphicsProfile)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()

		SLT_REFLECT_FIELD(msaa, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))
		SLT_REFLECT_FIELD(renderScale, SLT_RANGE(0.25f, 2.0f), SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))

		SLT_REFLECT_FIELD(tonemap, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))

		SLT_REFLECT_FIELD(lightsPerView, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))
		SLT_REFLECT_FIELD(lightsPerCluster, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))
		SLT_REFLECT_FIELD(decalsPerCluster, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))

		SLT_REFLECT_FIELD(shadowDistance, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(shadowCascades, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(shadowFirstCascadeDistance, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(shadowCascadeFit, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(shadowPcfTaps, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(shadowAtlasSize, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))
		SLT_REFLECT_FIELD(objectShadows, SLT_RANGE(0, 16), SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(objectShadowResolution, SLT_RANGE(64, 4096), SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(transparentReceiveShadows, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(reflectionProbeResolution, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))

		SLT_REFLECT_FIELD(anisotropicFiltering, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))
		SLT_REFLECT_FIELD(maxAnisotropy, SLT_RANGE(1, 16), SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))

		SLT_REFLECT_FIELD(lodBias, SLT_RANGE(0.1f, 4.0f), SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(maximumLODLevel, SLT_RANGE(0, 7), SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(lodCrossFade, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_END_REFLECT()

		GraphicsProfile() = default;

		StringHash GetAssetTypeId() const override { return GraphicsProfile::GetReflectType()->name; }

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;

		i32 msaa = 1;

		f32 renderScale = 1.0f;

		TonemapCurve tonemap = TonemapCurve::None;

		i32 lightsPerView = 256;

		i32 lightsPerCluster = 32;

		i32 decalsPerCluster = 32;

		f32 shadowDistance = 60.0f;

		i32 shadowCascades = 4;

		f32 shadowFirstCascadeDistance = 4.0f;

		// VisibleDepth: cascades fitted to depth read back from earlier frames
		ShadowCascadeFit shadowCascadeFit = ShadowCascadeFit::Fixed;

		i32 shadowPcfTaps = 5;

		i32 shadowAtlasSize = 2048;

		// most ObjectShadow maps per view, picked by priority then distance
		i32 objectShadows = 4;
		// texels per side, budget's maps capped at 1/2 of punctual atlas
		i32 objectShadowResolution = 1024;

		bool transparentReceiveShadows = true;

		i32 reflectionProbeResolution = 128;

		TextureAnisotropy anisotropicFiltering = TextureAnisotropy::PerTexture;

		i32 maxAnisotropy = 16;

		// multiplier on screen relative height, > 1 for farther detail
		f32 lodBias = 1.0f;
		// finest group level drawn, last level when group has fewer
		i32 maximumLODLevel = 0;
		bool lodCrossFade = true;
	};

	class SHALLTEAR_CORE_API GraphicsProfileImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("GraphicsProfile"); }
		Asset* Import(const Path& sourcePath) override;

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return "Rendering/Graphics Profile"; }
		std::string_view GetCreateDefaultFileName() const override { return "New Graphics Profile.slprofile"; }
		Asset* CreateAsset() override;
	};

} // namespace Shalltear
