#include "TestsPCH.h"

#include "Shalltear/Core/Math/MathUtil.h"

#ifndef SHALLTEAR_SHARED
#include "Shalltear/Core/Graphics/SkeletonAsset.h"

#include "RetargetPoseOps.h"
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED

namespace {

	struct PoseBone {
		const char* name;
		i16 parent;
		Vec3 bind;
		Quat bindRotation = Quat::Identity();
		Vec3 rest;
		Quat restRotation = Quat::Identity();
		bool bound = true;
	};

	class BindPoseFixture : public ::testing::Test {
	protected:
		SkeletonAsset* _skeleton = nullptr;
		Vector<i16> _jointToBone{GetMemoryResource(MemTag::Asset)};
		Vector<Mat4> _inverseBind{GetMemoryResource(MemTag::Asset)};

		void TearDown() override {
			if (_skeleton) {
				Delete(_skeleton);
				_skeleton = nullptr;
			}
		}

		void MakeRig(std::initializer_list<PoseBone> bones, const Mat4& modelToMesh = Mat4::Identity()) {
			_skeleton = New<SkeletonAsset>(MemTag::Asset);
			_jointToBone.clear();
			_inverseBind.clear();

			std::vector<Mat4> bindModel;
			i16 index = 0;
			for (const PoseBone& bone : bones) {
				_skeleton->boneNames.push_back(bone.name);
				_skeleton->parents.push_back(bone.parent);
				BoneTransform rest;
				rest.position = bone.rest;
				rest.rotation = bone.restRotation;
				_skeleton->restPose.push_back(rest);

				const Mat4 local = Mat4::TRS(bone.bind, bone.bindRotation, Vec3::One());
				bindModel.push_back(bone.parent >= 0 ? bindModel[static_cast<size_t>(bone.parent)] * local : local);
				if (bone.bound) {
					_jointToBone.push_back(index);
					_inverseBind.push_back((modelToMesh * bindModel.back()).Inverse());
				}
				++index;
			}
		}

		bool Sample(const Mat4& meshToModel, Vector<BoneTransform>& out) {
			return RetargetPoseOps::SampleBindPose(
				*_skeleton, std::span<const i16>(_jointToBone.data(), _jointToBone.size()),
				std::span<const Mat4>(_inverseBind.data(), _inverseBind.size()), meshToModel, out);
		}
	};

	Vec3 PositionOf(const Vector<BoneTransform>& pose, size_t bone) {
		return bone < pose.size() ? pose[bone].position : Vec3(9999.0f, 9999.0f, 9999.0f);
	}

} // anonymous namespace

TEST_F(BindPoseFixture, MidTakeRigSamplesBindStance) {
	MakeRig({
		{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.24f, 0.0f), Quat::Identity()},
		{"Spine", 0, Vec3(0.0f, 0.15f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.15f, 0.0f),
		 Quat::AngleAxis(80.0f, Vec3::Right())},
	});

	Vector<BoneTransform> pose{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(Sample(Mat4::Identity(), pose));
	ASSERT_EQ(pose.size(), 2u);

	EXPECT_NEAR(PositionOf(pose, 0).y, 1.0f, 1e-4f);
	EXPECT_LT(Quat::Angle(pose[1].rotation, Quat::Identity()), 0.5f);
}

