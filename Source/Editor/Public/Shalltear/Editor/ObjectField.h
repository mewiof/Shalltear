#pragma once

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/DragDrop.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <functional>
#include <string_view>

namespace Shalltear::ObjectField {

	struct Desc {
		std::string_view caption;

		std::function<bool(const UI::DragPayload&)> accepts;
		std::function<void(const UI::DragPayload&, Vec2)> onDrop;

		std::function<void()> onPick;
		std::string_view pickTooltip = "Select";
		std::function<void()> onClear;

		std::function<EditorPingTarget()> resolveTarget;
	};

	struct Parts {
		UI::ElementHandle root;
		UI::ElementHandle caption;
		UI::ElementHandle picker;
		UI::ElementHandle clear;
	};

	namespace Detail {

		inline Color CaptionText() { return EditorTheme::GetColor(EditorTheme::ColorToken::Text); }
		inline f32 CaptionTextPx() { return EditorTheme::GetMetric(EditorTheme::MetricToken::FontBody); }

		inline void DressButton(UI::Context& context, UI::ElementHandle button) {
			context.AddClass(button, EditorTheme::Class::ObjectFieldButton);
		}

	} // namespace Detail

	inline bool TakesTexture(const UI::DragPayload& payload, RHI::TextureDimension dimension) {
		if (payload.type != "Asset"_hash || !payload.data.Is<AssetDragData>() ||
			StringHash{payload.data.As<AssetDragData>().assetTypeHash} != StringHash("TextureAsset")) {
			return false;
		}
		Asset* asset = AssetDatabase::LoadAssetByGuid(payload.data.As<AssetDragData>().guid);
		if (!asset) {
			return false;
		}
		const bool taken = asset->GetAssetTypeId() == StringHash("TextureAsset") && static_cast<TextureAsset*>(asset)->GetDimension() == dimension;
		AssetDatabase::UnloadAsset(asset);
		return taken;
	}

	inline Parts Build(UI::Context& context, UI::ElementHandle parent, const Desc& desc) {
		Parts parts;
		Handle<FontAsset> font = UISystem::GetDefaultFont();

		parts.root = context.CreateElement();
		context.AppendChild(parent, parts.root);
		context.AddClass(parts.root, EditorTheme::Class::ObjectField);

		parts.caption = context.CreateElement();
		context.AppendChild(parts.root, parts.caption);
		context.AddClass(parts.caption, EditorTheme::Class::RowText);
		UI::Style captionStyle;
		captionStyle.flexGrow = 1.0f;
		context.SetStyle(parts.caption, captionStyle);
		context.SetHoverable(parts.caption, false);
		context.SetText(parts.caption, font, Detail::CaptionTextPx(), Detail::CaptionText(), desc.caption);

		if (desc.accepts) {
			context.Events(parts.root).onDragOver = desc.accepts;
		}
		if (desc.onDrop) {
			context.Events(parts.root).onDrop = desc.onDrop;
		}
		if (desc.resolveTarget) {
			context.Events(parts.root).onClick = [resolve = desc.resolveTarget]() {
				EditorPingTarget target = resolve();
				if (target.IsValid()) {
					EditorEvents::PingRequested.Publish(EventBus::GetGlobal(), {target});
				}
			};
		}

		if (desc.onPick) {
			EditorIcons::IconButtonParts pick = EditorIcons::IconButton(
				context, parts.root, EditorIcons::Glyph::Picker, "\xE2\x80\xA6",
				desc.pickTooltip, desc.onPick, Detail::CaptionTextPx());
			Detail::DressButton(context, pick.button);
			parts.picker = pick.button;
		}

		if (desc.onClear) {
			UI::Widgets::ButtonDesc clear;
			clear.label = "\xC3\x97";
			clear.font = font;
			clear.fontSizePx = EditorTheme::GetMetric(EditorTheme::MetricToken::FontTiny);
			clear.onClick = desc.onClear;
			parts.clear = UI::Widgets::Button(context, parts.root, clear);
			Detail::DressButton(context, parts.clear);
		}

		return parts;
	}

} // namespace Shalltear::ObjectField
