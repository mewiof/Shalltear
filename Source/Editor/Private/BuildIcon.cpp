#include "BuildIcon.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/ImageEncoding.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <span>
#include <system_error>

namespace Shalltear::BuildIcon {

	namespace {

		namespace fs = std::filesystem;

		constexpr std::string_view KEY = "icon";
		constexpr u32 PNG_ENTRY_SIZE = 256;

		struct SquareImage {
			u32 size = 0;
			Vector<u8> rgba{GetMemoryResource(MemTag::Editor)};
		};

		struct LinearTable {
			f32 values[256];

			LinearTable() {
				for (u32 value = 0; value < 256; ++value) {
					const f32 encoded = static_cast<f32>(value) / 255.0f;
					values[value] = encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
				}
			}
		};

		const LinearTable& ToLinear() {
			static const LinearTable table;
			return table;
		}

		f32 ToSRGB(f32 linear) {
			linear = std::clamp(linear, 0.0f, 1.0f);
			return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
		}

		u8 ToByte(f32 unit) {
			return static_cast<u8>(std::clamp(unit * 255.0f + 0.5f, 0.0f, 255.0f));
		}

		struct Tap {
			u32 source = 0;
			f32 weight = 0.0f;
		};

		struct Taps {
			Vector<u32> begin{GetMemoryResource(MemTag::Editor)};
			Vector<Tap> taps{GetMemoryResource(MemTag::Editor)};
		};

		void BuildTaps(u32 sourceLength, u32 targetLength, Taps& out) {
			out.begin.clear();
			out.taps.clear();
			const f64 step = static_cast<f64>(sourceLength) / static_cast<f64>(targetLength);
			for (u32 target = 0; target < targetLength; ++target) {
				out.begin.push_back(static_cast<u32>(out.taps.size()));
				const f64 start = target * step;
				const f64 end = (target + 1) * step;
				const u32 last = std::min(sourceLength, static_cast<u32>(std::ceil(end)));
				for (u32 source = static_cast<u32>(start); source < last; ++source) {
					const f64 overlap = std::min(end, source + 1.0) - std::max(start, static_cast<f64>(source));
					if (overlap > 0.0) {
						out.taps.push_back({source, static_cast<f32>(overlap / step)});
					}
				}
			}
			out.begin.push_back(static_cast<u32>(out.taps.size()));
		}

		void Resample(std::span<const u8> rgba, Vec2I sourceSize, u32 size, SquareImage& out) {
			const u32 width = static_cast<u32>(sourceSize.x);
			const u32 height = static_cast<u32>(sourceSize.y);
			out.size = size;
			out.rgba.assign(static_cast<size_t>(size) * size * 4, 0);

			const f64 scale = static_cast<f64>(size) / static_cast<f64>(std::max(width, height));
			const u32 fitWidth = std::clamp(static_cast<u32>(std::lround(width * scale)), 1u, size);
			const u32 fitHeight = std::clamp(static_cast<u32>(std::lround(height * scale)), 1u, size);
			const u32 left = (size - fitWidth) / 2;
			const u32 top = (size - fitHeight) / 2;

			if (fitWidth == width && fitHeight == height) {
				for (u32 y = 0; y < height; ++y) {
					for (u32 x = 0; x < width; ++x) {
						const u8* texel = rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
						if (texel[3] != 0) {
							std::memcpy(out.rgba.data() + ((static_cast<size_t>(top) + y) * size + left + x) * 4, texel, 4);
						}
					}
				}
				return;
			}

			Taps columns;
			Taps rows;
			BuildTaps(width, fitWidth, columns);
			BuildTaps(height, fitHeight, rows);
			const LinearTable& linear = ToLinear();

			Vector<f32> horizontal{GetMemoryResource(MemTag::Editor)};
			horizontal.resize(static_cast<size_t>(height) * fitWidth * 4);
			for (u32 y = 0; y < height; ++y) {
				const u8* row = rgba.data() + static_cast<size_t>(y) * width * 4;
				for (u32 x = 0; x < fitWidth; ++x) {
					f32 sum[4] = {};
					for (u32 tap = columns.begin[x]; tap < columns.begin[x + 1]; ++tap) {
						const u8* texel = row + static_cast<size_t>(columns.taps[tap].source) * 4;
						const f32 weight = columns.taps[tap].weight * (static_cast<f32>(texel[3]) / 255.0f);
						sum[0] += linear.values[texel[0]] * weight;
						sum[1] += linear.values[texel[1]] * weight;
						sum[2] += linear.values[texel[2]] * weight;
						sum[3] += weight;
					}
					std::memcpy(horizontal.data() + (static_cast<size_t>(y) * fitWidth + x) * 4, sum, sizeof(sum));
				}
			}

			for (u32 y = 0; y < fitHeight; ++y) {
				for (u32 x = 0; x < fitWidth; ++x) {
					f32 sum[4] = {};
					for (u32 tap = rows.begin[y]; tap < rows.begin[y + 1]; ++tap) {
						const f32* texel = horizontal.data() + (static_cast<size_t>(rows.taps[tap].source) * fitWidth + x) * 4;
						for (u32 channel = 0; channel < 4; ++channel) {
							sum[channel] += texel[channel] * rows.taps[tap].weight;
						}
					}
					const u8 alpha = ToByte(sum[3]);
					if (alpha == 0) {
						continue;
					}
					u8* texel = out.rgba.data() + ((static_cast<size_t>(top) + y) * size + left + x) * 4;
					for (u32 channel = 0; channel < 3; ++channel) {
						texel[channel] = ToByte(ToSRGB(sum[channel] / sum[3]));
					}
					texel[3] = alpha;
				}
			}
		}