TEST_F(BindPoseFixture, BoundRigSamplesRestPose) {
	MakeRig({
		{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity(), Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
		{"Spine", 0, Vec3(0.0f, 0.15f, 0.0f), Quat::AngleAxis(12.0f, Vec3::Right()), Vec3(0.0f, 0.15f, 0.0f),
		 Quat::AngleAxis(12.0f, Vec3::Right())},
		{"Head", 1, Vec3(0.0f, 0.4f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.4f, 0.0f), Quat::Identity()},
	});

	Vector<BoneTransform> pose{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(Sample(Mat4::Identity(), pose));
	ASSERT_EQ(pose.size(), 3u);
	for (size_t bone = 0; bone < pose.size(); ++bone) {
		EXPECT_TRUE(Vec3::Approximately(pose[bone].position, _skeleton->restPose[bone].position, 1e-4f)) << "bone " << bone;
		EXPECT_LT(Quat::Angle(pose[bone].rotation, _skeleton->restPose[bone].rotation), 0.05f) << "bone " << bone;
	}
}

TEST_F(BindPoseFixture, BoundParentIgnoresHostFrame) {
	MakeRig({
		{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.24f, 0.0f), Quat::Identity()},
		{"Spine", 0, Vec3(0.0f, 0.15f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.15f, 0.0f), Quat::Identity()},
	});

	Vector<BoneTransform> honest{GetMemoryResource(MemTag::Asset)};
	Vector<BoneTransform> wrong{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(Sample(Mat4::Identity(), honest));
	ASSERT_TRUE(Sample(Mat4::TRS(Vec3(0.0f, 1.0f, 0.0f), Quat::AngleAxis(90.0f, Vec3::Up()), Vec3::One()), wrong));

	EXPECT_TRUE(Vec3::Approximately(PositionOf(wrong, 1), PositionOf(honest, 1), 1e-4f));
	EXPECT_FALSE(Vec3::Approximately(PositionOf(wrong, 0), PositionOf(honest, 0), 1e-3f));
}

TEST_F(BindPoseFixture, RootLiftedIntoSkeletonFrame) {
	const Mat4 meshToModel = Mat4::TRS(Vec3(0.0f, -1.0f, 0.0f), Quat::AngleAxis(90.0f, Vec3::Up()), Vec3::One());
	Mat4 modelToMesh;
	ASSERT_TRUE(meshToModel.TryInvert(modelToMesh));
	MakeRig({{"Hips", -1, Vec3(0.5f, 2.0f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.0f, 0.0f), Quat::Identity()}},
			modelToMesh);

	Vector<BoneTransform> pose{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(Sample(meshToModel, pose));
	EXPECT_TRUE(Vec3::Approximately(PositionOf(pose, 0), Vec3(0.5f, 2.0f, 0.0f), 1e-4f));
}

TEST_F(BindPoseFixture, BoneLoweredThroughParentRest) {
	MakeRig({
		{"Armature", -1, Vec3(), Quat::Identity(), Vec3(0.0f, 0.5f, 0.0f), Quat::Identity(), false},
		{"Hips", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.24f, 0.0f), Quat::Identity()},
	});

	Vector<BoneTransform> pose{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(Sample(Mat4::Identity(), pose));
	ASSERT_EQ(pose.size(), 2u);

	EXPECT_TRUE(Vec3::Approximately(PositionOf(pose, 0), Vec3(0.0f, 0.5f, 0.0f), 1e-4f));
	EXPECT_NEAR(PositionOf(pose, 1).y, 0.5f, 1e-4f);
}

TEST_F(BindPoseFixture, UnboundRigRefused) {
	MakeRig({
		{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.24f, 0.0f), Quat::Identity(), false},
		{"Spine", 0, Vec3(0.0f, 0.15f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.15f, 0.0f), Quat::Identity(), false},
	});
	ASSERT_TRUE(_jointToBone.empty());

	Vector<BoneTransform> pose{GetMemoryResource(MemTag::Asset)};
	pose.push_back(BoneTransform{});
	EXPECT_FALSE(Sample(Mat4::Identity(), pose));
	EXPECT_EQ(pose.size(), 1u);
}

TEST_F(BindPoseFixture, ForeignJointSkipped) {
	MakeRig({
		{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.24f, 0.0f), Quat::Identity()},
		{"Spine", 0, Vec3(0.0f, 0.15f, 0.0f), Quat::Identity(), Vec3(0.0f, 0.15f, 0.0f), Quat::Identity()},
	});
	_jointToBone[1] = -1;
	_jointToBone.push_back(97);
	_inverseBind.push_back(Mat4::Identity());

	Vector<BoneTransform> pose{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(Sample(Mat4::Identity(), pose));
	ASSERT_EQ(pose.size(), 2u);
	EXPECT_NEAR(PositionOf(pose, 0).y, 1.0f, 1e-4f);
	EXPECT_NEAR(PositionOf(pose, 1).y, 0.15f, 1e-4f);
}

namespace {

	SkeletonAsset* MakeNamedRig(std::initializer_list<std::pair<const char*, f32>> bones) {
		auto* rig = New<SkeletonAsset>(MemTag::Asset);
		i16 index = 0;
		for (const auto& [name, height] : bones) {
			rig->boneNames.push_back(name);
			rig->parents.push_back(index == 0 ? static_cast<i16>(-1) : static_cast<i16>(0));
			BoneTransform rest;
			rest.position = Vec3(0.0f, height, 0.0f);
			rig->restPose.push_back(rest);
			++index;
		}
		return rig;
	}

	std::span<const BoneTransform> RestOf(const SkeletonAsset& rig) {
		return std::span<const BoneTransform>(rig.restPose.data(), rig.restPose.size());
	}

} // anonymous namespace

TEST(CopyReferencePoseTest, StanceCopiedByBoneName) {
	SkeletonAsset* source = MakeNamedRig({{"Hips", 1.0f}, {"Spine", 0.15f}});
	SkeletonAsset* target = MakeNamedRig({{"Hips", 0.24f}, {"Spine", 0.15f}});

	Vector<BoneTransform> pose{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(RetargetPoseOps::CopyReferencePoseByName(*source, RestOf(*source), *target, pose));
	ASSERT_EQ(pose.size(), 2u);
	EXPECT_NEAR(PositionOf(pose, 0).y, 1.0f, 1e-4f);
	Delete(source);
	Delete(target);
}

TEST(CopyReferencePoseTest, UnnamedBoneKeepsRest) {
	SkeletonAsset* source = MakeNamedRig({{"Hips", 1.0f}});
	SkeletonAsset* target = MakeNamedRig({{"Hips", 0.24f}, {"Producer Perspective", 7.5f}});

	Vector<BoneTransform> pose{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(RetargetPoseOps::CopyReferencePoseByName(*source, RestOf(*source), *target, pose));
	ASSERT_EQ(pose.size(), 2u);
	EXPECT_NEAR(PositionOf(pose, 0).y, 1.0f, 1e-4f);
	EXPECT_NEAR(PositionOf(pose, 1).y, 7.5f, 1e-4f);
	Delete(source);
	Delete(target);
}

TEST(CopyReferencePoseTest, UnrelatedSourceRefused) {
	SkeletonAsset* source = MakeNamedRig({{"Ugonchu_Hips", 1.0f}});
	SkeletonAsset* target = MakeNamedRig({{"Hips", 0.24f}});

	Vector<BoneTransform> pose{GetMemoryResource(MemTag::Asset)};
	pose.push_back(BoneTransform{});
	pose[0].position = Vec3(0.0f, 42.0f, 0.0f);
	EXPECT_FALSE(RetargetPoseOps::CopyReferencePoseByName(*source, RestOf(*source), *target, pose));
	ASSERT_EQ(pose.size(), 1u);
	EXPECT_NEAR(PositionOf(pose, 0).y, 42.0f, 1e-4f);
	Delete(source);
	Delete(target);
}

TEST(CopyReferencePoseTest, StatedPoseCopied) {
	SkeletonAsset* source = MakeNamedRig({{"Hips", 0.24f}});
	SkeletonAsset* target = MakeNamedRig({{"Hips", 9.0f}});

	Vector<BoneTransform> corrected{GetMemoryResource(MemTag::Asset)};
	corrected.push_back(BoneTransform{});
	corrected[0].position = Vec3(0.0f, 1.5f, 0.0f);

	Vector<BoneTransform> pose{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(RetargetPoseOps::CopyReferencePoseByName(
		*source, std::span<const BoneTransform>(corrected.data(), corrected.size()), *target, pose));
	ASSERT_EQ(pose.size(), 1u);
	EXPECT_NEAR(PositionOf(pose, 0).y, 1.5f, 1e-4f);
	Delete(source);
	Delete(target);
}

#endif
