#include "ImportPipelineEditor.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include <deque>
#include <functional>

namespace Shalltear::ImportPipelineEditor {

	namespace {

		using namespace UI;
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		constexpr std::string_view Block = "rig";
		constexpr std::string_view KeyExposed = "exposedBones";

		struct Draft {
			Guid asset;
			Vector<str> bones{GetMemoryResource(MemTag::Editor)};
			Vector<str> exposed{GetMemoryResource(MemTag::Editor)};
		};
		Draft* _draft = nullptr;

		Guid SkeletonOf(const Guid& model) {
			Vector<AssetProduct> products{GetMemoryResource(MemTag::Editor)};
			AssetDatabase::GetProducts(model, products);
			const StringHash skeletonType = SkeletonAsset::GetReflectType()->name;
			for (const AssetProduct& product : products) {
				if (product.type == skeletonType) {
					return product.guid;
				}
			}
			return {};
		}

		Draft& DraftFor(const Guid& guid, const Path& modelPath) {
			if (!_draft) {
				_draft = New<Draft>(MemTag::Editor);
			}
			if (_draft->asset == guid) {
				return *_draft;
			}
			_draft->asset = guid;
			_draft->bones.clear();
			_draft->exposed.clear();

			const Guid skeletonGuid = SkeletonOf(guid);
			if (Asset* resolved = skeletonGuid.IsValid() ? AssetDatabase::LoadAssetByGuid(skeletonGuid) : nullptr) {
				auto* skeleton = static_cast<SkeletonAsset*>(resolved);
				_draft->bones.reserve(skeleton->boneNames.size());
				for (const str& name : skeleton->boneNames) {
					_draft->bones.push_back(name);
				}
				AssetDatabase::UnloadAsset(resolved);
			}

			Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(modelPath));
			str text = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
			LinearAllocator arena(text.size() + 64, MemTag::Editor);
			JSONValue root = text.empty() ? JSONValue() : JSONParse(text, arena);
			const JSONValue& rig = root["importer"]["settings"][Block];
			const JSONValue& list = rig[KeyExposed];
			if (list.IsArray()) {
				for (size_t i = 0; i < list.Size(); ++i) {
					std::string_view name = list[i].AsString();
					if (!name.empty()) {
						_draft->exposed.push_back(str(name));
					}
				}
			} else {
				_draft->exposed = _draft->bones;
			}
			return *_draft;
		}

		bool IsExposed(const Draft& draft, const str& bone) {
			return std::find(draft.exposed.begin(), draft.exposed.end(), bone) != draft.exposed.end();
		}

		bool ApplyExposedBones(const Path& modelPath, const Draft& draft) {
			Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(modelPath));
			str text = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
			LinearAllocator arena(text.size() + 64, MemTag::Editor);
			JSONValue existing = text.empty() ? JSONValue() : JSONParse(text, arena);

			std::deque<str> pool;
			JSONValue block = JSONValue::MakeObject();

			if (draft.exposed.size() != draft.bones.size()) {
				JSONValue array = JSONValue::MakeArray();
				for (const str& bone : draft.exposed) {
					pool.push_back(bone);
					array.Push(JSONValue::MakeString(pool.back()));
				}
				block.AddMember(KeyExposed, std::move(array));
			}

			JSONValue rebuilt = JSONValue::MakeObject();
			if (block.MemberCount() > 0) {
				rebuilt.AddMember(Block, std::move(block));
			}
			const JSONValue& importer = existing["importer"];
			const JSONValue* previous = importer.IsObject() ? importer.Find("settings") : nullptr;
			if (previous && previous->IsObject()) {
				for (size_t i = 0; i < previous->MemberCount(); ++i) {
					if (previous->KeyAt(i) != Block) {
						rebuilt.AddMember(previous->KeyAt(i), previous->ChildAt(i));
					}
				}
			}

