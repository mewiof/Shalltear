#include "InspectorPanel.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/SpriteEditorOps.h"

#include <array>
#include <cmath>
#include <format>
#include <memory>

namespace Shalltear {

	namespace {

		using namespace UI;
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;
		namespace Ops = SpriteEditorOps;

		constexpr std::array<std::string_view, 3> SLICE_MODES{"Grid By Cell Size", "Grid By Cell Count", "Automatic"};

		struct SpriteDraft {
			Guid asset;
			str baseName;
			Vector<TextureSprite> sprites{GetMemoryResource(MemTag::Editor)};
			Vector<TextureSprite> saved{GetMemoryResource(MemTag::Editor)};
			i32 selected = -1;
			Ops::CanvasView view;
			Ops::Grab grab;
			RectI grabStart{};
			Vec2 pressPoint{};
			Vec2 pointNow{};
			Ops::SliceDesc slice;
			Widgets::VectorFieldParts positionFields{};
			Widgets::VectorFieldParts sizeFields{};
			bool hasRectFields = false;
			bool canvasFocused = false;
		};
		SpriteDraft* _draft = nullptr;

		f32 BodyPx() { return EditorTheme::GetMetric(MetricToken::FontBody); }

		SpriteDraft* DraftOf(const Guid& guid) { return _draft && _draft->asset == guid ? _draft : nullptr; }

		TextureSprite* Selected(SpriteDraft& draft) {
			return draft.selected >= 0 && static_cast<size_t>(draft.selected) < draft.sprites.size()
					   ? &draft.sprites[static_cast<size_t>(draft.selected)]
					   : nullptr;
		}

		SpriteDraft& DraftFor(const Guid& guid, const TextureAsset& texture, const Path& path) {
			if (!_draft) {
				_draft = New<SpriteDraft>(MemTag::Editor);
			}
			if (_draft->asset != guid) {
				_draft->asset = guid;
				_draft->sprites.assign(texture.sprites.begin(), texture.sprites.end());
				_draft->saved.assign(texture.sprites.begin(), texture.sprites.end());
				_draft->selected = _draft->sprites.empty() ? -1 : 0;
				_draft->view = {};
				_draft->grab = {};
				str stem = path.GetFilename();
				if (size_t dot = stem.find_last_of('.'); dot != str::npos) {
					stem.erase(dot);
				}
				_draft->baseName = stem.empty() ? str("Sprite") : stem;
			}
			_draft->hasRectFields = false;
			return *_draft;
		}

		ElementHandle AddRow(Context& context, ElementHandle parent) {
			ElementHandle row = context.CreateElement();
			context.AppendChild(parent, row);
			Style style;
			style.flexDirection = FlexDirection::Row;
			style.alignItems = Align::Center;
			style.width = Length::Percent(1.0f);
			style.padding = {8.0f, 0.0f, 8.0f, 0.0f};
			style.margin = {0.0f, 2.0f, 0.0f, 2.0f};
			context.SetStyle(row, style);
			return row;
		}

		ElementHandle AddLabeledRow(Context& context, ElementHandle parent, std::string_view text) {
			ElementHandle row = AddRow(context, parent);
			ElementHandle label = context.CreateElement();
			context.AppendChild(row, label);
			Style style;
			style.width = Length::Pixel(90.0f);
			style.flexShrink = 0.0f;
			context.SetStyle(label, style);
			context.SetHoverable(label, false);
			context.SetText(label, UISystem::GetDefaultFont(), BodyPx(), EditorTheme::GetColor(ColorToken::WidgetTextDim), text);
			return row;
		}

		void AddLine(Context& context, ElementHandle parent, std::string_view text) {
			ElementHandle line = context.CreateElement();
			context.AppendChild(parent, line);
			Style style;
			style.margin = {8.0f, 4.0f, 8.0f, 2.0f};
			context.SetStyle(line, style);
			context.SetHoverable(line, false);
			context.SetText(line, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
							EditorTheme::GetColor(ColorToken::WidgetTextDim), text);
		}

