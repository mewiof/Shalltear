#include "Shalltear/Editor/EditorIcons.h"

#include "EditorIconGlyphs.generated.h"

#include "Shalltear/Editor/EditorTheme.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ProceduralRenderer.h"
#include "Shalltear/Core/Graphics/ReflectionProbe.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Graphics/SkinnedMeshRenderer.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/UnresolvedComponent.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/UI/Font.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/UIPanelRenderer.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/VectorAsset.h"

#include <algorithm>

namespace Shalltear::EditorIcons {

	namespace {

		// entries are 3 byte Private Use Area characters
		u32 CodepointOf(std::string_view utf8) {
			if (utf8.empty()) {
				return 0;
			}
			const u32 first = static_cast<u8>(utf8[0]);
			if (first < 0x80) {
				return first;
			}
			if ((first & 0xE0u) == 0xC0u && utf8.size() >= 2) {
				return ((first & 0x1Fu) << 6) | (static_cast<u8>(utf8[1]) & 0x3Fu);
			}
			if ((first & 0xF0u) == 0xE0u && utf8.size() >= 3) {
				return ((first & 0x0Fu) << 12) | ((static_cast<u8>(utf8[1]) & 0x3Fu) << 6) |
					   (static_cast<u8>(utf8[2]) & 0x3Fu);
			}
			return 0;
		}

		i32 FindBaked(Icon icon) {
			const u32 codepoint = CodepointOf(icon.glyph);
			if (codepoint == 0) {
				return -1;
			}
			const u32 face = static_cast<u32>(icon.face);
			u32 low = 0;
			u32 high = Baked::GLYPH_COUNT;
			while (low < high) {
				const u32 mid = low + (high - low) / 2;
				const Baked::Glyph& probe = Baked::GLYPHS[mid];
				if (probe.face < face || (probe.face == face && probe.codepoint < codepoint)) {
					low = mid + 1;
				} else {
					high = mid;
				}
			}
			if (low < Baked::GLYPH_COUNT && Baked::GLYPHS[low].face == face &&
				Baked::GLYPHS[low].codepoint == codepoint) {
				return static_cast<i32>(low);
			}
			return -1;
		}

		struct BakedGlyphState {
			Vector<Handle<VectorAsset>> assets{GetMemoryResource(MemTag::Editor)};
		};
		BakedGlyphState* _bakedGlyphs = nullptr;

		BakedGlyphState& GlyphTable() {
			if (!_bakedGlyphs) {
				_bakedGlyphs = New<BakedGlyphState>(MemTag::Editor);
				_bakedGlyphs->assets.resize(Baked::GLYPH_COUNT);
			}
			return *_bakedGlyphs;
		}

		HashMap<StringHash, Handle<VectorAsset>> _vectors{GetMemoryResource(MemTag::Editor)};

		struct ComponentIconEntry {
			str markPath;
			const void* owner = nullptr;
		};

		struct ComponentIconState {
			HashMap<StringHash, ComponentIconEntry> icons{GetMemoryResource(MemTag::Editor)};
			bool seeded = false;
		};
		ComponentIconState* _componentIcons = nullptr;

		ComponentIconState& ComponentIconTable() {
			if (!_componentIcons) {
				_componentIcons = New<ComponentIconState>(MemTag::Editor);
			}
			return *_componentIcons;
		}

		void ApplyLineBox(UI::Context& context, UI::ElementHandle element, f32 lineTextPx) {
			f32 height = LineBoxHeight(lineTextPx);
			if (height <= 0.0f) {
				return;
			}
			UI::Style style = context.GetStyle(element);
			style.minHeight = UI::Length::Pixel(height);
			style.textAlignVertical = UI::Align::Center;
			context.SetStyle(element, style);
		}

	} // anonymous namespace