			if (!AssetDatabase::WriteImporterSettings(modelPath, rebuilt)) {
				return false;
			}
			if (!AssetDatabase::ReimportAsset(modelPath)) {
				SLT_LOG_WARN("ImportPipeline", "'{}': exposed-bone list saved, the model couldn't be re-imported. Close what uses it and re-import",
							 modelPath.ToString());
				return false;
			}
			return true;
		}

		// runs per clicked asset, must not load
		bool AppliesToRiggedModel(const Guid& guid, Asset& asset) {
			(void)asset;
			return SkeletonOf(guid).IsValid();
		}

		void DrawExposedBones(Context& context, ElementHandle parent, const Guid& guid, Asset& asset) {
			(void)asset;
			const Path modelPath = AssetDatabase::LookupPath(guid);
			Draft& draft = DraftFor(guid, modelPath);
			std::function<void()> rebuild = PropertyDrawers::GetInspectorRebuilder();

			ElementHandle header = context.CreateElement();
			context.AppendChild(parent, header);
			context.SetText(header, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontBody),
							EditorTheme::GetColor(ColorToken::WidgetText), "Exposed Bones");

			ElementHandle summary = context.CreateElement();
			context.AppendChild(parent, summary);
			context.SetText(summary, UISystem::GetDefaultFont(),
							EditorTheme::GetMetric(MetricToken::FontSmall),
							EditorTheme::GetColor(ColorToken::WidgetTextDim),
							std::format("{} of {} bone(s) get a SceneObject. Unticking one removes its object (the bone still animates).",
										draft.exposed.size(), draft.bones.size()));

			Widgets::ScrollViewParts list = Widgets::ScrollView(context, parent);
			{
				Style style = context.GetStyle(list.root);
				style.width = Length::Percent(1.0f);
				style.height = Length::Pixel(EditorTheme::GetMetric(MetricToken::InlineListHeight));
				context.SetStyle(list.root, style);
			}
			for (size_t i = 0; i < draft.bones.size(); ++i) {
				const str& bone = draft.bones[i];
				ElementHandle row = context.CreateElement();
				context.AppendChild(list.viewport, row);
				Style style;
				style.flexDirection = FlexDirection::Row;
				style.width = Length::Percent(1.0f);
				style.alignItems = Align::Center;
				style.margin = {0.0f, 2.0f, 0.0f, 2.0f};
				context.SetStyle(row, style);

				Widgets::Checkbox(context, row, IsExposed(draft, bone), [bone](bool on) {
					if (!_draft) {
						return;
					}
					auto it = std::find(_draft->exposed.begin(), _draft->exposed.end(), bone);
					if (on && it == _draft->exposed.end()) {
						_draft->exposed.push_back(bone);
					} else if (!on && it != _draft->exposed.end()) {
						_draft->exposed.erase(it);
					}
				});
				ElementHandle label = context.CreateElement();
				context.AppendChild(row, label);
				context.SetText(label, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontBody),
								EditorTheme::GetColor(ColorToken::WidgetText), bone);
			}

			ElementHandle actions = context.CreateElement();
			context.AppendChild(parent, actions);
			{
				Style style;
				style.flexDirection = FlexDirection::Row;
				style.width = Length::Percent(1.0f);
				style.alignItems = Align::Center;
				style.margin = {0.0f, 2.0f, 0.0f, 2.0f};
				context.SetStyle(actions, style);
			}

			Widgets::ButtonDesc apply;
			apply.label = "Apply";
			apply.font = UISystem::GetDefaultFont();
			apply.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
			apply.onClick = [modelPath, rebuild]() {
				if (!_draft) {
					return;
				}
				rebuild();
				ApplyExposedBones(modelPath, *_draft);
				_draft->asset = Guid{};
			};
			Widgets::Button(context, actions, apply);
		}

	} // anonymous namespace

	void RegisterModelRigSection(const void* owner) {
		AssetInspectorSectionDesc desc;
		desc.label = "Rig";
		desc.order = 10;
		desc.applies = &AppliesToRiggedModel;
		desc.draw = &DrawExposedBones;
		if (!PropertyDrawers::RegisterAssetInspectorSection(StringHash("PrefabAsset"), std::move(desc), owner)) {
			SLT_LOG_ERROR("ImportPipeline", "couldn't register the Rig tab's exposed-bone list");
		}
	}

	void ShutdownModelRigSection() {
		if (_draft) {
			Delete(_draft);
			_draft = nullptr;
		}
	}

} // namespace Shalltear::ImportPipelineEditor