		void Grow(Context& context, ElementHandle element) {
			Style style = context.GetStyle(element);
			style.flexGrow = 1.0f;
			style.flexShrink = 1.0f;
			context.SetStyle(element, style);
		}

		Widgets::VectorFieldParts AddInts(Context& context, ElementHandle row, std::initializer_list<i32> values,
										  std::array<const char*, 4> labels, std::function<void(u32, i32)> onChange) {
			Widgets::VectorFieldDesc desc;
			desc.componentCount = static_cast<u32>(values.size());
			u32 i = 0;
			for (i32 value : values) {
				desc.values[i] = value;
				desc.labels[i] = labels[i];
				++i;
			}
			desc.decimals = 0;
			desc.dragScale = 0.25;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = BodyPx();
			desc.onChange = [onChange](u32 component, f64 value) { onChange(component, static_cast<i32>(std::lround(value))); };
			Widgets::VectorFieldParts parts = Widgets::VectorField(context, row, desc);
			Grow(context, parts.root);
			return parts;
		}

		void ShowRectFields(Context& context, SpriteDraft& draft) {
			const TextureSprite* sprite = Selected(draft);
			if (!draft.hasRectFields || !sprite) {
				return;
			}
			const f64 position[2] = {static_cast<f64>(sprite->rect.position.x), static_cast<f64>(sprite->rect.position.y)};
			const f64 size[2] = {static_cast<f64>(sprite->rect.size.x), static_cast<f64>(sprite->rect.size.y)};
			Widgets::SetVectorFieldValues(context, draft.positionFields, 2, position, 0);
			Widgets::SetVectorFieldValues(context, draft.sizeFields, 2, size, 0);
		}

		f32 HandlePx(const Context& context) { return EditorTheme::GetMetric(MetricToken::SpriteHandle) * context.GetContentScale(); }

		void PaintCanvas(Context& context, ElementHandle canvas, PaintScope& paint, const Handle<TextureAsset>& subject) {
			const Rect box = context.GetLayout(canvas).content;
			paint.EmitQuad(box, EditorTheme::GetColor(ColorToken::Well));
			const TextureAsset* texture = subject.Get();
			SpriteDraft* owned = texture ? DraftOf(texture->assetId) : nullptr;
			if (!owned) {
				return;
			}
			SpriteDraft& draft = *owned;
			draft.view.box = box;
			draft.view.imageSize = texture->size;
			if (texture->texture.IsValid()) {
				const SamplerKind sampler = texture->settings.filter == RHI::FilterMode::Nearest ? SamplerKind::NearestClamp : SamplerKind::LinearClamp;
				paint.EmitTexturedQuad(draft.view.ToRect(RectI(0, 0, texture->size.x, texture->size.y)), texture->texture, {0.0f, 0.0f, 1.0f, 1.0f},
									   Color::White(), sampler);
			}

			const f32 line = std::max(1.0f, std::round(context.GetContentScale()));
			const Color outline = EditorTheme::GetColor(ColorToken::EdgeBright);
			for (size_t i = 0; i < draft.sprites.size(); ++i) {
				if (static_cast<i32>(i) != draft.selected) {
					paint.EmitRing(draft.view.ToRect(draft.sprites[i].rect), Edges::All(line), outline);
				}
			}

			if (const TextureSprite* sprite = Selected(draft)) {
				const Rect r = draft.view.ToRect(sprite->rect);
				const f32 scale = draft.view.Scale();
				const Color guide = EditorTheme::GetColor(ColorToken::StatusOk);
				const SpriteBorder& b = sprite->border;
				if (b.left > 0) {
					paint.EmitLine({r.x + b.left * scale, r.y}, {r.x + b.left * scale, r.y + r.h}, line, guide);
				}
				if (b.right > 0) {
					paint.EmitLine({r.x + r.w - b.right * scale, r.y}, {r.x + r.w - b.right * scale, r.y + r.h}, line, guide);
				}
				if (b.top > 0) {
					paint.EmitLine({r.x, r.y + b.top * scale}, {r.x + r.w, r.y + b.top * scale}, line, guide);
				}
				if (b.bottom > 0) {
					paint.EmitLine({r.x, r.y + r.h - b.bottom * scale}, {r.x + r.w, r.y + r.h - b.bottom * scale}, line, guide);
				}
				const Color accent = EditorTheme::GetColor(ColorToken::Accent);
				paint.EmitRing(r, Edges::All(line * 2.0f), accent);
				const f32 handle = HandlePx(context);
				for (f32 fx : {0.0f, 0.5f, 1.0f}) {
					for (f32 fy : {0.0f, 0.5f, 1.0f}) {
						if (fx == 0.5f && fy == 0.5f) {
							continue;
						}
						paint.EmitQuad({r.x + r.w * fx - handle * 0.5f, r.y + r.h * fy - handle * 0.5f, handle, handle}, accent);
					}
				}
			}

			if (draft.grab.kind == Ops::GrabKind::Create) {
				const RectI span = Ops::SpanRect(draft.view.ToTexel(draft.pressPoint), draft.view.ToTexel(draft.pointNow), draft.view.imageSize);
				if (span.size.x > 0 && span.size.y > 0) {
					const Rect r = draft.view.ToRect(span);
					paint.EmitQuad(r, EditorTheme::GetColor(ColorToken::GraphMarquee));
					paint.EmitRing(r, Edges::All(line), EditorTheme::GetColor(ColorToken::GraphMarqueeEdge));
				}
			}
		}

