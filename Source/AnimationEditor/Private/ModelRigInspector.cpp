#include "AnimationEditor.h"
#include "ModelAnimationSettings.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/SkinAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include "Shalltear/Animation/RetargetProfile.h"

#include <array>

namespace Shalltear::AnimationEditor {

	namespace {

		using namespace UI;
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		struct RigProducts {
			bool hasSkeleton = false;
			bool hasProfile = false;
			bool hasSkin = false;
			str skeletonKey;
			str profileKey;
		};

		RigProducts FindRigProducts(const Guid& guid) {
			RigProducts found;
			Vector<AssetProduct> products{GetMemoryResource(MemTag::Editor)};
			AssetDatabase::GetProducts(guid, products);
			const StringHash skeletonType = SkeletonAsset::GetReflectType()->name;
			const StringHash profileType = RetargetProfile::GetReflectType()->name;
			const StringHash skinType = SkinAsset::GetReflectType()->name;
			for (const AssetProduct& product : products) {
				if (!found.hasSkeleton && product.type == skeletonType) {
					found.hasSkeleton = true;
					found.skeletonKey = product.key;
				} else if (!found.hasProfile && product.type == profileType) {
					found.hasProfile = true;
					found.profileKey = product.key;
				} else if (product.type == skinType) {
					found.hasSkin = true;
				}
			}
			return found;
		}

		struct StanceCandidate {
			str label;
			Guid guid;
		};

		Vector<StanceCandidate> FindStanceCandidates(const Path& selfPath) {
			Vector<StanceCandidate> found{GetMemoryResource(MemTag::Editor)};
			Vector<AssetBrowserOps::Entry> tree{GetMemoryResource(MemTag::Editor)};
			if (!AssetBrowserOps::ScanTree(tree)) {
				return found;
			}
			const StringHash profileType = RetargetProfile::GetReflectType()->name;
			Vector<AssetBrowserOps::Entry> products{GetMemoryResource(MemTag::Editor)};
			for (const AssetBrowserOps::Entry& entry : tree) {
				if (entry.IsDirectory() || entry.relPath.empty()) {
					continue;
				}
				const Path candidatePath(str("project://") + entry.relPath);
				if (candidatePath == selfPath) {
					continue;
				}
				AssetBrowserOps::ScanProducts(entry.relPath, 0, products);
				for (const AssetBrowserOps::Entry& product : products) {
					if (product.assetType == profileType) {
						StanceCandidate candidate;
						candidate.label = entry.name;
						candidate.guid = AssetDatabase::LookupGuid(candidatePath);
						if (candidate.guid.IsValid()) {
							found.push_back(std::move(candidate));
						}
						break;
					}
				}
			}
			return found;
		}

		ElementHandle AddLine(Context& context, ElementHandle parent, ColorToken color, MetricToken size, std::string_view text) {
			ElementHandle line = context.CreateElement();
			context.AppendChild(parent, line);
			context.SetText(line, UISystem::GetDefaultFont(), EditorTheme::GetMetric(size),
							EditorTheme::GetColor(color), text);
			return line;
		}

		ElementHandle AddRow(Context& context, ElementHandle parent) {
			ElementHandle row = context.CreateElement();
			context.AppendChild(parent, row);
			Style style;
			style.flexDirection = FlexDirection::Row;
			style.width = Length::Percent(1.0f);
			style.margin = {0.0f, 2.0f, 0.0f, 2.0f};
			context.SetStyle(row, style);
			return row;
		}

		ElementHandle AddLabeledRow(Context& context, ElementHandle parent, std::string_view label) {
			ElementHandle row = AddRow(context, parent);
			ElementHandle text = context.CreateElement();
			context.AppendChild(row, text);
			Style style;
			style.width = Length::Pixel(120.0f);
			style.flexShrink = 0.0f;
			context.SetStyle(text, style);
			context.SetText(text, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontBody),
							EditorTheme::GetColor(ColorToken::WidgetTextDim), label);
			return row;
		}

		bool AppliesToModel(const Guid& guid, Asset& asset) {
			(void)asset;
			RigProducts products = FindRigProducts(guid);
			return products.hasSkeleton;
		}