		void Put16(Vector<u8>& out, u32 value) {
			out.push_back(static_cast<u8>(value));
			out.push_back(static_cast<u8>(value >> 8));
		}

		void Put32(Vector<u8>& out, u32 value) {
			Put16(out, value & 0xFFFFu);
			Put16(out, value >> 16);
		}

		void WriteDib(const SquareImage& image, Vector<u8>& out) {
			const u32 size = image.size;
			const u32 maskStride = (size + 31) / 32 * 4;
			Put32(out, 40);
			Put32(out, size);
			// color + mask rows
			Put32(out, size * 2);
			Put16(out, 1);
			Put16(out, 32);
			Put32(out, 0);
			Put32(out, size * size * 4 + maskStride * size);
			for (u32 field = 0; field < 4; ++field) {
				Put32(out, 0);
			}
			for (u32 row = size; row-- > 0;) {
				const u8* texel = image.rgba.data() + static_cast<size_t>(row) * size * 4;
				for (u32 x = 0; x < size; ++x, texel += 4) {
					out.push_back(texel[2]);
					out.push_back(texel[1]);
					out.push_back(texel[0]);
					out.push_back(texel[3]);
				}
			}
			for (u32 row = size; row-- > 0;) {
				const size_t start = out.size();
				out.resize(start + maskStride, 0);
				const u8* texel = image.rgba.data() + static_cast<size_t>(row) * size * 4;
				for (u32 x = 0; x < size; ++x) {
					if (texel[x * 4 + 3] == 0) {
						out[start + x / 8] |= static_cast<u8>(0x80u >> (x % 8));
					}
				}
			}
		}

		void WriteIco(std::span<const SquareImage> images, Vector<u8>& out) {
			Put16(out, 0);
			Put16(out, 1);
			Put16(out, static_cast<u32>(images.size()));
			Vector<u8> payloads{GetMemoryResource(MemTag::Editor)};
			u32 offset = static_cast<u32>(6 + 16 * images.size());
			for (const SquareImage& image : images) {
				const size_t start = payloads.size();
				if (image.size >= PNG_ENTRY_SIZE) {
					ImageEncoding::EncodePNG(image.rgba, Vec2I(static_cast<i32>(image.size), static_cast<i32>(image.size)), payloads);
				} else {
					WriteDib(image, payloads);
				}
				const u32 bytes = static_cast<u32>(payloads.size() - start);
				const u8 edge = image.size >= 256 ? 0 : static_cast<u8>(image.size);
				out.push_back(edge);
				out.push_back(edge);
				out.push_back(0);
				out.push_back(0);
				Put16(out, 1);
				Put16(out, 32);
				Put32(out, bytes);
				Put32(out, offset);
				offset += bytes;
			}
			out.insert(out.end(), payloads.begin(), payloads.end());
		}

		void WriteDocument(std::span<const SquareImage> images, str& out) {
			for (const SquareImage& image : images) {
				const PakIconImage header{image.size, image.size};
				out.append(reinterpret_cast<const char*>(&header), sizeof(header));
				out.append(reinterpret_cast<const char*>(image.rgba.data()), image.rgba.size());
			}
		}

		void ResampleAll(const TextureAsset& source, std::span<const u32> sizes, Vector<SquareImage>& out) {
			out.clear();
			for (u32 size : sizes) {
				SquareImage image;
				Resample({source.pixels.data(), source.pixels.size()}, source.size, size, image);
				out.push_back(std::move(image));
			}
		}

	} // anonymous namespace

