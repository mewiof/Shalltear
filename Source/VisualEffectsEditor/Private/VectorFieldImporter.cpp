#include "VisualEffectsEditor.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <cstring>

namespace Shalltear::VisualEffectsEditor {

	namespace {

		constexpr size_t VF_HEADER_BYTES = 10;

		u16 ReadU16(const u8* bytes) { return static_cast<u16>(bytes[0] | (bytes[1] << 8)); }

		f32 ReadF32(const u8* bytes) {
			f32 value = 0.0f;
			std::memcpy(&value, bytes, sizeof(value));
			return value;
		}

		class VectorFieldImporter : public IAssetImporter {
		public:
			StringHash GetAssetType() const override { return TextureAsset::GetReflectType()->name; }

			Asset* Import(const Path& sourcePath) override { return ImportWithSettings(sourcePath, Path(), JSONValue{}); }

			Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) override {
				const str name = virtualPath.ToString().empty() ? sourcePath.ToString() : virtualPath.ToString();
				Buffer file = FileSystem::ReadFile(sourcePath, MemTag::Asset);
				if (!file || file.size < VF_HEADER_BYTES) {
					SLT_LOG_ERROR("VisualEffects", "'{}' isn't a vector field (unreadable or shorter than its {}-byte header)", name, VF_HEADER_BYTES);
					return nullptr;
				}
				const u8* bytes = static_cast<const u8*>(file.data);
				u32 channels = 0;
				if (std::memcmp(bytes, "VF_F", 4) == 0) {
					channels = 1;
				} else if (std::memcmp(bytes, "VF_V", 4) == 0) {
					channels = 3;
				} else {
					SLT_LOG_ERROR("VisualEffects", "'{}' isn't a vector field (its header isn't VF_F or VF_V)", name);
					return nullptr;
				}
				const u32 width = ReadU16(bytes + 4);
				const u32 height = ReadU16(bytes + 6);
				const u32 depth = ReadU16(bytes + 8);
				const size_t texels = static_cast<size_t>(width) * height * depth;
				const size_t expected = VF_HEADER_BYTES + texels * channels * sizeof(f32);
				if (texels == 0 || file.size != expected) {
					SLT_LOG_ERROR("VisualEffects", "'{}' is a {}x{}x{} vector field of {} B (that size should be {} B)", name, width, height, depth, file.size,
								  expected);
					return nullptr;
				}

				std::string_view format = "half";
				std::string_view wrap = "repeat";
				std::string_view filter = "linear";
				if (settings.IsObject()) {
					format = settings["outputFormat"].AsString(format);
					wrap = settings["wrapMode"].AsString(wrap);
					filter = settings["filterMode"].AsString(filter);
				}
				if (format == "float") {
					SLT_LOG_WARN("VisualEffects", "'{}' asks for 32 bit floats (no texture format here holds them). Stored as Half", name);
				}
				const bool bytesOut = format == "byte";

				TextureAsset* asset = New<TextureAsset>(MemTag::Asset);
				asset->settings.sRGB = false;
				asset->settings.generateMips = false;
				asset->settings.shape = TextureShape::Texture3D;
				asset->settings.filter = filter == "nearest" ? RHI::FilterMode::Nearest : RHI::FilterMode::Linear;
				asset->settings.addressMode = wrap == "clamp"	 ? RHI::AddressMode::ClampToEdge
											  : wrap == "mirror" ? RHI::AddressMode::MirroredRepeat
																 : RHI::AddressMode::Repeat;
				asset->size = Vec2I(static_cast<i32>(width), static_cast<i32>(height));
				asset->slices = depth;
				asset->format = bytesOut ? RHI::Format::RGBA8Unorm : RHI::Format::RGBA16Float;
				asset->pixels.resize(texels * (bytesOut ? 4 : 8));
				const u8* data = bytes + VF_HEADER_BYTES;
				for (size_t i = 0; i < texels; ++i) {
					f32 texel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
					for (u32 c = 0; c < channels; ++c) {
						texel[c] = ReadF32(data + (i * channels + c) * sizeof(f32));
					}
					for (u32 c = 0; c < 4; ++c) {
						if (bytesOut) {
							asset->pixels[i * 4 + c] = static_cast<u8>(Math::Clamp(texel[c], 0.0f, 1.0f) * 255.0f + 0.5f);
						} else {
							const u16 half = Math::FloatToHalf(texel[c]);
							std::memcpy(asset->pixels.data() + (i * 4 + c) * 2, &half, sizeof(half));
						}
					}
				}
				return asset;
			}

			void SerializeSettings(const Path&, JSONValue& out) const override {
				out.AddMember("outputFormat", JSONValue::MakeString("half"));
				out.AddMember("wrapMode", JSONValue::MakeString("repeat"));
				out.AddMember("filterMode", JSONValue::MakeString("linear"));
			}
		};

		VectorFieldImporter& GetVectorFieldImporter() {
			static VectorFieldImporter importer;
			return importer;
		}

	} // anonymous namespace

	void RegisterVectorFieldImporter(const void* owner) { AssetImporterRegistry::RegisterExtension(".vf", &GetVectorFieldImporter(), owner); }

} // namespace Shalltear::VisualEffectsEditor
