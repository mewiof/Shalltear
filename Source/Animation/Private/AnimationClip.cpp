#include "Shalltear/Animation/AnimationClip.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

namespace Shalltear {

	AnimationClip::~AnimationClip() = default;

	void AnimationClip::OnLoad() {
		HoldCueAssets();
		if (!skeletonGuid.IsValid()) {
			return;
		}
		Asset* resolved = AssetDatabase::LoadAssetByGuid(skeletonGuid);
		if (!resolved) {
			SLT_LOG_WARN("Animation", "AnimationClip '{}': skeleton {} is unresolvable (bone tracks still bind by name)", virtualPath, skeletonGuid.ToString());
			return;
		}
		skeleton = Handle<SkeletonAsset>(static_cast<SkeletonAsset*>(resolved));
	}

	void AnimationClip::HoldCueAssets() {
		Vector<Handle<Asset>> held{GetMemoryResource(MemTag::Asset)};
		for (const Track& track : tracks) {
			for (const Guid& guid : track.assets) {
				bool already = !guid.IsValid();
				for (size_t index = 0; !already && index < held.size(); ++index) {
					already = held[index].Get()->assetId == guid;
				}
				if (already) {
					continue;
				}
				Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
				if (!loaded) {
					SLT_LOG_WARN("Animation", "AnimationClip '{}': audio key uses {} (can't be loaded, key ignored)", virtualPath, guid.ToString());
					continue;
				}
				held.push_back(Handle<Asset>(loaded));
			}
		}
		_heldCues.swap(held);
		for (const Handle<Asset>& stale : held) {
			if (Asset* asset = stale.Get()) {
				AssetDatabase::UnloadAsset(asset);
			}
		}
	}

	void AnimationClip::OnUnload() {
		if (SkeletonAsset* resolved = skeleton.Get()) {
			AssetDatabase::UnloadAsset(resolved);
		}
		skeleton = {};
		for (const Handle<Asset>& held : _heldCues) {
			if (Asset* asset = held.Get()) {
				AssetDatabase::UnloadAsset(asset);
			}
		}
		_heldCues.clear();
	}

	bool AnimationClip::Cook(CookWriter& descriptor, CookWriter& payload) const {
		descriptor.WriteAssetRef(skeletonGuid);
		descriptor.Write(length);
		descriptor.Write(frameRate);
		descriptor.Write(static_cast<u8>(loop));
		descriptor.Write(additiveReferenceTime);
		descriptor.Write(static_cast<u8>(bakeRootMotionIntoPose ? 1 : 0));
		descriptor.Write(static_cast<u32>(tracks.size()));

		for (const Track& track : tracks) {
			descriptor.Write(static_cast<u8>(track.kind));
			descriptor.Write(static_cast<u8>(track.interp));
			descriptor.Write(track.component);
			descriptor.WriteString(track.objectPath);
			descriptor.Write(track.componentType.GetValue());
			descriptor.WriteString(track.name);

			const u32 keys = static_cast<u32>(track.times.size());
			descriptor.Write(keys);
			if (track.kind == AnimationTrackKind::Audio) {
				for (u32 key = 0; key < keys; ++key) {
					descriptor.WriteAssetRef(key < track.assets.size() ? track.assets[key] : Guid{});
				}
			}

			payload.WriteBytes(track.times.data(), track.times.size() * sizeof(f32));
			payload.WriteBytes(track.values.data(), track.values.size() * sizeof(Vec4));
		}
		return true;
	}

	bool AnimationClip::LoadCooked(CookReader& descriptor, CookReader& payload) {
		Guid readSkeleton;
		descriptor.Read(readSkeleton);
		f32 readLength = 0.0f;
		f32 readFrameRate = 0.0f;
		u8 readLoop = 0;
		f32 readAdditiveReference = 0.0f;
		u8 readBakeRootMotion = 1;
		u32 trackCount = 0;
		descriptor.Read(readLength);
		descriptor.Read(readFrameRate);
		descriptor.Read(readLoop);
		descriptor.Read(readAdditiveReference);
		descriptor.Read(readBakeRootMotion);
		descriptor.Read(trackCount);

		constexpr size_t BYTES_PER_TRACK = 3 * sizeof(u8) + 2 * sizeof(u32) + sizeof(u64) + sizeof(u32);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(trackCount) * BYTES_PER_TRACK) {
			return false;
		}

