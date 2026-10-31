#pragma once

#include "ViewportHost.h"

#include "Shalltear/Core/Graphics/FullScreenPass.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorPreviews.h"

#include <functional>

namespace Shalltear {

	class Asset;
	class Scene;

	class PreviewPane {
	public:
		UI::ElementHandle Build(UI::Context& context, UI::ElementHandle parent);

		void Rebuild(UI::Context& context, const Guid& guid, bool expanded, std::function<void()> onToggle);

		void Tick(UI::Context& context);

		void Release();

		void ReleaseLive();

		void ReleaseForReload(UI::Context& context);

		UI::ElementHandle GetElement() const { return _root; }
	private:
		bool GoLive(UI::Context& context);
		void SubmitLive(UI::Context& context);

		void ShowImage(UI::Context& context, RHI::TextureHandle texture, Vec2I size, UI::SamplerKind sampler,
					   const str& detail);
		void ShowBadge(UI::Context& context);

		void HideRegistered();

		UI::ElementHandle _root;
		UI::ElementHandle _body;
		UI::ElementHandle _badge;
		UI::ElementHandle _detail;

		Guid _guid;
		bool _expanded = true;
		str _fileName;

		StringHash _type;
		bool _shown = false;
		bool _registeredBody = false;
		AssetPreviewPaneRefreshFn _headerRefresh;
		AssetPreviewPaneRefreshFn _bodyRefresh;
		RHI::TextureHandle _paintedTexture;
		UI::SamplerKind _paintedSampler = UI::SamplerKind::LinearClamp;
		Vec2I _imageSize{};

		ViewportHost _viewport;
		Handle<Scene> _liveStage;
		Handle<Asset> _liveAsset;
		FullScreenPass _liveRow;
		AABB _subjectBounds = AABB::Empty();
		AssetPreviewOrbit _orbit;
		bool _live = false;
		bool _notOrbitable = false;
		bool _dragging = false;
		Vec2 _lastDragPx{};
		Vec2I _liveSizePx{};
		bool _viewDirty = false;
		u64 _liveRevision = 0;
	};

} // namespace Shalltear
