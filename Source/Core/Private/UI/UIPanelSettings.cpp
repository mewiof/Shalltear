#include "Shalltear/Core/UI/UIPanelSettings.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <algorithm>

SLT_REGISTER_TYPE(Shalltear::UIPanelSettings)

namespace Shalltear {

	namespace {

		constexpr f32 MIN_CONTENT_SCALE = 0.01f;
		constexpr f32 DPI_AT_SCALE_ONE = 96.0f;

	} // anonymous namespace

	f32 UIPanelSettings::ResolveContentScale(Vec2I screenSizePx, f32 dpiScale) const {
		f32 magnification = 1.0f;
		switch (scaleMode) {
		case UIPanelScaleMode::ConstantPixelSize:
			break;
		case UIPanelScaleMode::ConstantPhysicalSize: {
			const f32 dpi = dpiScale * DPI_AT_SCALE_ONE;
			if (referenceDPI > 0.0f && dpi > 0.0f) {
				magnification = dpi / referenceDPI;
			}
			break;
		}
		case UIPanelScaleMode::ScaleWithScreenSize: {
			if (referenceResolution.x > 0 && referenceResolution.y > 0 && screenSizePx.x > 0 && screenSizePx.y > 0) {
				const f32 widthRatio = static_cast<f32>(screenSizePx.x) / static_cast<f32>(referenceResolution.x);
				const f32 heightRatio = static_cast<f32>(screenSizePx.y) / static_cast<f32>(referenceResolution.y);
				switch (screenMatchMode) {
				case UIPanelScreenMatchMode::Expand:
					magnification = std::min(widthRatio, heightRatio);
					break;
				case UIPanelScreenMatchMode::Shrink:
					magnification = std::max(widthRatio, heightRatio);
					break;
				case UIPanelScreenMatchMode::MatchWidthOrHeight:
				default: {
					const f32 t = std::clamp(match, 0.0f, 1.0f);
					magnification = widthRatio + (heightRatio - widthRatio) * t;
					break;
				}
				}
			}
			break;
		}
		}
		return std::max(magnification * scale, MIN_CONTENT_SCALE);
	}

	f32 UIPanelSettings::ResolveDefaultContentScale(Vec2I, f32 dpiScale) {
		return std::max(dpiScale, MIN_CONTENT_SCALE);
	}

	bool UIPanelSettings::Import(const Path& sourcePath) {
		if (!FileSystem::Exists(sourcePath)) {
			SLT_LOG_ERROR("Asset", "UIPanelSettings::Import: source '{}' doesn't exist", sourcePath.ToString());
			return false;
		}
		return TextSerializer::Deserialize(this, GetReflectType(), FileSystem::ReadTextFile(sourcePath, MemTag::Asset));
	}

	bool UIPanelSettings::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, TextSerializer::Serialize(this, GetReflectType()));
	}

	bool UIPanelSettings::Cook(CookWriter& descriptor, CookWriter& payload) const {
		if (!Asset::Cook(descriptor, payload)) {
			return false;
		}
		if (const FontAsset* face = font.Get(); face && face->assetId.IsValid()) {
			descriptor.NoteAssetRef(face->assetId);
		}
		return true;
	}

	Asset* UIPanelSettingsImporter::Import(const Path& sourcePath) {
		UIPanelSettings* asset = New<UIPanelSettings>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	Asset* UIPanelSettingsImporter::CreateAsset() {
		return New<UIPanelSettings>(MemTag::Asset);
	}

} // namespace Shalltear
