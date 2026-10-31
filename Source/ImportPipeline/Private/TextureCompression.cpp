#include "TextureCompression.h"

#include "BC6HDecoder.h"

#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Memory.h"

#include "ThirdParty/bc7e/basisu_bc7e_scalar.h"
#include "ThirdParty/bc7enc_rdo/bc7decomp.h"
#include "ThirdParty/bc7enc_rdo/rgbcx.h"
#include "ThirdParty/cmp_core/cmp_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Shalltear {

	namespace {

		Vec2I LevelSize(Vec2I size, u32 level) {
			return Vec2I(std::max(1, size.x >> level), std::max(1, size.y >> level));
		}

		size_t TexelBytes(Vec2I size) {
			return static_cast<size_t>(size.x) * static_cast<size_t>(size.y) * 4;
		}

		RHI::Format InputFormat(RHI::Format format) {
			return format == RHI::Format::BC6HUFloat ? RHI::Format::RGBA16Float : RHI::Format::RGBA8Unorm;
		}

		constexpr f32 BC6H_QUALITY = 0.05f;
		void* g_bc6hOptions = nullptr;

		struct SRGBTable {
			f32 toLinear[256];
			SRGBTable() {
				for (u32 value = 0; value < 256; ++value) {
					const f32 encoded = static_cast<f32>(value) / 255.0f;
					toLinear[value] = encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
				}
			}
		};

		const SRGBTable& Table() {
			static const SRGBTable table;
			return table;
		}

		f32 LinearToSRGB(f32 linear) {
			linear = std::clamp(linear, 0.0f, 1.0f);
			return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
		}

		u8 ToByte(f32 unit) {
			return static_cast<u8>(std::clamp(unit * 255.0f + 0.5f, 0.0f, 255.0f));
		}

		f32 Expand(u8 value, u32 channel, TexelMeaning meaning) {
			if (channel == 3 || meaning == TexelMeaning::Linear) {
				return static_cast<f32>(value) / 255.0f;
			}
			if (meaning == TexelMeaning::SRGBColor) {
				return Table().toLinear[value];
			}
			return static_cast<f32>(value) / 255.0f * 2.0f - 1.0f;
		}

		template <typename Fetch>
		void Reduce(Fetch&& fetch, Vec2I aboveSize, TexelMeaning meaning, Vector<f32>& below, Vec2I belowSize) {
			below.resize(TexelBytes(belowSize));
			for (i32 y = 0; y < belowSize.y; ++y) {
				const i32 y0 = std::min(2 * y, aboveSize.y - 1);
				const i32 y1 = std::min(2 * y + 1, aboveSize.y - 1);
				for (i32 x = 0; x < belowSize.x; ++x) {
					const i32 x0 = std::min(2 * x, aboveSize.x - 1);
					const i32 x1 = std::min(2 * x + 1, aboveSize.x - 1);
					f32* texel = below.data() + (static_cast<size_t>(y) * belowSize.x + x) * 4;
					for (u32 channel = 0; channel < 4; ++channel) {
						texel[channel] = 0.25f * (fetch(x0, y0, channel) + fetch(x1, y0, channel) + fetch(x0, y1, channel) + fetch(x1, y1, channel));
					}
					if (meaning == TexelMeaning::Normal) {
						const f32 length = std::sqrt(texel[0] * texel[0] + texel[1] * texel[1] + texel[2] * texel[2]);
						if (length > 1e-6f) {
							texel[0] /= length;
							texel[1] /= length;
							texel[2] /= length;
						} else {
							texel[0] = 0.0f;
							texel[1] = 0.0f;
							texel[2] = 1.0f;
						}
					}
				}
			}
		}

		void Quantize(const Vector<f32>& level, TexelMeaning meaning, u8* out) {
			for (size_t i = 0; i < level.size(); i += 4) {
				for (size_t channel = 0; channel < 3; ++channel) {
					const f32 value = level[i + channel];
					switch (meaning) {
					case TexelMeaning::SRGBColor:
						out[i + channel] = ToByte(LinearToSRGB(value));
						break;
					case TexelMeaning::Linear:
						out[i + channel] = ToByte(value);
						break;
					case TexelMeaning::Normal:
						out[i + channel] = ToByte(value * 0.5f + 0.5f);
						break;
					}
				}
				out[i + 3] = ToByte(level[i + 3]);
			}
		}

		f32 HalfAt(const u8* halves, size_t index) {
			u16 half = 0;
			std::memcpy(&half, halves + index * sizeof(u16), sizeof(u16));
			return Math::HalfToFloat(half);
		}

		void BuildHalfChain(std::span<const u8> halves, Vec2I size, u32 mipLevels, Vector<u8>& out) {
			size_t total = 0;
			for (u32 mip = 0; mip < mipLevels; ++mip) {
				total += TexelBytes(LevelSize(size, mip)) * sizeof(u16);
			}
			out.resize(total);
			std::memcpy(out.data(), halves.data(), TexelBytes(size) * sizeof(u16));
			Vector<f32> below{GetMemoryResource(MemTag::Asset)};
			size_t aboveOffset = 0;
			size_t offset = TexelBytes(size) * sizeof(u16);
			for (u32 mip = 1; mip < mipLevels; ++mip) {
				const Vec2I aboveSize = LevelSize(size, mip - 1);
				const Vec2I belowSize = LevelSize(size, mip);
				const u8* above = out.data() + aboveOffset;
				Reduce([&](i32 x, i32 y, u32 channel) { return HalfAt(above, (static_cast<size_t>(y) * aboveSize.x + x) * 4 + channel); }, aboveSize,
					   TexelMeaning::Linear, below, belowSize);
				for (size_t i = 0; i < below.size(); ++i) {
					const u16 half = Math::FloatToHalf(below[i]);
					std::memcpy(out.data() + offset + i * sizeof(u16), &half, sizeof(u16));
				}
				aboveOffset = offset;
				offset += TexelBytes(belowSize) * sizeof(u16);
			}
		}

		struct LevelJob {
			const u8* texels = nullptr;
			Vec2I size{};
			RHI::Format format = RHI::Format::Unknown;
			u8* blocks = nullptr;
			const bc7e_scalar::bc7e_compress_block_params* bc7 = nullptr;
		};

		void Gather(const LevelJob& job, u32 blockX, u32 blockY, u8 out[64]) {
			for (u32 y = 0; y < 4; ++y) {
				const i32 sourceY = std::min(static_cast<i32>(blockY * 4 + y), job.size.y - 1);
				for (u32 x = 0; x < 4; ++x) {
					const i32 sourceX = std::min(static_cast<i32>(blockX * 4 + x), job.size.x - 1);
					std::memcpy(out + (y * 4 + x) * 4, job.texels + (static_cast<size_t>(sourceY) * job.size.x + sourceX) * 4, 4);
				}
			}
		}

		u16 Representable(u16 half) {
			if (half & 0x8000) {
				return 0;
			}
			if ((half & 0x7C00) == 0x7C00) {
				return (half & 0x03FF) != 0 ? 0 : 0x7BFF;
			}
			return half;
		}

		void GatherHalves(const LevelJob& job, u32 blockX, u32 blockY, u16 out[48]) {
			for (u32 y = 0; y < 4; ++y) {
				const i32 sourceY = std::min(static_cast<i32>(blockY * 4 + y), job.size.y - 1);
				for (u32 x = 0; x < 4; ++x) {
					const i32 sourceX = std::min(static_cast<i32>(blockX * 4 + x), job.size.x - 1);
					const u8* texel = job.texels + (static_cast<size_t>(sourceY) * job.size.x + sourceX) * 4 * sizeof(u16);
					for (u32 channel = 0; channel < 3; ++channel) {
						u16 half = 0;
						std::memcpy(&half, texel + channel * sizeof(u16), sizeof(u16));
						out[(y * 4 + x) * 3 + channel] = Representable(half);
					}
				}
			}
		}

		void EncodeRows(const LevelJob& job, u32 firstRow, u32 endRow) {
			const u32 blocksX = (static_cast<u32>(job.size.x) + 3) / 4;
			alignas(8) u8 texels[64];
			u16 halves[48];
			for (u32 blockY = firstRow; blockY < endRow; ++blockY) {
				for (u32 blockX = 0; blockX < blocksX; ++blockX) {
					u8* block = job.blocks + (static_cast<size_t>(blockY) * blocksX + blockX) * 16;
					if (job.format == RHI::Format::BC6HUFloat) {
						GatherHalves(job, blockX, blockY, halves);
						CompressBlockBC6(halves, 4 * 3, block, g_bc6hOptions);
						continue;
					}
					Gather(job, blockX, blockY, texels);
					if (job.format == RHI::Format::BC5Unorm) {
						rgbcx::encode_bc5_hq(block, texels, 0, 1, 4);
					} else {
						u64 encoded[2];
						bc7e_scalar::bc7e_compress_blocks(1, encoded, reinterpret_cast<const uint32_t*>(texels), job.bc7);
						std::memcpy(block, encoded, 16);
					}
				}
			}
		}

		void EncodeLevel(const LevelJob& job) {
			const u32 blocksX = (static_cast<u32>(job.size.x) + 3) / 4;
			const u32 blocksY = (static_cast<u32>(job.size.y) + 3) / 4;
			const u32 workers = JobSystem::GetWorkerCount();
			if (workers == 0 || blocksX * blocksY < 64) {
				EncodeRows(job, 0, blocksY);
				return;
			}
			const u32 chunks = std::min(blocksY, workers * 4);
			Vector<JobHandle> handles{GetMemoryResource(MemTag::Asset)};
			handles.reserve(chunks);
			const LevelJob* shared = &job;
			for (u32 chunk = 0; chunk < chunks; ++chunk) {
				const u32 first = blocksY * chunk / chunks;
				const u32 end = blocksY * (chunk + 1) / chunks;
				handles.push_back(JobSystem::Submit([shared, first, end]() { EncodeRows(*shared, first, end); }));
			}
			JobSystem::WaitForAll({handles.data(), handles.size()});
		}

		bool IsOurs(RHI::Format format) {
			return format == RHI::Format::BC5Unorm || format == RHI::Format::BC7Unorm || format == RHI::Format::BC7SRGB ||
				   format == RHI::Format::BC6HUFloat;
		}

		bool Encode(RHI::Format format, const TextureEncodeInput& input, Vector<u8>& out) {
			if (!IsOurs(format) || input.texelFormat != InputFormat(format) || input.size.x <= 0 || input.size.y <= 0 || input.slices == 0 ||
				input.mipLevels == 0) {
				return false;
			}
			const bool hdr = input.texelFormat == RHI::Format::RGBA16Float;
			if ((hdr && input.meaning != TexelMeaning::Linear) || (input.suppliedLevels != 1 && input.suppliedLevels != input.mipLevels)) {
				return false;
			}
			if (format == RHI::Format::BC6HUFloat && !g_bc6hOptions) {
				return false;
			}
			const size_t texelScale = hdr ? sizeof(u16) : 1;
			size_t supplied = 0;
			for (u32 mip = 0; mip < input.suppliedLevels; ++mip) {
				supplied += TexelBytes(LevelSize(input.size, mip)) * texelScale * input.slices;
			}
			if (input.texels.size() != supplied) {
				return false;
			}
			u64 total = 0;
			for (u32 mip = 0; mip < input.mipLevels; ++mip) {
				total += RHI::FormatLevelBytes(format, LevelSize(input.size, mip)) * input.slices;
			}
			out.assign(static_cast<size_t>(total), 0);

			bc7e_scalar::bc7e_compress_block_params bc7;
			bc7e_scalar::bc7e_compress_block_params_init_slow(&bc7, false);

			const bool chainSupplied = input.suppliedLevels == input.mipLevels && input.mipLevels > 1;
			const size_t sliceBytes = TexelBytes(input.size) * texelScale;
			Vector<u8> chain{GetMemoryResource(MemTag::Asset)};
			for (u32 slice = 0; slice < input.slices; ++slice) {
				if (!chainSupplied) {
					const std::span<const u8> top = input.texels.subspan(slice * sliceBytes, sliceBytes);
					if (hdr) {
						BuildHalfChain(top, input.size, input.mipLevels, chain);
					} else {
						TextureCompressionOps::BuildChain(top, input.size, input.mipLevels, input.meaning, chain);
					}
				}
				size_t chainOffset = 0;
				size_t suppliedOffset = 0;
				u64 levelOffset = 0;
				for (u32 mip = 0; mip < input.mipLevels; ++mip) {
					const Vec2I levelSize = LevelSize(input.size, mip);
					const u64 levelBytes = RHI::FormatLevelBytes(format, levelSize);
					const size_t levelTexelBytes = TexelBytes(levelSize) * texelScale;
					LevelJob job;
					job.texels = chainSupplied ? input.texels.data() + suppliedOffset + slice * levelTexelBytes : chain.data() + chainOffset;
					job.size = levelSize;
					job.format = format;
					job.blocks = out.data() + levelOffset + slice * levelBytes;
					job.bc7 = &bc7;
					EncodeLevel(job);
					chainOffset += levelTexelBytes;
					suppliedOffset += levelTexelBytes * input.slices;
					levelOffset += levelBytes * input.slices;
				}
			}
			return true;
		}

		bool Decode(RHI::Format format, std::span<const u8> blocks, Vec2I size, std::span<u8> texels) {
			const RHI::Format decoded = TextureCodecs::DecodedFormat(format);
			if (!IsOurs(format) || size.x <= 0 || size.y <= 0 || blocks.size() < RHI::FormatLevelBytes(format, size) ||
				texels.size() < RHI::FormatLevelBytes(decoded, size)) {
				return false;
			}
			const size_t texelBytes = decoded == RHI::Format::RGBA16Float ? 4 * sizeof(u16) : 4;
			const u32 blocksX = (static_cast<u32>(size.x) + 3) / 4;
			const u32 blocksY = (static_cast<u32>(size.y) + 3) / 4;
			u16 decodedBlock[16 * 4];
			u8* blockTexels = reinterpret_cast<u8*>(decodedBlock);
			for (u32 blockY = 0; blockY < blocksY; ++blockY) {
				for (u32 blockX = 0; blockX < blocksX; ++blockX) {
					const u8* block = blocks.data() + (static_cast<size_t>(blockY) * blocksX + blockX) * 16;
					if (format == RHI::Format::BC6HUFloat) {
						BC6H::DecodeBlock(block, decodedBlock);
					} else if (format == RHI::Format::BC5Unorm) {
						for (u32 texel = 0; texel < 16; ++texel) {
							blockTexels[texel * 4 + 2] = 0;
							blockTexels[texel * 4 + 3] = 255;
						}
						rgbcx::unpack_bc5(block, blockTexels, 0, 1, 4);
					} else {
						bc7decomp::unpack_bc7(block, reinterpret_cast<bc7decomp::color_rgba*>(blockTexels));
					}
					for (u32 y = 0; y < 4 && blockY * 4 + y < static_cast<u32>(size.y); ++y) {
						const u32 width = std::min(4u, static_cast<u32>(size.x) - blockX * 4);
						std::memcpy(texels.data() + ((static_cast<size_t>(blockY) * 4 + y) * size.x + blockX * 4) * texelBytes,
									blockTexels + y * 4 * texelBytes, width * texelBytes);
					}
				}
			}
			return true;
		}

		const char* FormatLabel(RHI::Format format) {
			switch (format) {
			case RHI::Format::BC5Unorm:
				return "BC5";
			case RHI::Format::BC7SRGB:
				return "BC7 sRGB";
			case RHI::Format::BC6HUFloat:
				return "BC6H";
			default:
				return "BC7";
			}
		}

	} // anonymous namespace

	void TextureCompressionOps::BuildChain(std::span<const u8> rgba8, Vec2I size, u32 mipLevels, TexelMeaning meaning, Vector<u8>& out) {
		size_t total = 0;
		for (u32 mip = 0; mip < mipLevels; ++mip) {
			total += TexelBytes(LevelSize(size, mip));
		}
		out.resize(total);
		std::memcpy(out.data(), rgba8.data(), TexelBytes(size));

		Vector<f32> above{GetMemoryResource(MemTag::Asset)};
		Vector<f32> below{GetMemoryResource(MemTag::Asset)};
		size_t offset = TexelBytes(size);
		for (u32 mip = 1; mip < mipLevels; ++mip) {
			const Vec2I aboveSize = LevelSize(size, mip - 1);
			const Vec2I belowSize = LevelSize(size, mip);
			if (mip == 1) {
				Reduce([&](i32 x, i32 y, u32 channel) { return Expand(rgba8[(static_cast<size_t>(y) * aboveSize.x + x) * 4 + channel], channel, meaning); },
					   aboveSize, meaning, below, belowSize);
			} else {
				Reduce([&](i32 x, i32 y, u32 channel) { return above[(static_cast<size_t>(y) * aboveSize.x + x) * 4 + channel]; }, aboveSize, meaning, below,
					   belowSize);
			}
			Quantize(below, meaning, out.data() + offset);
			offset += TexelBytes(belowSize);
			std::swap(above, below);
		}
	}

	void RegisterTextureCodecs(const void* owner) {
		// before any worker encodes
		bc7e_scalar::bc7e_compress_block_init();
		rgbcx::init();
		if (!g_bc6hOptions) {
			CreateOptionsBC6(&g_bc6hOptions);
			SetQualityBC6(g_bc6hOptions, BC6H_QUALITY);
		}

		const TextureCodec codec{&Encode, &Decode};
		for (RHI::Format format : {RHI::Format::BC7SRGB, RHI::Format::BC7Unorm, RHI::Format::BC5Unorm, RHI::Format::BC6HUFloat}) {
			if (!TextureCodecs::Register(format, codec, owner)) {
				SLT_LOG_WARN("Asset", "couldn't register the import pipeline's {} codec (another one has the format)", FormatLabel(format));
			}
		}
	}

	void ShutdownTextureCodecs() {
		if (g_bc6hOptions) {
			DestroyOptionsBC6(g_bc6hOptions);
			g_bc6hOptions = nullptr;
		}
	}

} // namespace Shalltear