	Handle<VectorAsset> GetGlyph(Icon icon) {
		const i32 index = FindBaked(icon);
		if (index < 0) {
			return {};
		}
		BakedGlyphState& table = GlyphTable();
		Handle<VectorAsset>& slot = table.assets[static_cast<u32>(index)];
		if (slot.IsValid()) {
			return slot;
		}

		const Baked::Glyph& baked = Baked::GLYPHS[index];
		VectorAsset* asset = New<VectorAsset>(MemTag::Editor);
		asset->viewSize = {baked.viewW, baked.viewH};
		asset->verbs.reserve(baked.verbCount);
		for (u32 i = 0; i < baked.verbCount; ++i) {
			asset->verbs.push_back(static_cast<VectorVerb>(Baked::VERBS[baked.firstVerb + i]));
		}
		asset->points.reserve(baked.pointCount);
		for (u32 i = 0; i < baked.pointCount; ++i) {
			const f32* xy = Baked::POINTS + static_cast<size_t>(baked.firstPoint + i) * 2;
			asset->points.push_back(Vec2{xy[0], xy[1]});
		}
		asset->slotDefaults.push_back(Color::White());
		VectorShape shape;
		shape.firstVerb = 0;
		shape.verbCount = baked.verbCount;
		shape.firstPoint = 0;
		shape.pointCount = baked.pointCount;
		shape.slot = 0;
		shape.opacity = 1.0f;
		shape.mask = INVALID_VECTOR_INDEX;
		asset->shapes.push_back(shape);

		slot = Handle<VectorAsset>(asset);
		return slot;
	}

	f32 GlyphSizeScale(Icon icon) {
		const i32 index = FindBaked(icon);
		return index < 0 ? 1.0f : Baked::GLYPHS[index].sizeScale;
	}

	bool IsAvailable(Face face) {
		const u32 want = static_cast<u32>(face);
		if (want >= FACE_COUNT) {
			return false;
		}
		static bool computed = false;
		static bool present[FACE_COUNT] = {};
		if (!computed) {
			computed = true;
			for (u32 i = 0; i < Baked::GLYPH_COUNT; ++i) {
				if (Baked::GLYPHS[i].face < FACE_COUNT) {
					present[Baked::GLYPHS[i].face] = true;
				}
			}
		}
		return present[want];
	}

	void InstallWidgetMarks() {
		Handle<VectorAsset> caret = GetGlyph(Glyph::CaretDown);
		if (!caret.IsValid()) {
			return;
		}
		UI::Widgets::SetDropdownMark(caret, GlyphSizeScale(Glyph::CaretDown));
	}

	void Shutdown() {
		UI::Widgets::SetDropdownMark({}, 0.0f);
		if (_bakedGlyphs) {
			for (Handle<VectorAsset>& glyph : _bakedGlyphs->assets) {
				Delete(glyph.Get());
			}
			Delete(_bakedGlyphs);
			_bakedGlyphs = nullptr;
		}
		for (auto& [path, vector] : _vectors) {
			if (Asset* asset = vector.Get()) {
				AssetDatabase::UnloadAsset(asset);
			}
		}
		HashMap<StringHash, Handle<VectorAsset>> empty{GetMemoryResource(MemTag::Editor)};
		_vectors.swap(empty);

		Delete(_componentIcons);
		_componentIcons = nullptr;
	}

	Handle<VectorAsset> GetVector(std::string_view enginePath) {
		const StringHash key(enginePath);
		auto it = _vectors.find(key);
		if (it != _vectors.end()) {
			return it->second;
		}

		Handle<VectorAsset> handle;
		Asset* asset = AssetDatabase::LoadAsset(Path(str(enginePath)));
		if (asset && asset->GetAssetTypeId() == StringHash("VectorAsset")) {
			handle = Handle<VectorAsset>(static_cast<VectorAsset*>(asset));
		} else {
			if (asset) {
				AssetDatabase::UnloadAsset(asset);
			}
			SLT_LOG_WARN("Editor", "vector mark {} unavailable", enginePath);
		}
		_vectors.emplace(key, handle);
		return handle;
	}

	UI::ElementHandle AddVector(UI::Context& context, UI::ElementHandle parent, std::string_view enginePath,
								const UI::IconPalette& palette, f32 sizePx, f32 gapPx, f32 lineTextPx) {
		UI::ElementHandle element = context.CreateElement();
		UI::Style style;
		style.margin = {0.0f, 0.0f, gapPx, 0.0f};
		style.flexShrink = 0.0f;
		context.SetStyle(element, style);
		context.SetHoverable(element, false);
		context.AppendChild(parent, element);
		ApplyLineBox(context, element, lineTextPx);

		Handle<VectorAsset> asset = GetVector(enginePath);
		if (asset.IsValid()) {
			context.SetIcon(element, asset, sizePx, palette);
		}
		return element;
	}

