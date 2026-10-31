#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/UIDocument.h"

#include "Shalltear/Editor/UIDocumentOps.h"
#include "Shalltear/Editor/UndoSystem.h"

namespace Shalltear {

	class UISetDocumentCommand final : public IEditorCommand {
	public:
		UISetDocumentCommand(Handle<UIDocumentAsset> document, UIDocumentOps::Snapshot before,
							 UIDocumentOps::Snapshot after, std::string_view label);

		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
	private:
		void Write(const UIDocumentOps::Snapshot& snapshot);

		Handle<UIDocumentAsset> _document;
		UIDocumentOps::Snapshot _before;
		UIDocumentOps::Snapshot _after;
		str _label;
	};

	class UISetSheetSourceCommand final : public IEditorCommand {
	public:
		UISetSheetSourceCommand(Handle<StyleSheetAsset> sheet, str before, str after, std::string_view label);

		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
	private:
		void Write(const str& value);

		Handle<StyleSheetAsset> _sheet;
		str _before;
		str _after;
		str _label;
	};

} // namespace Shalltear
