#include "TestsPCH.h"

#include "Shalltear/Core/Graphics/TextureCodecs.h"

using namespace Shalltear;

namespace {

	bool EncodeA(RHI::Format, const TextureEncodeInput&, Vector<u8>&) { return true; }
	bool EncodeB(RHI::Format, const TextureEncodeInput&, Vector<u8>&) { return false; }
	bool Decode(RHI::Format, std::span<const u8>, Vec2I, std::span<u8>) { return true; }

	const TextureCodec CodecA{&EncodeA, &Decode};
	const TextureCodec CodecB{&EncodeB, &Decode};

	const int OWNER_ONE = 1;
	const int OWNER_TWO = 2;

} // anonymous namespace

TEST(TextureCodecsTest, FormatTakesOneCodecAndFirstStays) {
	TextureCodecRegistry registry;
	EXPECT_EQ(registry.Find(RHI::Format::BC7SRGB), nullptr);
	EXPECT_TRUE(registry.Register(RHI::Format::BC7SRGB, CodecA, &OWNER_ONE));
	EXPECT_FALSE(registry.Register(RHI::Format::BC7SRGB, CodecB, &OWNER_TWO));
	const TextureCodec* found = registry.Find(RHI::Format::BC7SRGB);
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->encode, &EncodeA);
	EXPECT_FALSE(registry.Register(RHI::Format::BC7SRGB, CodecB, &OWNER_ONE));
	EXPECT_EQ(registry.Find(RHI::Format::BC7SRGB)->encode, &EncodeA);
}

TEST(TextureCodecsTest, SweepTakesOwnersCodecs) {
	TextureCodecRegistry registry;
	ASSERT_TRUE(registry.Register(RHI::Format::BC7Unorm, CodecA, &OWNER_ONE));
	ASSERT_TRUE(registry.Register(RHI::Format::BC5Unorm, CodecA, &OWNER_ONE));
	ASSERT_TRUE(registry.Register(RHI::Format::BC7SRGB, CodecB, &OWNER_TWO));

	EXPECT_EQ(registry.UnregisterOwnedBy(nullptr), 0u);
	EXPECT_EQ(registry.UnregisterOwnedBy(&OWNER_ONE), 2u);
	EXPECT_EQ(registry.Find(RHI::Format::BC7Unorm), nullptr);
	EXPECT_EQ(registry.Find(RHI::Format::BC5Unorm), nullptr);
	ASSERT_NE(registry.Find(RHI::Format::BC7SRGB), nullptr);
	EXPECT_EQ(registry.Find(RHI::Format::BC7SRGB)->encode, &EncodeB);
	EXPECT_EQ(registry.UnregisterOwnedBy(&OWNER_ONE), 0u);

	EXPECT_TRUE(registry.Register(RHI::Format::BC7Unorm, CodecB, &OWNER_TWO));
	EXPECT_EQ(registry.UnregisterOwnedBy(&OWNER_TWO), 2u);
}

TEST(TextureCodecsTest, RegistryRefusesWhatCannotBeCodec) {
	TextureCodecRegistry registry;
	EXPECT_FALSE(registry.Register(RHI::Format::RGBA8SRGB, CodecA, &OWNER_ONE));
	EXPECT_FALSE(registry.Register(RHI::Format::Unknown, CodecA, &OWNER_ONE));
	EXPECT_FALSE(registry.Register(RHI::Format::BC5Unorm, TextureCodec{&EncodeA, nullptr}, &OWNER_ONE));
	EXPECT_FALSE(registry.Register(RHI::Format::BC5Unorm, TextureCodec{nullptr, &Decode}, &OWNER_ONE));
	EXPECT_EQ(registry.Find(RHI::Format::BC5Unorm), nullptr);
	EXPECT_EQ(registry.Find(RHI::Format::Count), nullptr);
	EXPECT_EQ(registry.Find(static_cast<RHI::Format>(0xFF)), nullptr);
	EXPECT_TRUE(registry.Register(RHI::Format::BC5Unorm, CodecA, &OWNER_ONE));
}
