#include "Shalltear/Editor/EditorFonts.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/UI/FontAsset.h"

namespace Shalltear::EditorFonts {

	namespace {

		constexpr const char* MONOSPACE_PATH = "engine://Fonts/SourceCodePro/SourceCodePro-Regular.ttf";

		Handle<FontAsset> _monospace;
		bool _attempted = false;

	} // namespace

	Handle<FontAsset> GetMonospace() {
		if (_attempted) {
			return _monospace;
		}
		_attempted = true;
		Asset* asset = AssetDatabase::LoadAsset(Path(MONOSPACE_PATH));
		if (asset && asset->GetAssetTypeId() == StringHash("FontAsset")) {
			_monospace = Handle<FontAsset>(static_cast<FontAsset*>(asset));
		} else {
			if (asset) {
				AssetDatabase::UnloadAsset(asset);
			}
			SLT_LOG_WARN("Editor", "monospace face {} unavailable (code views use the UI face)", MONOSPACE_PATH);
		}
		return _monospace;
	}

	void Shutdown() {
		if (Asset* font = _monospace.Get()) {
			AssetDatabase::UnloadAsset(font);
		}
		_monospace = {};
		_attempted = false;
	}

} // namespace Shalltear::EditorFonts
