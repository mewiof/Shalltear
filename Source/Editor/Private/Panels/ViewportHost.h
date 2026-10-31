#pragma once

#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include <functional>

namespace Shalltear {

	class ViewportHost {
	public:
		~ViewportHost() { Release(); }

		UI::ElementHandle Build(UI::Context& context, UI::ElementHandle parent);

		void Tick(UI::Context& context, f32 deltaSeconds);

		void Release();

		UI::ElementHandle GetElement() const { return _element; }
		ViewTargetId GetTarget() const { return _target; }
		Vec2I GetSizePx() const { return _size; }
		bool IsReady() const { return _onScreen && _target != INVALID_VIEW_TARGET && _size.x > 0 && _size.y > 0; }
		f32 GetAspect() const { return _size.y > 0 ? static_cast<f32>(_size.x) / static_cast<f32>(_size.y) : 1.0f; }

		UI::Rect GetRect(UI::Context& context) const;
		Vec2 ToLocal(UI::Context& context, Vec2 contextPx) const;

		std::function<void(UI::Context&, UI::PaintScope&, const UI::Rect&)> overlay;
	private:
		UI::ElementHandle _element;
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Vec2I _size{};
		Vec2I _pendingSize{};
		f32 _resizeDelay = 0.0f;
		bool _onScreen = false;
	};

} // namespace Shalltear
