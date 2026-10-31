#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/FontAsset.h"

namespace Shalltear {

	enum class UIPanelScaleMode : u8 {
		ConstantPixelSize = 0,
		ConstantPhysicalSize = 1,
		ScaleWithScreenSize = 2
	};

	enum class UIPanelScreenMatchMode : u8 {
		MatchWidthOrHeight = 0,
		Shrink = 1,
		Expand = 2
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::UIPanelScaleMode)
SLT_ENUM_VALUE(ConstantPixelSize)
SLT_ENUM_VALUE(ConstantPhysicalSize)
SLT_ENUM_VALUE(ScaleWithScreenSize)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::UIPanelScreenMatchMode)
SLT_ENUM_VALUE(MatchWidthOrHeight)
SLT_ENUM_VALUE(Shrink)
SLT_ENUM_VALUE(Expand)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_CORE_API UIPanelSettings : public Asset {
	public:
		SLT_REFLECT_CLASS(UIPanelSettings)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(scaleMode)
		SLT_REFLECT_FIELD(scale, SLT_RANGE(0.01f, 100.0f))
		SLT_REFLECT_FIELD(referenceDPI, SLT_RANGE(1.0f, 1000.0f))
		SLT_REFLECT_FIELD(referenceResolution)
		SLT_REFLECT_FIELD(screenMatchMode)
		SLT_REFLECT_FIELD(match, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(sortOrder)
		SLT_REFLECT_FIELD(font)
		SLT_REFLECT_FIELD(fontSizePx, SLT_RANGE(1.0f, 512.0f))
		SLT_REFLECT_FIELD(textColor)
		SLT_END_REFLECT()

		UIPanelSettings() = default;

		StringHash GetAssetTypeId() const override { return UIPanelSettings::GetReflectType()->name; }

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;
		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;

		UIPanelScaleMode scaleMode = UIPanelScaleMode::ConstantPhysicalSize;
		f32 scale = 1.0f;
		f32 referenceDPI = 96.0f;
		Vec2I referenceResolution{1200, 800};
		UIPanelScreenMatchMode screenMatchMode = UIPanelScreenMatchMode::MatchWidthOrHeight;
		f32 match = 0.0f;
		f32 sortOrder = 0.0f;

		Handle<FontAsset> font;
		f32 fontSizePx = 14.0f;
		Color textColor = Color::White();

		// dpiScale 1.0 = 96 DPI
		f32 ResolveContentScale(Vec2I screenSizePx, f32 dpiScale) const;

		static f32 ResolveDefaultContentScale(Vec2I screenSizePx, f32 dpiScale);
	};

	class SHALLTEAR_CORE_API UIPanelSettingsImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("UIPanelSettings"); }
		Asset* Import(const Path& sourcePath) override;

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return "Panel Settings"; }
		std::string_view GetCreateDefaultFileName() const override { return "New Panel Settings.slpanel"; }
		Asset* CreateAsset() override;
	};

} // namespace Shalltear
