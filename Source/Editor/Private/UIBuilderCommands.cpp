#include "UIBuilderCommands.h"

#include "Shalltear/Core/Log.h"

#include "Shalltear/Editor/EditorAssetOps.h"

namespace Shalltear {

	namespace {

		void NoteWritten(Asset& asset, u32& version) {
			++version;
			EditorAssetOps::MarkDirty(Handle<Asset>(&asset));
		}

	} // anonymous namespace

	UISetDocumentCommand::UISetDocumentCommand(Handle<UIDocumentAsset> document, UIDocumentOps::Snapshot before,
											   UIDocumentOps::Snapshot after, std::string_view label)
		: _document(document), _before(std::move(before)), _after(std::move(after)), _label(label) {}

	void UISetDocumentCommand::Apply() {
		Write(_after);
	}

	void UISetDocumentCommand::Revert() {
		Write(_before);
	}

	str UISetDocumentCommand::GetLabel() const {
		return _label;
	}

	void UISetDocumentCommand::Write(const UIDocumentOps::Snapshot& snapshot) {
		UIDocumentAsset* document = _document.Get();
		if (!document) {
			SLT_LOG_WARN("Editor", "UIBuilder: document edit skipped, its document is gone");
			return;
		}
		UIDocumentOps::Restore(*document, snapshot);
		NoteWritten(*document, document->version);
	}

	UISetSheetSourceCommand::UISetSheetSourceCommand(Handle<StyleSheetAsset> sheet, str before, str after,
													 std::string_view label)
		: _sheet(sheet), _before(std::move(before)), _after(std::move(after)), _label(label) {}

	void UISetSheetSourceCommand::Apply() {
		Write(_after);
	}

	void UISetSheetSourceCommand::Revert() {
		Write(_before);
	}

	str UISetSheetSourceCommand::GetLabel() const {
		return _label;
	}

	void UISetSheetSourceCommand::Write(const str& value) {
		StyleSheetAsset* sheet = _sheet.Get();
		if (!sheet) {
			SLT_LOG_WARN("Editor", "UIBuilder: sheet edit skipped, its style document is gone");
			return;
		}
		sheet->source = value;
		NoteWritten(*sheet, sheet->version);
	}

} // namespace Shalltear
