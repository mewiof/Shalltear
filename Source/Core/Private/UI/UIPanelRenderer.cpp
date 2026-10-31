#include "Shalltear/Core/UI/UIPanelRenderer.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/Context.h"
#include "UI/UIInternal.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct LiveRenderers {
			Vector<Handle<UIPanelRenderer>> renderers{GetMemoryResource(MemTag::UI)};
		};
		LiveRenderers* _live = nullptr;
		const u8 _FRAME_STAGE_OWNER = 0;

		UIScreenSurfaceDesc DescFor(const Handle<UIDocumentAsset>& document, const Handle<UIPanelSettings>& settings, f32 sortOrder) {
			UIScreenSurfaceDesc desc;
			desc.settings = settings;
			desc.sortOrder = sortOrder;
			const UIDocumentAsset* resolved = document.Get();
			desc.name = resolved && !resolved->virtualPath.empty() ? resolved->virtualPath : str("UIPanelRenderer");
			return desc;
		}

	} // anonymous namespace

	UIPanelRenderer::~UIPanelRenderer() = default;

	void UIPanelRenderer::OnEnable() {
		Build();
		if (!_live) {
			_live = New<LiveRenderers>(MemTag::UI);
			FrameStages::Register(FrameStage::UIUpdate, FrameStagePosition::Early,
								  &UIPanelRenderer::ReconcileAll, nullptr, &_FRAME_STAGE_OWNER);
		}
		_live->renderers.push_back(Handle<UIPanelRenderer>(this));
	}

	void UIPanelRenderer::OnDisable() {
		if (_live) {
			Handle<UIPanelRenderer> self(this);
			auto& list = _live->renderers;
			list.erase(std::remove(list.begin(), list.end(), self), list.end());
			if (list.empty()) {
				FrameStages::UnregisterOwnedBy(&_FRAME_STAGE_OWNER);
				Delete(_live);
				_live = nullptr;
			}
		}
		Teardown();
	}

	void UIPanelRenderer::Build() {
		if (!UISystem::IsInitialized()) {
			return;
		}
		_surface = UISystem::CreateScreenSurface(DescFor(_document, _panelSettings, _sortOrder));
		_aimedSettings = _panelSettings;
		_aimedSortOrder = _sortOrder;
		Populate();
	}

	void UIPanelRenderer::Teardown() {
		if (_surface != INVALID_UI_SURFACE) {
			UISystem::DestroySurface(_surface);
			_surface = INVALID_UI_SURFACE;
		}
		_named.clear();
		_builtDocument = {};
	}

	void UIPanelRenderer::Aim() {
		if (_surface != INVALID_UI_SURFACE) {
			UISystem::SetScreenSurfaceDesc(_surface, DescFor(_document, _panelSettings, _sortOrder));
		}
		_aimedSettings = _panelSettings;
		_aimedSortOrder = _sortOrder;
	}

	void UIPanelRenderer::Populate() {
		_named.clear();
		_builtDocument = _document;
		const UIDocumentAsset* built = _document.Get();
		_builtVersion = built ? built->GetContentVersion() : 0;
		UI::Context* context = GetContext();
		if (!context) {
			return;
		}
		context->ClearChildren(context->GetRoot());
		context->ClearStyleSheets();

		const UIDocumentAsset* document = _document.Get();
		if (!document) {
			return;
		}
		UIInstantiateDesc desc;
		const UIPanelSettings* settings = _panelSettings.Get();
		desc.font = settings && settings->font.IsValid() ? settings->font : UISystem::GetDefaultFont();
		if (settings) {
			desc.fontSizePx = settings->fontSizePx;
			desc.textColor = settings->textColor;
		}
		Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::UI)};
		document->Instantiate(*context, context->GetRoot(), desc, mapping);
		for (const UIInstantiateMapping& entry : mapping) {
			if (entry.node && !entry.node->id.empty()) {
				_named.push_back({StringHash(entry.node->id), entry.element});
			}
		}
	}

	void UIPanelRenderer::Reconcile() {
		if (_surface == INVALID_UI_SURFACE) {
			return;
		}
		const bool documentMoved = !(_document == _builtDocument);
		const bool settingsMoved = !(_panelSettings == _aimedSettings);
		if (settingsMoved || _sortOrder != _aimedSortOrder || documentMoved) {
			Aim();
		}
		const UIDocumentAsset* document = _document.Get();
		const bool contentMoved = !documentMoved && document && document->GetContentVersion() != _builtVersion;
		if (documentMoved || settingsMoved || contentMoved) {
			Populate();
		}
	}

	void UIPanelRenderer::ReconcileAll(const FrameStageArgs&, void*) {
		if (!_live) {
			return;
		}
		Vector<Handle<UIPanelRenderer>> renderers{GetMemoryResource(MemTag::UI)};
		renderers = _live->renderers;
		for (const Handle<UIPanelRenderer>& handle : renderers) {
			if (UIPanelRenderer* renderer = handle.Get()) {
				renderer->Reconcile();
			}
		}
	}

	void UIPanelRenderer::SetDocument(Handle<UIDocumentAsset> document) {
		_document = document;
		if (_surface != INVALID_UI_SURFACE) {
			Aim();
			Populate();
		}
	}

	void UIPanelRenderer::SetPanelSettings(Handle<UIPanelSettings> settings) {
		_panelSettings = settings;
		if (_surface != INVALID_UI_SURFACE) {
			Aim();
			Populate();
		}
	}

	void UIPanelRenderer::SetSortOrder(f32 order) {
		_sortOrder = order;
		if (_surface != INVALID_UI_SURFACE) {
			Aim();
		}
	}

	UI::Context* UIPanelRenderer::GetContext() const {
		return _surface != INVALID_UI_SURFACE ? UISystem::GetSurfaceContext(_surface) : nullptr;
	}

	UI::ElementHandle UIPanelRenderer::FindElement(std::string_view id) const {
		UI::Context* context = GetContext();
		if (!context || id.empty()) {
			return {};
		}
		const StringHash wanted(id);
		for (auto it = _named.rbegin(); it != _named.rend(); ++it) {
			if (it->id == wanted && context->IsAlive(it->element)) {
				return it->element;
			}
		}
		return {};
	}

} // namespace Shalltear

// static player drops this unit without a reference
SLT_REGISTER_TYPE(Shalltear::UIPanelRenderer)