		Vector<Track> readTracks{GetMemoryResource(MemTag::Asset)};
		readTracks.reserve(trackCount);
		for (u32 i = 0; i < trackCount; ++i) {
			Track track;
			u8 kind = 0;
			u8 interp = 0;
			descriptor.Read(kind);
			descriptor.Read(interp);
			descriptor.Read(track.component);
			track.objectPath = str(descriptor.ReadString());
			u64 componentType = 0;
			descriptor.Read(componentType);
			track.name = str(descriptor.ReadString());
			u32 keys = 0;
			descriptor.Read(keys);
			if (descriptor.Failed()) {
				return false;
			}
			if (kind > static_cast<u8>(AnimationTrackKind::Audio) ||
				interp > static_cast<u8>(AnimationInterpolation::CubicSpline)) {
				return false;
			}
			track.kind = static_cast<AnimationTrackKind>(kind);
			track.interp = static_cast<AnimationInterpolation>(interp);
			track.componentType = StringHash(componentType);
			if (track.kind == AnimationTrackKind::Audio) {
				if (descriptor.Remaining() < static_cast<size_t>(keys) * sizeof(Guid)) {
					return false;
				}
				track.assets.resize(keys);
				for (Guid& guid : track.assets) {
					descriptor.Read(guid);
				}
			}

			payload.ReadInto(track.times, keys);
			payload.ReadInto(track.values, static_cast<size_t>(keys) * AnimationValuesPerKey(track.interp));
			if (payload.Failed()) {
				return false;
			}
			readTracks.push_back(std::move(track));
		}
		if (descriptor.Failed() || readLoop > static_cast<u8>(AnimationLoopMode::PingPong)) {
			return false;
		}

		skeletonGuid = readSkeleton;
		length = readLength;
		frameRate = readFrameRate;
		loop = static_cast<AnimationLoopMode>(readLoop);
		additiveReferenceTime = readAdditiveReference;
		bakeRootMotionIntoPose = readBakeRootMotion != 0;
		tracks = std::move(readTracks);
		if (GetLoadState() == AssetLoadState::Loaded) {
			HoldCueAssets();
		}