	LogLevelFace FaceForLogLevel(LogLevel level) {
		using EditorTheme::ColorToken;
		switch (level) {
		case LogLevel::Trace:
			return {"TRACE", Glyph::LevelTrace, EditorTheme::GetColor(ColorToken::TextFaint)};
		case LogLevel::Debug:
			return {"DEBUG", Glyph::LevelDebug, EditorTheme::GetColor(ColorToken::LogDebug)};
		case LogLevel::Warn:
			return {"WARN", Glyph::LevelWarn, EditorTheme::GetColor(ColorToken::StatusWarn)};
		case LogLevel::Error:
			return {"ERROR", Glyph::LevelError, EditorTheme::GetColor(ColorToken::StatusError)};
		case LogLevel::Fatal:
			return {"FATAL", Glyph::LevelFatal, EditorTheme::GetColor(ColorToken::StatusFatal)};
		default:
			return {"INFO", Glyph::LevelInfo, EditorTheme::GetColor(ColorToken::LogInfo)};
		}
	}

	f32 GutterSizePx() {
		return EditorTheme::GetMetric(EditorTheme::MetricToken::IconGutter);
	}

	f32 BodySizePx() {
		return EditorTheme::GetMetric(EditorTheme::MetricToken::IconBody);
	}

	f32 GutterGapPx() {
		return EditorTheme::GetMetric(EditorTheme::MetricToken::IconGap);
	}

	f32 TabSizePx() {
		return EditorTheme::GetMetric(EditorTheme::MetricToken::IconTab);
	}

	f32 LineBoxHeight(f32 textSizePx) {
		UI::IFontSystem* fonts = UISystem::GetFontSystem();
		Handle<FontAsset> text = UISystem::GetDefaultFont();
		if (!fonts || !text.IsValid() || textSizePx <= 0.0f) {
			return 0.0f;
		}
		return fonts->GetLineHeight(text, textSizePx);
	}

	void SetGlyph(UI::Context& context, UI::ElementHandle element, Icon icon, Color color, f32 sizePx) {
		if (!context.IsAlive(element)) {
			return;
		}
		if (icon.glyph.empty()) {
			context.ClearText(element);
			context.ClearIcon(element);
			return;
		}
		Handle<VectorAsset> glyph = GetGlyph(icon);
		if (!glyph.IsValid()) {
			return;
		}
		context.ClearText(element);
		context.SetIcon(element, glyph, sizePx * GlyphSizeScale(icon), UI::IconPalette::Tint(color));
	}

	bool SetGlyphOrText(UI::Context& context, UI::ElementHandle element, Icon icon,
						std::string_view fallbackText, Color color, f32 iconSizePx, f32 textSizePx) {
		if (!context.IsAlive(element)) {
			return false;
		}
		ApplyLineBox(context, element, textSizePx);

		Handle<VectorAsset> glyph = icon.glyph.empty() ? Handle<VectorAsset>{} : GetGlyph(icon);
		if (glyph.IsValid()) {
			context.ClearText(element);
			context.SetIcon(element, glyph, iconSizePx * GlyphSizeScale(icon), UI::IconPalette::Tint(color));
			return true;
		}
		context.ClearIcon(element);
		context.SetText(element, UISystem::GetDefaultFont(), textSizePx, color, fallbackText);
		return false;
	}

	UI::ElementHandle Add(UI::Context& context, UI::ElementHandle parent, Icon icon,
						  Color color, f32 sizePx, f32 gapPx, f32 lineTextPx) {
		UI::ElementHandle element = context.CreateElement();
		UI::Style style;
		style.margin = {0.0f, 0.0f, gapPx, 0.0f};
		style.flexShrink = 0.0f;
		context.SetStyle(element, style);
		context.SetHoverable(element, false);
		context.AppendChild(parent, element);
		ApplyLineBox(context, element, lineTextPx);
		SetGlyph(context, element, icon, color, sizePx);
		return element;
	}