	void DescribeSetting(Vector<BuildSettingDesc>& out) {
		BuildSettingDesc icon;
		icon.key = str(KEY);
		icon.type = BuildSettingType::Path;
		icon.merge = BuildMergePolicy::Replace;
		icon.label = "Icon";
		icon.help = "image in this project (256x256 or larger), scaled to each icon size, empty = system icon";
		out.push_back(std::move(icon));
	}

	void Validate(const str& projectPath, const ResolvedProfile& profile, Vector<BuildIssue>& out) {
		const ResolvedSetting* setting = profile.Find(KEY);
		if (!setting || setting->value.empty()) {
			return;
		}
		const str& value = setting->value;
		const bool absolute = Path(value).IsAbsolute();
		if (absolute) {
			out.push_back(BuildIssue{BuildIssueSeverity::Warning, profile.name, str(KEY),
									 "'" + value + "' is an absolute path (only valid on 1 machine)"});
		}
		std::error_code ec;
		const fs::path file = absolute ? fs::path{value} : fs::path{projectPath} / fs::path{value};
		if (!fs::is_regular_file(file, ec) || ec) {
			out.push_back(BuildIssue{BuildIssueSeverity::Error, profile.name, str(KEY),
									 "'" + value + "' is not a file in this project"});
		}
	}

	BuildStepStatus Step(BuildContext& context) {
		context.executableIconFile.clear();
		context.windowIconDocument.clear();
		const ResolvedSetting* setting = context.profile ? context.profile->Find(KEY) : nullptr;
		if (!setting || setting->value.empty() ||
			(context.executableIconSizes.empty() && context.windowIconSizes.empty())) {
			return BuildStepStatus::Succeeded;
		}
		const str& value = setting->value;
		const str path = Path(value).IsAbsolute() ? value : context.projectRoot + "/" + value;

		TextureAsset source;
		source.settings.generateMips = false;
		source.settings.shape = TextureShape::Texture2D;
		source.settings.compression = TextureCompression::None;
		if (!source.Import(Path(path)) || source.size.x <= 0 || source.size.y <= 0) {
			context.AddIssue(BuildIssueSeverity::Error, KEY, "couldn't read '" + value + "' as an image. Use a PNG/JPG/TGA/BMP file");
			return BuildStepStatus::Failed;
		}
		if (source.format != RHI::Format::RGBA8SRGB && source.format != RHI::Format::RGBA8Unorm) {
			context.AddIssue(BuildIssueSeverity::Error, KEY, "'" + value + "' is an HDR image. Use an 8 bit PNG");
			return BuildStepStatus::Failed;
		}

		u32 largest = 0;
		for (const Vector<u32>* sizes : {&context.executableIconSizes, &context.windowIconSizes}) {
			for (u32 size : *sizes) {
				largest = std::max(largest, size);
			}
		}
		const u32 edge = static_cast<u32>(std::max(source.size.x, source.size.y));
		if (edge < largest) {
			context.AddIssue(BuildIssueSeverity::Warning, KEY,
							 std::format("'{}' is {}x{} (scaled up to {}x{}). Use {}x{} or larger", value, source.size.x,
										 source.size.y, largest, largest, largest, largest));
		}

		Vector<SquareImage> images{GetMemoryResource(MemTag::Editor)};
		if (!context.executableIconSizes.empty()) {
			ResampleAll(source, context.executableIconSizes, images);
			Vector<u8> ico{GetMemoryResource(MemTag::Editor)};
			WriteIco(images, ico);

			const str file = context.intermediateDir + "/Icon.ico";
			std::error_code ec;
			fs::create_directories(fs::path{context.intermediateDir}, ec);
			Buffer bytes(ico.size(), MemTag::Editor);
			std::memcpy(bytes.data, ico.data(), ico.size());
			if (!FileSystem::WriteFile(Path(file), bytes)) {
				context.AddIssue(BuildIssueSeverity::Error, KEY, "couldn't write '" + file + "'");
				return BuildStepStatus::Failed;
			}
			context.executableIconFile = file;
			context.Log(std::format("icon '{}' ({}x{}) -> {} size(s) in Icon.ico ({} B)", value, source.size.x,
									source.size.y, images.size(), ico.size()));
		}
		if (!context.windowIconSizes.empty()) {
			ResampleAll(source, context.windowIconSizes, images);
			WriteDocument(images, context.windowIconDocument);
			context.Log(std::format("icon '{}' ({}x{}) -> {} size(s) in package ({} B)", value, source.size.x,
									source.size.y, images.size(), context.windowIconDocument.size()));
		}
		return BuildStepStatus::Succeeded;
	}

} // namespace Shalltear::BuildIcon
