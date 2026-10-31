#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RHITypes.h"

namespace Shalltear::UI {

	struct LayerPass {
		RHI::TextureHandle target;
		Vec2I sizePx{};
		Vec2 originPx{};
		bool layered = false;
	};

	class LayerPassStack {
	public:
		void Reset(RHI::TextureHandle target, Vec2I sizePx) {
			_stack.clear();
			_compositeSource = RHI::TextureHandle{};
			_stack.push_back(LayerPass{target, sizePx, {0.0f, 0.0f}, false});
		}

		const LayerPass& Top() const { return _stack.back(); }
		size_t Depth() const { return _stack.size(); }

		const LayerPass& Push(RHI::TextureHandle layer, Vec2I sizePx, Vec2 originPx) {
			if (!layer.IsValid()) {
				_stack.push_back(DegradedFrom(_stack.back()));
				return _stack.back();
			}
			_stack.push_back(LayerPass{layer, sizePx, originPx, true});
			return _stack.back();
		}

		// must not pop, the scope's pop is still coming
		const LayerPass& DegradeTop() {
			if (_stack.size() > 1) {
				_stack.back() = DegradedFrom(_stack[_stack.size() - 2]);
			}
			return _stack.back();
		}

		struct PopResult {
			bool popped = false;
			LayerPass closed{};
			LayerPass parent{};
		};

		PopResult Pop() {
			if (_stack.size() <= 1) {
				return PopResult{};
			}
			PopResult result;
			result.popped = true;
			result.closed = _stack.back();
			_stack.pop_back();
			result.parent = _stack.back();
			_compositeSource = result.closed.layered ? result.closed.target : RHI::TextureHandle{};
			return result;
		}

		RHI::TextureHandle CompositeSource() const { return _compositeSource; }
	private:
		static LayerPass DegradedFrom(const LayerPass& parent) {
			LayerPass entry = parent;
			entry.layered = false;
			return entry;
		}

		Vector<LayerPass> _stack{GetMemoryResource(MemTag::UI)};
		RHI::TextureHandle _compositeSource;
	};

} // namespace Shalltear::UI
