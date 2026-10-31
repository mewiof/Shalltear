#include "TestsPCH.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetCues.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

using namespace Shalltear;

namespace {

	class CueBaseAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(CueBaseAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_END_REFLECT()

		StringHash GetAssetTypeId() const override { return CueBaseAsset::GetReflectType()->name; }
	};

	class CueDerivedAsset : public CueBaseAsset {
	public:
		SLT_REFLECT_CLASS(CueDerivedAsset)
		SLT_REFLECT_PARENT(CueBaseAsset)
		SLT_END_REFLECT()

		StringHash GetAssetTypeId() const override { return CueDerivedAsset::GetReflectType()->name; }
	};

	class CueOtherAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(CueOtherAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_END_REFLECT()

		StringHash GetAssetTypeId() const override { return CueOtherAsset::GetReflectType()->name; }
	};

	struct Heard {
		const char* by = nullptr;
		u64 asset = 0;
		f32 volume = 0.0f;
		u32 calls = 0;
	};
	Heard _heard;

	bool HearBase(const AssetCue& cue) {
		_heard = Heard{"base", cue.asset.GetInstanceId(), cue.volume, _heard.calls + 1};
		return true;
	}

	bool HearDerived(const AssetCue& cue) {
		_heard = Heard{"derived", cue.asset.GetInstanceId(), cue.volume, _heard.calls + 1};
		return true;
	}

	bool Refuse(const AssetCue&) { return false; }

	class AssetCuesFixture : public ::testing::Test {
	protected:
		const char _owner = 0;
		const char _otherOwner = 0;

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			CueBaseAsset::GetReflectType();
			CueDerivedAsset::GetReflectType();
			CueOtherAsset::GetReflectType();
			_heard = Heard{};
		}

		void TearDown() override {
			AssetCues::UnregisterOwnedBy(&_owner);
			AssetCues::UnregisterOwnedBy(&_otherOwner);
		}

		static AssetCue CueOf(Asset& asset, f32 volume) {
			AssetCue cue;
			cue.asset = Handle<Asset>(&asset);
			cue.volume = volume;
			return cue;
		}
	};

} // namespace

TEST_F(AssetCuesFixture, CueReachesNearestTypedHandler) {
	CueBaseAsset base;
	CueDerivedAsset derived;
	CueOtherAsset other;
	EXPECT_FALSE(AssetCues::Cue(CueOf(derived, 1.0f)));
	EXPECT_FALSE(AssetCues::CanCue(CueDerivedAsset::GetReflectType()));

	ASSERT_TRUE(AssetCues::Register(CueBaseAsset::GetReflectType(), &HearBase, &_owner));
	ASSERT_TRUE(AssetCues::Register(CueDerivedAsset::GetReflectType(), &HearDerived, &_otherOwner));

	EXPECT_TRUE(AssetCues::Cue(CueOf(derived, 0.25f)));
	EXPECT_STREQ(_heard.by, "derived");
	EXPECT_EQ(_heard.asset, derived.GetInstanceId());
	EXPECT_FLOAT_EQ(_heard.volume, 0.25f);

	EXPECT_TRUE(AssetCues::Cue(CueOf(base, 1.0f)));
	EXPECT_STREQ(_heard.by, "base");

	const u32 calls = _heard.calls;
	EXPECT_FALSE(AssetCues::Cue(CueOf(other, 1.0f)));
	EXPECT_EQ(_heard.calls, calls);
	EXPECT_TRUE(AssetCues::CanCue(CueDerivedAsset::GetReflectType()));
	EXPECT_TRUE(AssetCues::CanCue(CueBaseAsset::GetReflectType()));
	EXPECT_FALSE(AssetCues::CanCue(CueOtherAsset::GetReflectType()));
	EXPECT_FALSE(AssetCues::CanCue(nullptr));

	AssetCue empty;
	EXPECT_FALSE(AssetCues::Cue(empty));
}

TEST_F(AssetCuesFixture, SweepTakesOwnersHandlersOnly) {
	CueDerivedAsset derived;
	ASSERT_TRUE(AssetCues::Register(CueBaseAsset::GetReflectType(), &HearBase, &_owner));
	ASSERT_TRUE(AssetCues::Register(CueDerivedAsset::GetReflectType(), &HearDerived, &_otherOwner));

	EXPECT_EQ(AssetCues::UnregisterOwnedBy(&_otherOwner), 1u);
	EXPECT_EQ(AssetCues::UnregisterOwnedBy(nullptr), 0u);
	EXPECT_TRUE(AssetCues::Cue(CueOf(derived, 1.0f)));
	EXPECT_STREQ(_heard.by, "base");

	ASSERT_TRUE(AssetCues::Register(CueBaseAsset::GetReflectType(), &Refuse, &_owner));
	EXPECT_FALSE(AssetCues::Cue(CueOf(derived, 1.0f)));
	EXPECT_EQ(AssetCues::UnregisterOwnedBy(&_owner), 1u);
	EXPECT_FALSE(AssetCues::CanCue(CueDerivedAsset::GetReflectType()));

	EXPECT_FALSE(AssetCues::Register(nullptr, &HearBase, &_owner));
	EXPECT_FALSE(AssetCues::Register(CueBaseAsset::GetReflectType(), nullptr, &_owner));
}
