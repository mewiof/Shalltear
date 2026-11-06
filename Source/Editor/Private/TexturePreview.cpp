#include "PreviewRig.h"

#include "Shalltear/Editor/EditorPreviews.h"

#include "Shalltear/Core/Graphics/SpriteAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"

#include "Shalltear/Core/Graphics/TextureCodecs.h"

#include "Shalltear/Editor/SpriteEditorOps.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace Shalltear {

	namespace EditorPreviewsInternal {
		bool ProduceMaterialPreview(const AssetPreviewRequest& request, AssetPreviewImage& out);
		bool ProduceMeshPreview(const AssetPreviewRequest& request, AssetPreviewImage& out);
		bool ProducePrefabPreview(const AssetPreviewRequest& request, AssetPreviewImage& out);
		void ReleaseMaterialPreviewAssets();

		void ReleaseBuiltInAssets() {
			ReleaseMaterialPreviewAssets();
			ReleaseSharedDefaultMaterial();
		}
	} // namespace EditorPreviewsInternal

	void EditorPreviews::RegisterBuiltIns() {
		Initialize();

		RegisterProducer(StringHash("MaterialAsset"), &EditorPreviewsInternal::ProduceMaterialPreview);
		RegisterProducer(StringHash("MeshAsset"), &EditorPreviewsInternal::ProduceMeshPreview);
		RegisterProducer(StringHash("PrefabAsset"), &EditorPreviewsInternal::ProducePrefabPreview);

		RegisterProducer(
			StringHash("TextureAsset"),
			[](const AssetPreviewRequest& request, AssetPreviewImage& out) {
				TextureAsset& texture = static_cast<TextureAsset&>(*request.asset);
				if (texture.size.x <= 0 || texture.size.y <= 0 || texture.pixels.empty()) {
					return false;
				}
				out.pixels = {texture.pixels.data(), texture.pixels.size()};
				out.pixelsFormat = texture.format;
				if (RHI::IsBlockCompressed(texture.format)) {
					const TextureCodec* codec = TextureCodecs::Find(texture.format);
					const RHI::Format decoded = TextureCodecs::DecodedFormat(texture.format);
					const size_t levelBytes = static_cast<size_t>(RHI::FormatLevelBytes(texture.format, texture.size));
					out.decodedPixels.resize(static_cast<size_t>(RHI::FormatLevelBytes(decoded, texture.size)));
					if (!codec || texture.pixels.size() < levelBytes ||
						!codec->decode(texture.format, {texture.pixels.data(), levelBytes}, texture.size, out.decodedPixels)) {
						return false;
					}
					if (texture.format == RHI::Format::BC5Unorm) {
						for (size_t i = 0; i < out.decodedPixels.size(); i += 4) {
							const f32 x = out.decodedPixels[i] / 255.0f * 2.0f - 1.0f;
							const f32 y = out.decodedPixels[i + 1] / 255.0f * 2.0f - 1.0f;
							const f32 z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
							out.decodedPixels[i + 2] = static_cast<u8>(z * 127.5f + 127.5f + 0.5f);
						}
					}
					out.pixels = {out.decodedPixels.data(), out.decodedPixels.size()};
					out.pixelsFormat = decoded;
				}
				out.size = texture.size;
				out.sampler = texture.settings.filter == TextureFilter::Point
								  ? UI::SamplerKind::NearestClamp
								  : UI::SamplerKind::LinearClamp;
				out.detail = EditorPreviewOps::DescribeTexture(texture.size, texture.format);
				return true;
			});

		RegisterProducer(
			StringHash("SpriteAsset"),
			[](const AssetPreviewRequest& request, AssetPreviewImage& out) {
				const SpriteAsset& sprite = static_cast<const SpriteAsset&>(*request.asset);
				const TextureAsset* texture = sprite.GetTexture();
				const TextureSprite* definition = sprite.GetDefinition();
				if (!texture || !definition) {
					return false;
				}
				out.dependencies.push_back(texture->assetId);
				Vector<u8> texels{GetMemoryResource(MemTag::Editor)};
				if (!SpriteEditorOps::DecodeTexels(*texture, texels)) {
					return false;
				}
				const RectI& rect = definition->rect;
				out.decodedPixels.resize(static_cast<size_t>(rect.size.x) * rect.size.y * 4);
				for (i32 y = 0; y < rect.size.y; ++y) {
					const size_t from = (static_cast<size_t>(rect.Top() + y) * texture->size.x + rect.Left()) * 4;
					std::memcpy(out.decodedPixels.data() + static_cast<size_t>(y) * rect.size.x * 4, texels.data() + from,
								static_cast<size_t>(rect.size.x) * 4);
				}
				out.pixels = {out.decodedPixels.data(), out.decodedPixels.size()};
				out.pixelsFormat = RHI::IsSRGB(texture->format) ? RHI::Format::RGBA8SRGB : RHI::Format::RGBA8Unorm;
				out.size = rect.size;
				out.sampler = texture->settings.filter == TextureFilter::Point ? UI::SamplerKind::NearestClamp : UI::SamplerKind::LinearClamp;
				out.detail = std::format("{}x{} in {}x{}", rect.size.x, rect.size.y, texture->size.x, texture->size.y);
				return true;
			});
	}

} // namespace Shalltear
