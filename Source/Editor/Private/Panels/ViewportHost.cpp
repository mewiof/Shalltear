#include "ViewportHost.h"

#include "Shalltear/Core/UI/RenderList.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"

namespace Shalltear {

	namespace {
		constexpr f32 RESIZE_DEBOUNCE_SECONDS = 0.25f;
	} // anonymous namespace

	UI::ElementHandle ViewportHost::Build(UI::Context& context, UI::ElementHandle parent) {
		_element = context.CreateElement();
		context.AppendChild(parent, _element);

		UI::Style style;
		style.width = UI::Length::Percent(1.0f);
		style.height = UI::Length::Pixel(0.0f);
		style.flexGrow = 1.0f;
		context.SetStyle(_element, style);
		context.AddClass(_element, EditorTheme::Class::ViewportPlaceholder);
		context.SetFocusable(_element, true);

		context.Events(_element).customDraw =
			[this](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				const UI::Rect area = UI::SnapRectToPixels(c.GetLayout(element).content);
				if (area.w <= 1.0f || area.h <= 1.0f) {
					return;
				}
				RHI::TextureHandle texture = RenderSystem::GetViewTargetTexture(_target);
				if (texture.IsValid()) {
					paint.EmitTexturedQuad(area, texture, {0.0f, 0.0f, 1.0f, 1.0f}, Color::White(),
										   UI::SamplerKind::LinearClamp);
				}
				if (overlay) {
					overlay(c, paint, area);
				}
			};

		return _element;
	}

	void ViewportHost::Tick(UI::Context& context, f32 deltaSeconds) {
		if (!context.IsAlive(_element)) {
			_onScreen = false;
			return;
		}
		const UI::Rect content = context.GetLayout(_element).content;
		Vec2I wanted = EditorViewportOps::ViewTargetSizeFor(content);
		if (!EditorViewportOps::ViewportIsOnScreen(content)) {
			_onScreen = false;
			return;
		}
		_onScreen = true;

		if (_target == INVALID_VIEW_TARGET) {
			_target = RenderSystem::CreateViewTarget(wanted);
			_size = _target != INVALID_VIEW_TARGET ? wanted : Vec2I{};
			_pendingSize = wanted;
			_resizeDelay = 0.0f;
			return;
		}

		if (wanted == _size) {
			_resizeDelay = 0.0f;
			_pendingSize = wanted;
			return;
		}
		if (wanted != _pendingSize) {
			_pendingSize = wanted;
			_resizeDelay = 0.0f;
			return;
		}
		_resizeDelay += deltaSeconds;
		if (_resizeDelay >= RESIZE_DEBOUNCE_SECONDS) {
			RenderSystem::ResizeViewTarget(_target, wanted);
			_size = RenderSystem::GetViewTargetSize(_target);
			_resizeDelay = 0.0f;
		}
	}

	void ViewportHost::Release() {
		if (_target != INVALID_VIEW_TARGET) {
			RenderSystem::DestroyViewTarget(_target);
			_target = INVALID_VIEW_TARGET;
		}
		_size = {};
		_onScreen = false;
	}

	UI::Rect ViewportHost::GetRect(UI::Context& context) const {
		if (!context.IsAlive(_element)) {
			return {};
		}
		return context.GetLayout(_element).content;
	}

	Vec2 ViewportHost::ToLocal(UI::Context& context, Vec2 contextPx) const {
		UI::Rect area = GetRect(context);
		return {contextPx.x - area.x, contextPx.y - area.y};
	}

} // namespace Shalltear
