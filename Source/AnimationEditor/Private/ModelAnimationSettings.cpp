#include "ModelAnimationSettings.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <deque>

namespace Shalltear {

	namespace AnimationEditor {

		namespace {

			constexpr std::string_view Block = "animation";
			constexpr std::string_view KeyType = "animationType";
			constexpr std::string_view KeyRigPose = "rigPose";
			constexpr std::string_view KeyStanceSource = "stanceSource";
			constexpr std::string_view KeyClips = "clips";
			constexpr std::string_view KeyName = "name";
			constexpr std::string_view KeyStart = "start";
			constexpr std::string_view KeyEnd = "end";
			constexpr std::string_view KeyLoop = "loop";
			constexpr std::string_view KeyMirror = "mirror";

			std::string_view NameOf(AnimationLoopMode loop) {
				switch (loop) {
				case AnimationLoopMode::Loop:
					return "Loop";
				case AnimationLoopMode::PingPong:
					return "PingPong";
				case AnimationLoopMode::None:
				default:
					return "None";
				}
			}

			AnimationLoopMode LoopFromName(std::string_view name) {
				if (name == "Loop") {
					return AnimationLoopMode::Loop;
				}
				if (name == "PingPong") {
					return AnimationLoopMode::PingPong;
				}
				return AnimationLoopMode::None;
			}

			constexpr std::string_view TypeNone = "None";
			constexpr std::string_view TypeGeneric = "Generic";

			std::string_view NameOf(ModelAnimationType type) {
				return type == ModelAnimationType::None ? TypeNone : TypeGeneric;
			}

			constexpr std::string_view PoseFromModel = "FromModel";
			constexpr std::string_view PoseBind = "BindPose";
			constexpr std::string_view PoseCopyFrom = "CopyFrom";

			std::string_view NameOf(ModelRigPose pose) {
				switch (pose) {
				case ModelRigPose::BindPose:
					return PoseBind;
				case ModelRigPose::CopyFrom:
					return PoseCopyFrom;
				default:
					return PoseFromModel;
				}
			}

		} // anonymous namespace

		ModelAnimationSettings ReadModelAnimationSettings(const JSONValue& importerSettings) {
			ModelAnimationSettings settings;
			if (!importerSettings.IsObject()) {
				return settings;
			}
			const JSONValue* block = importerSettings.Find(Block);
			if (!block || !block->IsObject()) {
				return settings;
			}
			const JSONValue* type = block->Find(KeyType);
			if (type && type->AsString() == TypeNone) {
				settings.type = ModelAnimationType::None;
			}

			const JSONValue* rigPose = block->Find(KeyRigPose);
			if (rigPose && rigPose->AsString() == PoseBind) {
				settings.rigPose = ModelRigPose::BindPose;
			} else if (rigPose && rigPose->AsString() == PoseCopyFrom) {
				settings.rigPose = ModelRigPose::CopyFrom;
			}

			const JSONValue* stanceSource = block->Find(KeyStanceSource);
			if (stanceSource) {
				settings.stanceSource = Guid::FromString(stanceSource->AsString());
			}

			const JSONValue* clips = block->Find(KeyClips);
			if (clips && clips->IsArray()) {
				for (size_t i = 0; i < clips->Size(); ++i) {
					const JSONValue& entry = (*clips)[i];
					if (!entry.IsObject()) {
						continue;
					}
					ModelClipRange range;
					range.name = str(entry[KeyName].AsString());
					range.start = static_cast<f32>(entry[KeyStart].AsNumber(0.0));
					range.end = static_cast<f32>(entry[KeyEnd].AsNumber(0.0));
					range.loop = LoopFromName(entry[KeyLoop].AsString());
					range.mirror = entry[KeyMirror].AsBool(false);
					if (range.name.empty() || range.end <= range.start) {
						SLT_LOG_WARN("Animation", "ignoring a clip range with no name or no duration");
						continue;
					}
					settings.clips.push_back(std::move(range));
				}
			}
			return settings;
		}

		ModelAnimationSettings LoadModelAnimationSettings(const Path& modelVirtualPath) {
			Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(modelVirtualPath));
			str text = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
			LinearAllocator arena(text.size() + 64, MemTag::Editor);
			JSONValue root = text.empty() ? JSONValue() : JSONParse(text, arena);
			return ReadModelAnimationSettings(root["importer"]["settings"]);
		}

		bool ApplyModelAnimationSettings(const Path& modelVirtualPath, const ModelAnimationSettings& settings) {
			Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(modelVirtualPath));
			str text = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
			LinearAllocator arena(text.size() + 64, MemTag::Editor);
			JSONValue existing = text.empty() ? JSONValue() : JSONParse(text, arena);

			JSONValue block = JSONValue::MakeObject();
			block.AddMember(KeyType, JSONValue::MakeString(NameOf(settings.type)));
			block.AddMember(KeyRigPose, JSONValue::MakeString(NameOf(settings.rigPose)));

			std::deque<str> pool;

			if (settings.stanceSource.IsValid()) {
				pool.push_back(settings.stanceSource.ToString());
				block.AddMember(KeyStanceSource, JSONValue::MakeString(pool.back()));
			}
			if (!settings.clips.empty()) {
				JSONValue array = JSONValue::MakeArray();
				for (const ModelClipRange& range : settings.clips) {
					JSONValue entry = JSONValue::MakeObject();
					pool.push_back(range.name);
					entry.AddMember(KeyName, JSONValue::MakeString(pool.back()));
					entry.AddMember(KeyStart, JSONValue::MakeNumber(range.start));
					entry.AddMember(KeyEnd, JSONValue::MakeNumber(range.end));
					entry.AddMember(KeyLoop, JSONValue::MakeString(NameOf(range.loop)));
					if (range.mirror) {
						entry.AddMember(KeyMirror, JSONValue::MakeBool(true));
					}
					array.Push(std::move(entry));
				}
				block.AddMember(KeyClips, std::move(array));
			}

			JSONValue rebuilt = JSONValue::MakeObject();
			rebuilt.AddMember(Block, std::move(block));

			const JSONValue& importer = existing["importer"];
			const JSONValue* previous = importer.IsObject() ? importer.Find("settings") : nullptr;
			if (previous && previous->IsObject()) {
				for (size_t i = 0; i < previous->MemberCount(); ++i) {
					if (previous->KeyAt(i) != Block) {
						rebuilt.AddMember(previous->KeyAt(i), previous->ChildAt(i));
					}
				}
			}

			if (!AssetDatabase::WriteImporterSettings(modelVirtualPath, rebuilt)) {
				return false;
			}
			if (!AssetDatabase::ReimportAsset(modelVirtualPath)) {
				SLT_LOG_WARN("Animation", "'{}': settings saved, the model couldn't be re-imported. Close what uses it and re-import",
							 modelVirtualPath.ToString());
				return false;
			}
			return true;
		}

	} // namespace AnimationEditor

} // namespace Shalltear