	UI::ElementHandle Prefix(UI::Context& context, UI::ElementHandle widget, Icon icon,
							 Color color, f32 sizePx, f32 gapPx, f32 lineTextPx) {
		const UI::Element* pod = context.GetElement(widget);
		UI::ElementHandle label = pod ? context.MakeHandle(pod->firstChild) : UI::ElementHandle{};
		UI::ElementHandle element = Add(context, widget, icon, color, sizePx, gapPx, lineTextPx);
		if (context.IsAlive(label)) {
			context.AppendChild(widget, label);
		}
		return element;
	}

	IconButtonParts IconButton(UI::Context& context, UI::ElementHandle parent, Icon icon,
							   std::string_view fallbackLabel, std::string_view tooltip,
							   std::function<void()> onClick, f32 textSizePx) {
		UI::Widgets::ButtonDesc desc;
		desc.label = fallbackLabel;
		desc.font = UISystem::GetDefaultFont();
		desc.fontSizePx = textSizePx;
		desc.onClick = std::move(onClick);

		IconButtonParts parts;
		parts.button = UI::Widgets::Button(context, parent, desc);
		const UI::Element* pod = context.GetElement(parts.button);
		parts.face = pod ? context.MakeHandle(pod->firstChild) : UI::ElementHandle{};

		SetGlyphOrText(context, parts.face, icon, fallbackLabel, EditorTheme::GetColor(EditorTheme::ColorToken::Text), textSizePx, textSizePx);
		context.SetTooltip(parts.button, tooltip);
		return parts;
	}

	namespace {

		void PaintRowCaret(UI::Context& context, const UI::Widgets::TreeRowParts& row,
						   bool hasChildren, bool expanded, Color caretColor) {
			if (!IsAvailable(Glyph::CaretRight.face)) {
				return;
			}
			UI::Style style = context.GetStyle(row.arrow);
			style.margin.right = GutterGapPx();
			context.SetStyle(row.arrow, style);
			SetGlyph(context, row.arrow,
					 hasChildren ? (expanded ? Glyph::CaretDown : Glyph::CaretRight) : Icon{},
					 caretColor, GutterSizePx());
		}

		f32 RowTextPx() {
			return EditorTheme::GetMetric(EditorTheme::MetricToken::FontRow);
		}

		UI::ElementHandle PlaceRowPicture(UI::Context& context, const UI::Widgets::TreeRowParts& row,
										  UI::ElementHandle element, f32 columnPx) {
			if (columnPx > 0.0f) {
				UI::Style style = context.GetStyle(element);
				style.width = UI::Length::Pixel(columnPx);
				style.minHeight = UI::Length::Pixel(std::max(columnPx, LineBoxHeight(RowTextPx())));
				style.textAlign = UI::Align::Center;
				style.textAlignVertical = UI::Align::Center;
				context.SetStyle(element, style);
			}
			if (context.IsAlive(row.labelElement)) {
				context.AppendChild(row.row, row.labelElement);
			}
			return element;
		}

	} // anonymous namespace

	UI::ElementHandle SetRowGutter(UI::Context& context, const UI::Widgets::TreeRowParts& row,
								   bool hasChildren, bool expanded, Color caretColor,
								   Icon icon, Color iconColor, f32 columnPx) {
		if (!context.IsAlive(row.arrow)) {
			return {};
		}
		PaintRowCaret(context, row, hasChildren, expanded, caretColor);

		if (icon.glyph.empty()) {
			return {};
		}
		UI::ElementHandle element = Add(context, row.row, icon, iconColor, GutterSizePx(), GutterGapPx(), RowTextPx());
		return PlaceRowPicture(context, row, element, columnPx);
	}

