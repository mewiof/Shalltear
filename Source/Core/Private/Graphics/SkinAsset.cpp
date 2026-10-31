#include "Shalltear/Core/Graphics/SkinAsset.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Log.h"

namespace Shalltear {

	SkinAsset::~SkinAsset() = default;

	void SkinAsset::OnLoad() {
		if (!skeletonGuid.IsValid()) {
			return;
		}
		Asset* resolved = AssetDatabase::LoadAssetByGuid(skeletonGuid);
		if (!resolved) {
			SLT_LOG_WARN("Graphics", "SkinAsset '{}': skeleton {} is unresolvable", virtualPath, skeletonGuid.ToString());
			return;
		}
		skeleton = Handle<SkeletonAsset>(static_cast<SkeletonAsset*>(resolved));
	}

	void SkinAsset::OnUnload() {
		if (SkeletonAsset* resolved = skeleton.Get()) {
			AssetDatabase::UnloadAsset(resolved);
		}
		skeleton = {};
	}

	bool SkinAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		descriptor.WriteAssetRef(skeletonGuid);
		descriptor.Write(static_cast<u32>(jointToBone.size()));
		descriptor.WriteBytes(jointToBone.data(), jointToBone.size() * sizeof(i16));

		payload.WriteBytes(inverseBind.data(), inverseBind.size() * sizeof(Mat4));
		return true;
	}

	bool SkinAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		Guid readSkeleton;
		descriptor.Read(readSkeleton);
		u32 jointCount = 0;
		descriptor.Read(jointCount);

		Vector<i16> map{GetMemoryResource(MemTag::Asset)};
		Vector<Mat4> binds{GetMemoryResource(MemTag::Asset)};
		descriptor.ReadInto(map, jointCount);
		payload.ReadInto(binds, jointCount);
		if (descriptor.Failed() || payload.Failed()) {
			return false;
		}

		skeletonGuid = readSkeleton;
		jointToBone = std::move(map);
		inverseBind = std::move(binds);
		return true;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::SkinAsset)
