#include "Shalltear/Animation/RetargetProfile.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Log.h"

namespace Shalltear {

	RetargetProfile::~RetargetProfile() = default;

	const RetargetProfile::Chain* RetargetProfile::FindChain(std::string_view role) const {
		for (const Chain& chain : chains) {
			if (chain.role == role) {
				return &chain;
			}
		}
		return nullptr;
	}

	std::span<const BoneTransform> RetargetProfile::GetReferencePose() const {
		if (!referencePose.empty()) {
			return std::span<const BoneTransform>(referencePose.data(), referencePose.size());
		}
		const SkeletonAsset* rig = skeleton.Get();
		if (!rig) {
			return {};
		}
		return std::span<const BoneTransform>(rig->restPose.data(), rig->restPose.size());
	}

	void RetargetProfile::OnLoad() {
		if (!skeletonGuid.IsValid()) {
			SLT_LOG_WARN("Animation", "RetargetProfile '{}' has no skeleton", virtualPath);
			return;
		}
		Asset* resolved = AssetDatabase::LoadAssetByGuid(skeletonGuid);
		if (!resolved) {
			SLT_LOG_WARN("Animation", "RetargetProfile '{}': skeleton {} is unresolvable", virtualPath, skeletonGuid.ToString());
			return;
		}
		skeleton = Handle<SkeletonAsset>(static_cast<SkeletonAsset*>(resolved));
	}

	void RetargetProfile::OnUnload() {
		if (SkeletonAsset* resolved = skeleton.Get()) {
			AssetDatabase::UnloadAsset(resolved);
		}
		skeleton = {};
	}

	bool RetargetProfile::Cook(CookWriter& descriptor, CookWriter& payload) const {
		descriptor.WriteAssetRef(skeletonGuid);
		descriptor.Write(static_cast<u32>(chains.size()));
		for (const Chain& chain : chains) {
			descriptor.WriteString(chain.role);
			descriptor.WriteString(chain.mirror);
			descriptor.Write(static_cast<u32>(chain.bones.size()));
			for (i16 bone : chain.bones) {
				descriptor.Write(bone);
			}
		}
		descriptor.Write(static_cast<u32>(goals.size()));
		for (const str& role : goals) {
			descriptor.WriteString(role);
		}

		descriptor.Write(static_cast<u32>(referencePose.size()));
		payload.WriteBytes(referencePose.data(), referencePose.size() * sizeof(BoneTransform));
		return true;
	}

	bool RetargetProfile::LoadCooked(CookReader& descriptor, CookReader& payload) {
		Guid readSkeleton;
		descriptor.Read(readSkeleton);
		u32 chainCount = 0;
		descriptor.Read(chainCount);

		constexpr size_t BYTES_PER_CHAIN = sizeof(u32) + sizeof(u32);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(chainCount) * BYTES_PER_CHAIN) {
			return false;
		}

		Vector<Chain> readChains{GetMemoryResource(MemTag::Asset)};
		readChains.reserve(chainCount);
		for (u32 index = 0; index < chainCount; ++index) {
			Chain chain;
			chain.role = str(descriptor.ReadString());
			chain.mirror = str(descriptor.ReadString());
			u32 boneCount = 0;
			descriptor.Read(boneCount);
			if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(boneCount) * sizeof(i16)) {
				return false;
			}
			chain.bones.reserve(boneCount);
			for (u32 bone = 0; bone < boneCount; ++bone) {
				i16 value = -1;
				descriptor.Read(value);
				chain.bones.push_back(value);
			}
			if (descriptor.Failed()) {
				return false;
			}
			readChains.push_back(std::move(chain));
		}

		u32 goalCount = 0;
		descriptor.Read(goalCount);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(goalCount) * sizeof(u32)) {
			return false;
		}
		Vector<str> readGoals{GetMemoryResource(MemTag::Asset)};
		readGoals.reserve(goalCount);
		for (u32 index = 0; index < goalCount; ++index) {
			readGoals.push_back(str(descriptor.ReadString()));
		}
		if (descriptor.Failed()) {
			return false;
		}

		u32 poseCount = 0;
		descriptor.Read(poseCount);
		if (descriptor.Failed()) {
			return false;
		}
		Vector<BoneTransform> readPose{GetMemoryResource(MemTag::Asset)};
		payload.ReadInto(readPose, poseCount);
		if (payload.Failed()) {
			return false;
		}

		skeletonGuid = readSkeleton;
		chains = std::move(readChains);
		goals = std::move(readGoals);
		referencePose = std::move(readPose);
		return true;
	}

} // namespace Shalltear