		void DrawRigSection(Context& context, ElementHandle parent, const Guid& guid, Asset& asset) {
			(void)asset;
			const Path modelPath = AssetDatabase::LookupPath(guid);
			RigProducts products = FindRigProducts(guid);

			ModelAnimationSettings settings = LoadModelAnimationSettings(modelPath);

			{
				ElementHandle row = AddLabeledRow(context, parent, "Animation Type");
				static constexpr std::array<std::string_view, 2> ITEMS{"Generic", "None"};
				Widgets::DropdownDesc desc;
				desc.items = std::span<const std::string_view>(ITEMS.data(), ITEMS.size());
				desc.selected = settings.type == ModelAnimationType::None ? 1u : 0u;
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
				desc.onSelect = [modelPath](u32 index) {
					ModelAnimationSettings next = LoadModelAnimationSettings(modelPath);
					next.type = index == 1 ? ModelAnimationType::None : ModelAnimationType::Generic;
					ApplyModelAnimationSettings(modelPath, next);
				};
				Widgets::Dropdown(context, row, desc);
			}

			{
				ElementHandle row = AddLabeledRow(context, parent, "Skeleton");
				AddLine(context, row, ColorToken::WidgetText, MetricToken::FontBody,
						products.hasSkeleton ? products.skeletonKey : str("(none)"));
			}

			{
				ElementHandle row = AddLabeledRow(context, parent, "Humanoid Rig");
				if (products.hasProfile) {
					AddLine(context, row, ColorToken::StatusOk, MetricToken::FontBody,
							"\xE2\x9C\x93  " + products.profileKey);
				} else {
					AddLine(context, row, ColorToken::StatusWarn, MetricToken::FontBody,
							"\xE2\x9C\x95  no humanoid match");
				}
			}
			if (!products.hasProfile) {
				AddLine(context, parent, ColorToken::WidgetTextDim, MetricToken::FontSmall,
						"This rig's bone names weren't recognized (no retarget profile). Clips still play.");
				return;
			}

			{
				ElementHandle row = AddLabeledRow(context, parent, "Rig Pose");
				static constexpr std::array<std::string_view, 3> ITEMS{"From Model", "Sample Bind-pose", "Copy From Model"};
				Widgets::DropdownDesc desc;
				desc.items = std::span<const std::string_view>(ITEMS.data(), ITEMS.size());
				desc.selected = settings.rigPose == ModelRigPose::BindPose	 ? 1u
								: settings.rigPose == ModelRigPose::CopyFrom ? 2u
																			 : 0u;
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
				desc.onSelect = [modelPath](u32 index) {
					ModelAnimationSettings next = LoadModelAnimationSettings(modelPath);
					next.rigPose = index == 1	? ModelRigPose::BindPose
								   : index == 2 ? ModelRigPose::CopyFrom
												: ModelRigPose::FromModel;
					ApplyModelAnimationSettings(modelPath, next);
				};
				Widgets::Dropdown(context, row, desc);
			}

			if (settings.rigPose == ModelRigPose::CopyFrom) {
				const Vector<StanceCandidate> candidates = FindStanceCandidates(modelPath);

				Vector<std::string_view> items{GetMemoryResource(MemTag::Editor)};
				items.push_back("(none)");
				u32 selected = 0;
				for (const StanceCandidate& candidate : candidates) {
					if (candidate.guid == settings.stanceSource) {
						selected = static_cast<u32>(items.size());
					}
					items.push_back(candidate.label);
				}

				ElementHandle row = AddLabeledRow(context, parent, "Source");
				Widgets::DropdownDesc desc;
				desc.items = std::span<const std::string_view>(items.data(), items.size());
				desc.selected = selected;
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
				desc.onSelect = [modelPath, candidates](u32 index) {
					ModelAnimationSettings next = LoadModelAnimationSettings(modelPath);
					next.stanceSource = index == 0 || index > candidates.size()
											? Guid{}
											: candidates[index - 1].guid;
					ApplyModelAnimationSettings(modelPath, next);
				};
				Widgets::Dropdown(context, row, desc);

				if (candidates.empty()) {
					AddLine(context, parent, ColorToken::StatusWarn, MetricToken::FontSmall,
							"No other model in this project has a humanoid rig to copy a stance from.");
				} else if (!settings.stanceSource.IsValid()) {
					AddLine(context, parent, ColorToken::StatusWarn, MetricToken::FontSmall,
							"No source is set. Rig uses the file's stance.");
				}
			}
			if (!products.hasSkin && settings.rigPose == ModelRigPose::BindPose) {
				AddLine(context, parent, ColorToken::StatusWarn, MetricToken::FontSmall,
						"This file has no skin (no bind pose to sample). Rig uses its nodes' stance (for an animation file, a frame of the animation). Use Copy From Model.");
			}
			AddLine(context, parent, ColorToken::WidgetTextDim, MetricToken::FontSmall,
					"Stance this rig's clips are retargeted against. Sample Bind-pose when the file was saved mid-animation, Copy From Model when it has no skin.");
		}

	} // anonymous namespace

	void RegisterModelRigInspector(const void* owner) {
		AssetInspectorSectionDesc desc;
		desc.label = "Rig";
		desc.order = 100;
		desc.applies = &AppliesToModel;
		desc.draw = &DrawRigSection;
		if (!PropertyDrawers::RegisterAssetInspectorSection(StringHash("PrefabAsset"), std::move(desc), owner)) {
			SLT_LOG_ERROR("Animation", "couldn't register the Rig tab's animation rows");
		}
	}

} // namespace Shalltear::AnimationEditor