		++generation;
		return true;
	}

	namespace {

		constexpr std::string_view KeyLength = "length";
		constexpr std::string_view KeyFrameRate = "frameRate";
		constexpr std::string_view KeyLoop = "loop";
		constexpr std::string_view KeyAdditiveRef = "additiveReferenceTime";
		constexpr std::string_view KeyBakeRoot = "bakeRootMotionIntoPose";
		constexpr std::string_view KeySkeleton = "skeleton";
		constexpr std::string_view KeyTracks = "tracks";
		constexpr std::string_view KeyKind = "kind";
		constexpr std::string_view KeyInterp = "interp";
		constexpr std::string_view KeyComponent = "component";
		constexpr std::string_view KeyObjectPath = "objectPath";
		constexpr std::string_view KeyComponentType = "componentType";
		constexpr std::string_view KeyName = "name";
		constexpr std::string_view KeyTimes = "times";
		constexpr std::string_view KeyValues = "values";
		constexpr std::string_view KeyAssets = "assets";

		constexpr std::string_view TrackKindNames[] = {
			"BonePosition",
			"BoneRotation",
			"BoneScale",
			"Property",
			"Event",
			"Method",
			"BlendShape",
			"Audio",
		};
		constexpr std::string_view InterpNames[] = {"Step", "Linear", "CubicSpline"};
		constexpr std::string_view LoopNames[] = {"None", "Loop", "PingPong"};

		template <size_t N>
		std::string_view NameOfEnum(const std::string_view (&names)[N], u8 value) {
			return value < N ? names[value] : names[0];
		}

		template <size_t N>
		bool EnumFromName(const std::string_view (&names)[N], std::string_view text, u8& out) {
			for (size_t i = 0; i < N; ++i) {
				if (names[i] == text) {
					out = static_cast<u8>(i);
					return true;
				}
			}
			return false;
		}

		str HexOf(StringHash hash) { return std::format("{:016x}", hash.GetValue()); }

		bool HexTo(std::string_view text, u64& out) {
			if (text.empty()) {
				out = 0;
				return true;
			}
			if (text.size() != 16) {
				return false;
			}
			u64 value = 0;
			for (char c : text) {
				u32 digit;
				if (c >= '0' && c <= '9') {
					digit = static_cast<u32>(c - '0');
				} else if (c >= 'a' && c <= 'f') {
					digit = static_cast<u32>(c - 'a') + 10u;
				} else {
					return false;
				}
				value = (value << 4) | digit;
			}
			out = value;
			return true;
		}

	} // namespace

	bool AnimationClip::Export(const Path& destPath) const {
		JSONValue root = JSONValue::MakeObject();
		root.AddMember(KeyLength, JSONValue::MakeNumber(length));
		root.AddMember(KeyFrameRate, JSONValue::MakeNumber(frameRate));
		root.AddMember(KeyLoop, JSONValue::MakeString(NameOfEnum(LoopNames, static_cast<u8>(loop))));
		root.AddMember(KeyAdditiveRef, JSONValue::MakeNumber(additiveReferenceTime));
		root.AddMember(KeyBakeRoot, JSONValue::MakeBool(bakeRootMotionIntoPose));

		Vector<str> pool{GetMemoryResource(MemTag::Asset)};
		size_t pooled = tracks.size() + 1;
		for (const Track& track : tracks) {
			pooled += track.assets.size();
		}
		pool.reserve(pooled);
		pool.push_back(skeletonGuid.ToString());
		root.AddMember(KeySkeleton, JSONValue::MakeString(pool.back()));

		JSONValue jsonTracks = JSONValue::MakeArray();
		for (const Track& track : tracks) {
			JSONValue row = JSONValue::MakeObject();
			row.AddMember(KeyKind, JSONValue::MakeString(NameOfEnum(TrackKindNames, static_cast<u8>(track.kind))));
			row.AddMember(KeyInterp, JSONValue::MakeString(NameOfEnum(InterpNames, static_cast<u8>(track.interp))));
			row.AddMember(KeyComponent, JSONValue::MakeNumber(static_cast<f64>(track.component)));
			row.AddMember(KeyObjectPath, JSONValue::MakeString(track.objectPath));
			pool.push_back(track.componentType.GetValue() != 0 ? HexOf(track.componentType) : str());
			row.AddMember(KeyComponentType, JSONValue::MakeString(pool.back()));
			row.AddMember(KeyName, JSONValue::MakeString(track.name));

			JSONValue times = JSONValue::MakeArray();
			for (f32 t : track.times) {
				times.Push(JSONValue::MakeNumber(t));
			}
			row.AddMember(KeyTimes, std::move(times));

			JSONValue values = JSONValue::MakeArray();
			for (const Vec4& v : track.values) {
				JSONValue entry = JSONValue::MakeArray();
				entry.Push(JSONValue::MakeNumber(v.x));
				entry.Push(JSONValue::MakeNumber(v.y));
				entry.Push(JSONValue::MakeNumber(v.z));
				entry.Push(JSONValue::MakeNumber(v.w));
				values.Push(std::move(entry));
			}
			row.AddMember(KeyValues, std::move(values));
			if (track.kind == AnimationTrackKind::Audio) {
				JSONValue assets = JSONValue::MakeArray();
				for (const Guid& guid : track.assets) {
					pool.push_back(guid.ToString());
					assets.Push(JSONValue::MakeString(pool.back()));
				}
				row.AddMember(KeyAssets, std::move(assets));
			}
			jsonTracks.Push(std::move(row));
		}
		root.AddMember(KeyTracks, std::move(jsonTracks));

		return FileSystem::WriteTextFile(destPath, JSONSerialize(root));
	}

	bool AnimationClip::Import(const Path& sourcePath) {
		str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		if (text.empty()) {
			return true;
		}
		LinearAllocator arena(text.size() + 64, MemTag::Asset);
		JSONValue root = JSONParse(text, arena);
		if (!root.IsObject()) {
			SLT_LOG_ERROR("Animation", "AnimationClip '{}': not a JSON object", sourcePath.ToString());
			return false;
		}

		f32 readLength = static_cast<f32>(root[KeyLength].AsNumber(0.0));
		f32 readFrameRate = static_cast<f32>(root[KeyFrameRate].AsNumber(0.0));
		u8 readLoop = 0;
		const JSONValue* loopValue = root.Find(KeyLoop);
		if (loopValue && !EnumFromName(LoopNames, loopValue->AsString(), readLoop)) {
			SLT_LOG_ERROR("Animation", "AnimationClip '{}': unknown loop mode '{}'", sourcePath.ToString(), loopValue->AsString());
			return false;
		}
		f32 readAdditiveReference = static_cast<f32>(root[KeyAdditiveRef].AsNumber(0.0));
		bool readBakeRootMotion = root[KeyBakeRoot].AsBool(true);
		Guid readSkeleton;
		const JSONValue* skeletonValue = root.Find(KeySkeleton);
		if (skeletonValue && skeletonValue->IsString()) {
			readSkeleton = Guid::FromString(skeletonValue->AsString());
		}

		Vector<Track> readTracks{GetMemoryResource(MemTag::Asset)};
		const JSONValue* jsonTracks = root.Find(KeyTracks);
		if (jsonTracks && jsonTracks->IsArray()) {
			readTracks.reserve(jsonTracks->Size());
			for (size_t i = 0; i < jsonTracks->Size(); ++i) {
				const JSONValue& row = (*jsonTracks)[i];
				if (!row.IsObject()) {
					continue;
				}
				Track track;
				u8 kind = 0;
				u8 interp = 0;
				if (!EnumFromName(TrackKindNames, row[KeyKind].AsString(), kind) ||
					!EnumFromName(InterpNames, row[KeyInterp].AsString(), interp)) {
					SLT_LOG_ERROR("Animation", "AnimationClip '{}': track {} has a kind or interpolation this build doesn't know", sourcePath.ToString(), i);
					return false;
				}
				track.kind = static_cast<AnimationTrackKind>(kind);
				track.interp = static_cast<AnimationInterpolation>(interp);
				track.component = static_cast<u8>(row[KeyComponent].AsNumber(ANIMATION_WHOLE_VALUE));
				track.objectPath = str(row[KeyObjectPath].AsString());
				u64 componentType = 0;
				if (!HexTo(row[KeyComponentType].AsString(), componentType)) {
					SLT_LOG_ERROR("Animation", "AnimationClip '{}': track {} has a malformed componentType", sourcePath.ToString(), i);
					return false;
				}
				track.componentType = StringHash(componentType);
				track.name = str(row[KeyName].AsString());

				const JSONValue* times = row.Find(KeyTimes);
				if (times && times->IsArray()) {
					track.times.reserve(times->Size());
					for (size_t k = 0; k < times->Size(); ++k) {
						track.times.push_back(static_cast<f32>((*times)[k].AsNumber(0.0)));
					}
				}
				const JSONValue* values = row.Find(KeyValues);
				if (values && values->IsArray()) {
					track.values.reserve(values->Size());
					for (size_t k = 0; k < values->Size(); ++k) {
						const JSONValue& entry = (*values)[k];
						track.values.push_back(Vec4{static_cast<f32>(entry[0].AsNumber(0.0)),
													static_cast<f32>(entry[1].AsNumber(0.0)),
													static_cast<f32>(entry[2].AsNumber(0.0)),
													static_cast<f32>(entry[3].AsNumber(0.0))});
					}
				}

				if (track.values.size() != track.times.size() * AnimationValuesPerKey(track.interp)) {
					SLT_LOG_ERROR("Animation", "AnimationClip '{}': track {} has {} key(s) and {} value(s) ({} interpolation needs {} per key)",
								  sourcePath.ToString(), i, track.times.size(), track.values.size(),
								  NameOfEnum(InterpNames, interp), AnimationValuesPerKey(track.interp));
					return false;
				}
				if (track.kind == AnimationTrackKind::Audio) {
					const JSONValue* assets = row.Find(KeyAssets);
					if (assets && assets->IsArray()) {
						track.assets.reserve(assets->Size());
						for (size_t k = 0; k < assets->Size(); ++k) {
							track.assets.push_back(Guid::FromString((*assets)[k].AsString()));
						}
					}
					if (track.assets.size() != track.times.size()) {
						SLT_LOG_ERROR("Animation", "AnimationClip '{}': audio track {} doesn't have one asset per key", sourcePath.ToString(), i);
						return false;
					}
				}
				readTracks.push_back(std::move(track));
			}
		}

		skeletonGuid = readSkeleton;
		length = readLength;
		frameRate = readFrameRate;
		loop = static_cast<AnimationLoopMode>(readLoop);
		additiveReferenceTime = readAdditiveReference;
		bakeRootMotionIntoPose = readBakeRootMotion;
		tracks = std::move(readTracks);
		if (GetLoadState() == AssetLoadState::Loaded) {
			HoldCueAssets();
		}

		++generation;
		return true;
	}

	namespace {

		class AnimationClipImporter : public IAssetImporter {
		public:
			StringHash GetAssetType() const override { return AnimationClip::GetReflectType()->name; }

			u32 GetImporterVersion() const override { return 1; }

			Asset* Import(const Path& sourcePath) override {
				auto* clip = New<AnimationClip>(MemTag::Asset);
				if (!clip->Import(sourcePath)) {
					Delete(clip);
					return nullptr;
				}
				return clip;
			}

			bool CanCreateAsset() const override { return true; }
			std::string_view GetCreateMenuLabel() const override { return "Animation Clip"; }
			std::string_view GetCreateDefaultFileName() const override { return "New Animation Clip.anim"; }

			Asset* CreateAsset() override {
				auto* clip = New<AnimationClip>(MemTag::Asset);
				clip->frameRate = 60.0f;
				clip->length = 0.0f;
				return clip;
			}
		};

		AnimationClipImporter _clipImporter;

	} // namespace

	void RegisterAnimationClipImporter(const void* owner) {
		AssetImporterRegistry::Register(StringHash("AnimationClip"), &_clipImporter, {".anim"}, owner);
	}

} // namespace Shalltear
