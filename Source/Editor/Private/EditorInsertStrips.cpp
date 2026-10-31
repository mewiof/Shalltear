#include "Shalltear/Editor/EditorInsertStrips.h"

#include "Shalltear/Core/UI/UITypes.h"

#include "Shalltear/Editor/EditorTheme.h"

#include <algorithm>
#include <span>

namespace Shalltear {

	namespace {
		constexpr f32 STRIP_HEIGHT_PX = 3.0f;
	} // namespace

	f32 EditorInsertStrips::HeightPx() {
		return STRIP_HEIGHT_PX;
	}

	void EditorInsertStrips::BeginBuild(const UI::Context& context) {
		_pending.clear();
		_tail.clear();
		_built.clear();
		_live = context.IsHoverSuppressed();
	}

	void EditorInsertStrips::PendInFlow(std::function<void(UI::Context&, UI::ElementHandle)> build) {
		_pending.push_back({false, [build = std::move(build)](UI::Context& ctx, UI::ElementHandle host, u32) {
								if (build) {
									build(ctx, host);
								}
							}});
	}

	void EditorInsertStrips::PendStrip(EditorInsertStripDesc desc) {
		_pending.push_back({true, [this, desc = std::move(desc)](UI::Context& ctx, UI::ElementHandle host, u32 above) {
								UI::ElementHandle strip = ctx.CreateElement();
								ctx.AppendChild(host, strip);

								f32 scale = std::max(0.001f, ctx.GetContentScale());
								f32 unit = UI::SnapLengthToPixels(STRIP_HEIGHT_PX, scale) / scale;
								f32 lift = unit * static_cast<f32>(above);

								UI::Style style;
								style.height = UI::Length::Pixel(unit);
								style.width = UI::Length::Percent(1.0f);
								style.margin = {0.0f, -lift, 0.0f, lift - unit};
								style.pointerEvents = _live;
								ctx.SetStyle(strip, style);
								ctx.AddClass(strip, EditorTheme::Class::InsertStrip);

								if (desc.onDragOver) {
									ctx.Events(strip).onDragOver = desc.onDragOver;
								}
								if (desc.onDrop) {
									ctx.Events(strip).onDrop = desc.onDrop;
								}
								_built.push_back(strip);
							}});
	}

	void EditorInsertStrips::Drain(std::span<const Pending> pending, UI::Context& context, UI::ElementHandle host) {
		for (size_t index = 0; index < pending.size();) {
			if (!pending[index].overlay) {
				pending[index].build(context, host, 0);
				++index;
				continue;
			}
			size_t end = index;
			while (end < pending.size() && pending[end].overlay) {
				++end;
			}
			u32 run = static_cast<u32>(end - index);
			for (size_t strip = index; strip < end; ++strip) {
				pending[strip].build(context, host, run - static_cast<u32>(strip - index));
			}
			index = end;
		}
	}

	std::function<void(UI::Context&, UI::ElementHandle)> EditorInsertStrips::Take() {
		if (_pending.empty()) {
			return {};
		}
		Vector<Pending> pending{GetMemoryResource(MemTag::Editor)};
		pending.swap(_pending);
		return [pending = std::move(pending)](UI::Context& context, UI::ElementHandle host) {
			Drain({pending.data(), pending.size()}, context, host);
		};
	}

	void EditorInsertStrips::DrainInto(UI::Context& context, UI::ElementHandle host) {
		if (!_pending.empty()) {
			_tail.clear();
			_tail.swap(_pending);
		}
		Drain({_tail.data(), _tail.size()}, context, host);
	}

	void EditorInsertStrips::SyncHits(UI::Context& context) {
		bool live = context.IsHoverSuppressed();
		if (live == _live) {
			return;
		}
		_live = live;
		for (UI::ElementHandle strip : _built) {
			if (!context.IsAlive(strip)) {
				continue;
			}
			UI::Style style = context.GetStyle(strip);
			style.pointerEvents = live;
			context.SetStyle(strip, style);
		}
	}

} // namespace Shalltear