		void BuildCanvas(Context& context, ElementHandle parent, const Handle<TextureAsset>& subject, const std::function<void()>& rebuild) {
			ElementHandle canvas = context.CreateElement();
			context.AppendChild(parent, canvas);
			Style style;
			style.width = Length::Percent(1.0f);
			style.height = Length::Pixel(EditorTheme::GetMetric(MetricToken::SpriteCanvasHeight));
			style.overflow = Overflow::Hidden;
			style.margin = {0.0f, 2.0f, 0.0f, 4.0f};
			context.SetStyle(canvas, style);
			context.SetFocusable(canvas, true);
			context.AddClass(canvas, EditorTheme::Class::Clickable);
			if (_draft && _draft->canvasFocused) {
				context.SetFocus(canvas);
				_draft->canvasFocused = false;
			}

			Context* const contextPtr = &context;
			const Guid guid = subject.Get() ? subject->assetId : Guid{};
			ElementEvents& events = context.Events(canvas);
			events.customDraw = [subject](Context& c, ElementHandle element, PaintScope& paint) { PaintCanvas(c, element, paint, subject); };
			events.onMouseDown = [contextPtr, canvas, guid]() {
				if (!DraftOf(guid)) {
					return;
				}
				contextPtr->SetFocus(canvas);
				SpriteDraft& draft = *_draft;
				const Vec2 point = contextPtr->GetPointerPosition();
				draft.pressPoint = point;
				draft.pointNow = point;
				draft.grab = Ops::Pick(draft.view, draft.sprites, draft.selected, point, HandlePx(*contextPtr));
				if (draft.grab.sprite >= 0) {
					draft.selected = draft.grab.sprite;
					draft.grabStart = draft.sprites[static_cast<size_t>(draft.selected)].rect;
				}
			};
			events.onPressDrag = [contextPtr, guid](Vec2 point) {
				if (!DraftOf(guid)) {
					return;
				}
				SpriteDraft& draft = *_draft;
				draft.pointNow = point;
				TextureSprite* sprite = Selected(draft);
				if (!sprite || (draft.grab.kind != Ops::GrabKind::Move && draft.grab.kind != Ops::GrabKind::Resize)) {
					return;
				}
				const Vec2 from = draft.view.ToTexel(draft.pressPoint);
				const Vec2 to = draft.view.ToTexel(point);
				const Vec2I delta(static_cast<i32>(std::lround(to.x - from.x)), static_cast<i32>(std::lround(to.y - from.y)));
				sprite->rect = Ops::Drag(draft.grabStart, draft.grab, delta, draft.view.imageSize);
				sprite->border = Ops::ClampBorder(sprite->border, sprite->rect.size);
				ShowRectFields(*contextPtr, draft);
			};
			events.onPressEnd = [rebuild, guid]() {
				if (!DraftOf(guid)) {
					return;
				}
				SpriteDraft& draft = *_draft;
				if (draft.grab.kind == Ops::GrabKind::Create) {
					const RectI span = Ops::SpanRect(draft.view.ToTexel(draft.pressPoint), draft.view.ToTexel(draft.pointNow), draft.view.imageSize);
					if (span.size.x > 0 && span.size.y > 0) {
						TextureSprite sprite;
						sprite.id = Ops::MintId();
						sprite.name = Ops::UniqueName(draft.sprites, draft.baseName);
						sprite.rect = span;
						draft.sprites.push_back(std::move(sprite));
						draft.selected = static_cast<i32>(draft.sprites.size()) - 1;
					} else {
						draft.selected = -1;
					}
				}
				draft.grab = {};
				draft.canvasFocused = true;
				rebuild();
			};
			events.onWheel = [contextPtr, guid](Vec2 notches) {
				if (!DraftOf(guid) || notches.y == 0.0f) {
					return false;
				}
				Ops::ZoomAt(_draft->view, contextPtr->GetPointerPosition(), notches.y);
				return true;
			};
			events.onMiddleDrag = [guid](Vec2 deltaPx) {
				if (DraftOf(guid)) {
					_draft->view.pan.x += deltaPx.x;
					_draft->view.pan.y += deltaPx.y;
				}
			};
			events.onKeyDown = [rebuild, guid](Key key, KeyMod mods) {
				if (!DraftOf(guid) || mods != KeyMod::None) {
					return false;
				}
				if (key == Key::F) {
					_draft->view.zoom = 1.0f;
					_draft->view.pan = {};
					return true;
				}
				if ((key == Key::Delete || key == Key::Backspace) && Selected(*_draft)) {
					_draft->sprites.erase(_draft->sprites.begin() + _draft->selected);
					_draft->selected = std::min(_draft->selected, static_cast<i32>(_draft->sprites.size()) - 1);
					_draft->canvasFocused = true;
					rebuild();
					return true;
				}
				return false;
			};
		}

