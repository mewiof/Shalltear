#include "Shalltear/Core/Graphics/TextureCodecs.h"

namespace Shalltear {

	bool TextureCodecRegistry::Register(RHI::Format format, const TextureCodec& codec, const void* owner) {
		if (!RHI::IsBlockCompressed(format) || !codec.encode || !codec.decode) {
			return false;
		}
		Entry& entry = _entries[static_cast<size_t>(format)];
		if (entry.used) {
			return false;
		}
		entry = Entry{codec, owner, true};
		return true;
	}

	u32 TextureCodecRegistry::UnregisterOwnedBy(const void* owner) {
		if (!owner) {
			return 0;
		}
		u32 removed = 0;
		for (Entry& entry : _entries) {
			if (entry.used && entry.owner == owner) {
				entry = Entry{};
				++removed;
			}
		}
		return removed;
	}

	const TextureCodec* TextureCodecRegistry::Find(RHI::Format format) const {
		const size_t index = static_cast<size_t>(format);
		if (index >= static_cast<size_t>(RHI::Format::Count) || !_entries[index].used) {
			return nullptr;
		}
		return &_entries[index].codec;
	}

	namespace {
		TextureCodecRegistry _registry;
	} // anonymous namespace

	namespace TextureCodecs {

		bool Register(RHI::Format format, const TextureCodec& codec, const void* owner) {
			return _registry.Register(format, codec, owner);
		}

		u32 UnregisterOwnedBy(const void* owner) {
			return _registry.UnregisterOwnedBy(owner);
		}

		const TextureCodec* Find(RHI::Format format) {
			return _registry.Find(format);
		}

	} // namespace TextureCodecs

} // namespace Shalltear
