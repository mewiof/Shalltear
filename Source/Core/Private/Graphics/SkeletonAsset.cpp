#include "Shalltear/Core/Graphics/SkeletonAsset.h"

#include "Shalltear/Core/Asset/PakFormat.h"

namespace Shalltear {

	static_assert(sizeof(BoneTransform) == 48, "Quat's 16-byte alignment pads this. The cook writes scalars, never the struct");

	SkeletonAsset::~SkeletonAsset() = default;

	i32 SkeletonAsset::FindBone(std::string_view name) const {
		for (size_t i = 0; i < boneNames.size(); ++i) {
			if (boneNames[i] == name) {
				return static_cast<i32>(i);
			}
		}
		return -1;
	}

	bool SkeletonAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		(void)payload;

		descriptor.Write(static_cast<u32>(parents.size()));
		for (size_t i = 0; i < parents.size(); ++i) {
			descriptor.WriteString(i < boneNames.size() ? std::string_view(boneNames[i]) : std::string_view());
			descriptor.Write(parents[i]);

			const BoneTransform rest = i < restPose.size() ? restPose[i] : BoneTransform{};
			descriptor.Write(rest.position.x);
			descriptor.Write(rest.position.y);
			descriptor.Write(rest.position.z);
			descriptor.Write(rest.rotation.x);
			descriptor.Write(rest.rotation.y);
			descriptor.Write(rest.rotation.z);
			descriptor.Write(rest.rotation.w);
			descriptor.Write(rest.scale.x);
			descriptor.Write(rest.scale.y);
			descriptor.Write(rest.scale.z);
		}
		return true;
	}

	bool SkeletonAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		(void)payload;

		u32 boneCount = 0;
		descriptor.Read(boneCount);

		constexpr size_t BYTES_PER_BONE = sizeof(u32) + sizeof(i16) + 10 * sizeof(f32);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(boneCount) * BYTES_PER_BONE) {
			return false;
		}

		Vector<str> names{GetMemoryResource(MemTag::Asset)};
		Vector<i16> readParents{GetMemoryResource(MemTag::Asset)};
		Vector<BoneTransform> rest{GetMemoryResource(MemTag::Asset)};
		names.reserve(boneCount);
		readParents.reserve(boneCount);
		rest.reserve(boneCount);

		for (u32 i = 0; i < boneCount; ++i) {
			names.emplace_back(descriptor.ReadString());
			i16 parent = -1;
			descriptor.Read(parent);
			readParents.push_back(parent);

			BoneTransform bone;
			descriptor.Read(bone.position.x);
			descriptor.Read(bone.position.y);
			descriptor.Read(bone.position.z);
			descriptor.Read(bone.rotation.x);
			descriptor.Read(bone.rotation.y);
			descriptor.Read(bone.rotation.z);
			descriptor.Read(bone.rotation.w);
			descriptor.Read(bone.scale.x);
			descriptor.Read(bone.scale.y);
			descriptor.Read(bone.scale.z);
			rest.push_back(bone);
		}
		if (descriptor.Failed()) {
			return false;
		}

		boneNames = std::move(names);
		parents = std::move(readParents);
		restPose = std::move(rest);
		return true;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::SkeletonAsset)