		void BuildSliceRows(Context& context, ElementHandle parent, const Handle<TextureAsset>& subject, const std::function<void()>& rebuild) {
			SpriteDraft& draft = *_draft;
			ElementHandle mode = AddLabeledRow(context, parent, "Slice");
			Widgets::DropdownDesc modes;
			modes.items = SLICE_MODES;
			modes.selected = static_cast<u32>(draft.slice.mode);
			modes.font = UISystem::GetDefaultFont();
			modes.fontSizePx = BodyPx();
			modes.onSelect = [rebuild](u32 index) {
				if (_draft) {
					_draft->slice.mode = static_cast<Ops::SliceMode>(std::min<u32>(index, 2));
					rebuild();
				}
			};
			Grow(context, Widgets::Dropdown(context, mode, modes).root);

			Widgets::ButtonDesc slice;
			slice.label = "Slice";
			slice.font = UISystem::GetDefaultFont();
			slice.fontSizePx = BodyPx();
			slice.onClick = [subject, rebuild]() {
				const TextureAsset* texture = subject.Get();
				if (!_draft || !texture) {
					return;
				}
				Vector<u8> texels{GetMemoryResource(MemTag::Editor)};
				const bool readable = Ops::DecodeTexels(*texture, texels);
				if (!readable && _draft->slice.mode == Ops::SliceMode::Automatic) {
					SLT_LOG_WARN("Editor", "'{}': automatic slicing needs readable texels (this format has no decoder)", texture->virtualPath);
					return;
				}
				Vector<RectI> rects{GetMemoryResource(MemTag::Editor)};
				Ops::Slice(_draft->slice, texture->size, readable ? texels.data() : nullptr, rects);
				Ops::MergeSlices(_draft->sprites, rects, _draft->baseName);
				if (!Selected(*_draft)) {
					_draft->selected = _draft->sprites.empty() ? -1 : 0;
				}
				rebuild();
			};
			Widgets::Button(context, mode, slice);

			if (draft.slice.mode == Ops::SliceMode::Automatic) {
				return;
			}
			const bool bySize = draft.slice.mode == Ops::SliceMode::CellSize;
			ElementHandle cell = AddLabeledRow(context, parent, bySize ? "Pixel Size" : "Column & Row");
			AddInts(context, cell, {draft.slice.cell.x, draft.slice.cell.y}, {bySize ? "W" : "C", bySize ? "H" : "R", "", ""}, [](u32 component, i32 value) {
				if (_draft) {
					(component == 0 ? _draft->slice.cell.x : _draft->slice.cell.y) = std::max(1, value);
				}
			});
			ElementHandle offset = AddLabeledRow(context, parent, "Offset");
			AddInts(context, offset, {draft.slice.offset.x, draft.slice.offset.y}, {"X", "Y", "", ""}, [](u32 component, i32 value) {
				if (_draft) {
					(component == 0 ? _draft->slice.offset.x : _draft->slice.offset.y) = std::max(0, value);
				}
			});
			ElementHandle padding = AddLabeledRow(context, parent, "Padding");
			AddInts(context, padding, {draft.slice.padding.x, draft.slice.padding.y}, {"X", "Y", "", ""}, [](u32 component, i32 value) {
				if (_draft) {
					(component == 0 ? _draft->slice.padding.x : _draft->slice.padding.y) = std::max(0, value);
				}
			});
		}