	UI::ElementHandle SetRowGutterMark(UI::Context& context, const UI::Widgets::TreeRowParts& row,
									   bool hasChildren, bool expanded, Color caretColor,
									   std::string_view markPath, f32 sizePx, f32 columnPx) {
		if (!context.IsAlive(row.arrow)) {
			return {};
		}
		PaintRowCaret(context, row, hasChildren, expanded, caretColor);
		if (markPath.empty()) {
			return {};
		}
		UI::ElementHandle element = AddVector(context, row.row, markPath, UI::IconPalette{}, sizePx, GutterGapPx(), RowTextPx());
		return PlaceRowPicture(context, row, element, columnPx);
	}

	namespace ComponentIcons {

		namespace {

			void Plant(StringHash typeName, std::string_view markPath, bool onlyAbsent) {
				if (onlyAbsent && ComponentIconTable().icons.contains(typeName)) {
					return;
				}
				Register(typeName, markPath);
			}

			void SeedBuiltIns(bool onlyAbsent = false) {
				ComponentIconState& table = ComponentIconTable();
				if (table.seeded && !onlyAbsent) {
					return;
				}
				table.seeded = true;
				Plant(Component::GetReflectType()->name, Mark::Component, onlyAbsent);
				Plant(Behavior::GetReflectType()->name, Mark::Behavior, onlyAbsent);
				Plant(UnresolvedComponent::GetReflectType()->name, Mark::MissingComponent, onlyAbsent);
				Plant(Camera::GetReflectType()->name, Mark::Camera, onlyAbsent);
				Plant(Light::GetReflectType()->name, Mark::Light, onlyAbsent);
				Plant(MeshRenderer::GetReflectType()->name, Mark::MeshRenderer, onlyAbsent);
				Plant(SkinnedMeshRenderer::GetReflectType()->name, Mark::SkinnedMeshRenderer, onlyAbsent);
				Plant(Skeleton::GetReflectType()->name, Mark::Skeleton, onlyAbsent);
				Plant(Environment::GetReflectType()->name, Mark::Environment, onlyAbsent);
				Plant(ReflectionProbe::GetReflectType()->name, Mark::ReflectionProbe, onlyAbsent);
				Plant(UIPanelRenderer::GetReflectType()->name, Mark::UIPanelRenderer, onlyAbsent);
				Plant(ProceduralRenderer::GetReflectType()->name, Mark::ProceduralRenderer, onlyAbsent);
			}

		} // anonymous namespace

		void RegisterBuiltIns() {
			SeedBuiltIns();
		}

		void Register(StringHash componentTypeName, std::string_view markPath, const void* owner) {
			if (markPath.empty()) {
				return;
			}
			ComponentIconTable().icons[componentTypeName] = {str(markPath), owner};
		}

		u32 UnregisterOwnedBy(const void* owner) {
			if (!owner) {
				return 0;
			}
			if (!_componentIcons) {
				return 0;
			}
			u32 removed = 0;
			auto& icons = _componentIcons->icons;
			for (auto it = icons.begin(); it != icons.end();) {
				if (it->second.owner == owner) {
					it = icons.erase(it);
					++removed;
				} else {
					++it;
				}
			}
			if (removed > 0) {
				SeedBuiltIns(true);
			}
			return removed;
		}

		std::string_view For(const TypeInfo* componentType) {
			if (!componentType) {
				return {};
			}
			SeedBuiltIns();
			const auto& icons = ComponentIconTable().icons;
			for (const TypeInfo* type = componentType; type != nullptr; type = type->parent) {
				auto it = icons.find(type->name);
				if (it != icons.end()) {
					return it->second.markPath;
				}
				if (type == Component::GetReflectType()) {
					break;
				}
			}
			return {};
		}

		f32 SizePx() {
			return EditorTheme::GetMetric(EditorTheme::MetricToken::MarkComponent);
		}

		UI::ElementHandle Add(UI::Context& context, UI::ElementHandle parent, const TypeInfo* componentType,
							  f32 sizePx, f32 gapPx, f32 lineTextPx) {
			std::string_view mark = For(componentType);
			if (mark.empty()) {
				return {};
			}
			return AddVector(context, parent, mark, UI::IconPalette{},
							 sizePx > 0.0f ? sizePx : SizePx(),
							 gapPx > 0.0f ? gapPx : GutterGapPx(), lineTextPx);
		}

	} // namespace ComponentIcons

} // namespace Shalltear::EditorIcons
