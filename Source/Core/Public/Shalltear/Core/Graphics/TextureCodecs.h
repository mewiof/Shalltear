#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	enum class TexelMeaning : u8 {
		SRGBColor = 0,
		Linear,
		Normal
	};

	struct TextureEncodeInput {
		std::span<const u8> texels;
		// only BC6H takes RGBA16Float
		RHI::Format texelFormat = RHI::Format::RGBA8Unorm;
		Vec2I size{};
		u32 slices = 1;
		u32 mipLevels = 1;
		u32 suppliedLevels = 1;
		TexelMeaning meaning = TexelMeaning::SRGBColor;
	};

	struct TextureCodec {
		bool (*encode)(RHI::Format format, const TextureEncodeInput& input, Vector<u8>& out) = nullptr;
		bool (*decode)(RHI::Format format, std::span<const u8> blocks, Vec2I size, std::span<u8> texels) = nullptr;
	};

	class SHALLTEAR_CORE_API TextureCodecRegistry {
	public:
		bool Register(RHI::Format format, const TextureCodec& codec, const void* owner);
		u32 UnregisterOwnedBy(const void* owner);
		const TextureCodec* Find(RHI::Format format) const;
	private:
		struct Entry {
			TextureCodec codec;
			const void* owner = nullptr;
			bool used = false;
		};
		Entry _entries[static_cast<size_t>(RHI::Format::Count)] = {};
	};

	namespace TextureCodecs {

		SHALLTEAR_CORE_API bool Register(RHI::Format format, const TextureCodec& codec, const void* owner);
		SHALLTEAR_CORE_API u32 UnregisterOwnedBy(const void* owner);
		SHALLTEAR_CORE_API const TextureCodec* Find(RHI::Format format);

		inline RHI::Format DecodedFormat(RHI::Format format) {
			switch (format) {
			case RHI::Format::BC5Unorm:
			case RHI::Format::BC7Unorm:
				return RHI::Format::RGBA8Unorm;
			case RHI::Format::BC7SRGB:
				return RHI::Format::RGBA8SRGB;
			case RHI::Format::BC6HUFloat:
				return RHI::Format::RGBA16Float;
			default:
				return RHI::Format::Unknown;
			}
		}

	} // namespace TextureCodecs

} // namespace Shalltear