		void BuildSelectedRows(Context& context, ElementHandle parent, const std::function<void()>& rebuild) {
			SpriteDraft& draft = *_draft;
			TextureSprite* sprite = Selected(draft);
			if (!sprite) {
				AddLine(context, parent, draft.sprites.empty() ? "Drag on the image to mark a sprite (or slice it)" : "Click a sprite to edit it");
				return;
			}

			ElementHandle name = AddLabeledRow(context, parent, "Name");
			Widgets::TextFieldDesc field;
			field.text = sprite->name;
			field.font = UISystem::GetDefaultFont();
			field.fontSizePx = BodyPx();
			field.onCommit = [rebuild](std::string_view text) {
				if (!_draft || !Selected(*_draft) || text.empty() || text == Selected(*_draft)->name) {
					return;
				}
				for (const TextureSprite& other : _draft->sprites) {
					if (other.name == text) {
						SLT_LOG_WARN("Editor", "a sprite named '{}' already exists in this texture", text);
						rebuild();
						return;
					}
				}
				Selected(*_draft)->name = str(text);
			};
			Grow(context, Widgets::TextField(context, name, field));

			Context* const contextPtr = &context;
			auto editRect = [contextPtr](u32 component, i32 value) {
				TextureSprite* edited = _draft ? Selected(*_draft) : nullptr;
				if (!edited) {
					return;
				}
				RectI next = edited->rect;
				const Vec2I image = _draft->view.imageSize;
				if (component == 0) {
					next.position.x = std::clamp(value, 0, std::max(0, image.x - next.size.x));
				} else if (component == 1) {
					next.position.y = std::clamp(value, 0, std::max(0, image.y - next.size.y));
				} else if (component == 2) {
					next.size.x = std::clamp(value, 1, std::max(1, image.x - next.position.x));
				} else {
					next.size.y = std::clamp(value, 1, std::max(1, image.y - next.position.y));
				}
				edited->rect = next;
				edited->border = Ops::ClampBorder(edited->border, next.size);
				ShowRectFields(*contextPtr, *_draft);
			};
			draft.positionFields = AddInts(context, AddLabeledRow(context, parent, "Position"), {sprite->rect.position.x, sprite->rect.position.y},
										   {"X", "Y", "", ""}, [editRect](u32 component, i32 value) { editRect(component, value); });
			draft.sizeFields = AddInts(context, AddLabeledRow(context, parent, "Size"), {sprite->rect.size.x, sprite->rect.size.y}, {"W", "H", "", ""},
									   [editRect](u32 component, i32 value) { editRect(component + 2, value); });
			draft.hasRectFields = true;

			auto editBorder = [](i32 SpriteBorder::* side, i32 value) {
				TextureSprite* edited = _draft ? Selected(*_draft) : nullptr;
				if (!edited) {
					return;
				}
				SpriteBorder next = edited->border;
				next.*side = value;
				edited->border = Ops::ClampBorder(next, edited->rect.size);
			};
			AddInts(context, AddLabeledRow(context, parent, "Border L/R"), {sprite->border.left, sprite->border.right}, {"L", "R", "", ""},
					[editBorder](u32 component, i32 value) { editBorder(component == 0 ? &SpriteBorder::left : &SpriteBorder::right, value); });
			AddInts(context, AddLabeledRow(context, parent, "Border T/B"), {sprite->border.top, sprite->border.bottom}, {"T", "B", "", ""},
					[editBorder](u32 component, i32 value) { editBorder(component == 0 ? &SpriteBorder::top : &SpriteBorder::bottom, value); });

			ElementHandle actions = AddRow(context, parent);
			Widgets::ButtonDesc remove;
			remove.label = "Delete Sprite";
			remove.font = UISystem::GetDefaultFont();
			remove.fontSizePx = BodyPx();
			remove.onClick = [rebuild]() {
				if (!_draft || !Selected(*_draft)) {
					return;
				}
				_draft->sprites.erase(_draft->sprites.begin() + _draft->selected);
				_draft->selected = std::min(_draft->selected, static_cast<i32>(_draft->sprites.size()) - 1);
				rebuild();
			};
			Widgets::Button(context, actions, remove);
		}

		void DrawTextureInspector(Context& context, ElementHandle parent, const Guid& guid, Asset& asset) {
			TextureAsset& texture = static_cast<TextureAsset&>(asset);
			const Path path = AssetDatabase::LookupPath(guid);
			const Handle<TextureAsset> subject(&texture);
			const std::function<void()> rebuild = PropertyDrawers::GetInspectorRebuilder();

			AddInspectorHeader(context, parent, "Sprites", EditorTheme::GetColor(ColorToken::TextStrong));
			if (texture.GetDimension() != RHI::TextureDimension::Texture2D) {
				AddLine(context, parent, "Sprites need a 2D texture (this one has another shape)");
				return;
			}
			if (path.ToString().empty()) {
				AddLine(context, parent, "Extract this texture to mark sprites on it");
				return;
			}
			SpriteDraft& draft = DraftFor(guid, texture, path);

			BuildCanvas(context, parent, subject, rebuild);
			AddLine(context, parent, std::format("{}x{}, {} sprite(s). Drag to mark one, wheel to zoom, middle drag to pan", texture.size.x, texture.size.y, draft.sprites.size()));
			BuildSliceRows(context, parent, subject, rebuild);
			BuildSelectedRows(context, parent, rebuild);

			ElementHandle row = AddRow(context, parent);
			Style end = context.GetStyle(row);
			end.justifyContent = Align::End;
			end.margin = {0.0f, 6.0f, 0.0f, 2.0f};
			context.SetStyle(row, end);

			Widgets::ButtonDesc revert;
			revert.label = "Revert";
			revert.font = UISystem::GetDefaultFont();
			revert.fontSizePx = BodyPx();
			revert.onClick = [rebuild]() {
				if (_draft) {
					_draft->asset = Guid{};
					rebuild();
				}
			};
			Widgets::Button(context, row, revert);

			Widgets::ButtonDesc apply;
			apply.label = "Apply";
			apply.font = UISystem::GetDefaultFont();
			apply.fontSizePx = BodyPx();
			apply.onClick = [path, rebuild]() {
				if (!_draft || _draft->sprites == _draft->saved) {
					return;
				}
				rebuild();
				const Vector<TextureSprite> sprites = _draft->sprites;
				Ops::ApplySprites(path, sprites);
				_draft->asset = Guid{};
			};
			Widgets::Button(context, row, apply);
		}

	} // anonymous namespace

	void RegisterTextureAssetInspector() {
		PropertyDrawers::Initialize();
		PropertyDrawers::RegisterAssetInspector(TextureAsset::GetReflectType()->name, &DrawTextureInspector);
	}

	void TerminateTextureAssetInspector() {
		if (_draft) {
			Delete(_draft);
			_draft = nullptr;
		}
	}

} // namespace Shalltear
