#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Element.h"
#include "Shalltear/Core/UI/Style.h"
#include "Shalltear/Core/UI/UIDocument.h"

#include <span>
#include <string_view>

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	namespace UI {
		class Context;
		struct WidgetDesc;
	} // namespace UI

	namespace UIBuilderOps {

		inline constexpr u32 INVALID_NODE = 0xFFFFFFFFu;

		SHALLTEAR_EDITOR_API bool OpenDocumentInBuilder(std::string_view virtualPath);

		SHALLTEAR_EDITOR_API bool ShowSheetRuleInBuilder(std::string_view sheetVirtualPath, u32 rule,
														 std::string_view selector);

		struct Row {
			u32 index = INVALID_NODE;
			u32 depth = 0;
			const UIDocumentNode* node = nullptr;
		};

		SHALLTEAR_EDITOR_API void BuildRows(const UIDocumentAsset& document, Vector<Row>& out);

		SHALLTEAR_EDITOR_API str RowLabel(const UIDocumentNode& node);

		SHALLTEAR_EDITOR_API UI::ElementHandle ElementForNode(std::span<const UIInstantiateMapping> mapping,
															  const UIDocumentNode* node);

		SHALLTEAR_EDITOR_API u32 NodeForElement(const UI::Context& context, const UIDocumentAsset& document,
												std::span<const UIInstantiateMapping> mapping, UI::ElementHandle element);

		struct LibraryGroup {
			std::string_view category;
			Vector<const UI::WidgetDesc*> widgets{GetMemoryResource(MemTag::Editor)};
		};

		SHALLTEAR_EDITOR_API void BuildLibrary(Vector<LibraryGroup>& out);

		struct SheetRow {
			u32 sheet = 0;
			u32 rule = INVALID_NODE;
			std::string_view path;
			std::string_view selector;
			std::string_view pseudo;
		};

		SHALLTEAR_EDITOR_API void BuildSheetRows(const UIDocumentAsset& document, Vector<SheetRow>& out);

		enum class Subject : u8 { None,
								  Element,
								  Selector,
								  Canvas };

		struct PropertyRow {
			std::string_view name;
			std::string_view value;
			bool overridden = false;
		};

		SHALLTEAR_EDITOR_API void BuildPropertyRows(std::span<const UI::StyleDeclarationSource> declarations,
													Vector<PropertyRow>& out);

		struct MatchingSelector {
			std::string_view sheet;
			std::string_view selector;
			i32 specificity = 0;
		};

		SHALLTEAR_EDITOR_API void BuildMatchingSelectors(const UIDocumentAsset& document, const UIDocumentNode& node,
														 Vector<MatchingSelector>& out);

		SHALLTEAR_EDITOR_API const UIDocumentNode* NodeAt(const UIDocumentAsset& document, u32 index);

		// runs must be views into source
		SHALLTEAR_EDITOR_API str EraseRuns(std::string_view source, std::span<const std::string_view> runs);

		enum class DeclarationLayout : u8 {
			Block,
			Inline
		};

		SHALLTEAR_EDITOR_API str SetDeclaration(std::string_view source, std::string_view region,
												std::span<const UI::StyleDeclarationSource> declarations,
												std::string_view property, std::string_view value,
												DeclarationLayout layout);

		SHALLTEAR_EDITOR_API str AppendRule(std::string_view source, std::string_view selector,
											std::span<const std::string_view> runs);

		enum class CodeToken : u8 {
			Text = 0,
			Punctuation,
			Tag,
			Attribute,
			Value,
			Comment,
		};

		enum class CodeLanguage : u8 {
			Markup,
			Style,
		};

		struct CodeSpan {
			std::string_view text;
			CodeToken kind = CodeToken::Text;
		};

		struct CodeLine {
			u32 number = 0;
			Vector<CodeSpan> spans{GetMemoryResource(MemTag::Editor)};
		};

		SHALLTEAR_EDITOR_API void TokenizeCode(std::string_view source, CodeLanguage language, Vector<CodeLine>& out);

		inline constexpr f32 MIN_CANVAS_ZOOM = 0.02f;
		inline constexpr f32 MAX_CANVAS_ZOOM = 2.0f;

		struct CanvasView {
			f32 zoom = 1.0f;
			Vec2 pan{};
		};

		SHALLTEAR_EDITOR_API UI::Rect CanvasRectFor(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight,
													const CanvasView& view);

		SHALLTEAR_EDITOR_API f32 ZoomToFit(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight);

		SHALLTEAR_EDITOR_API CanvasView ZoomAtPoint(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight,
													const CanvasView& view, Vec2 point, f32 notches);

		SHALLTEAR_EDITOR_API CanvasView PanBy(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight,
											  const CanvasView& view, Vec2 deltaPx);

		SHALLTEAR_EDITOR_API CanvasView ClampView(const UI::Rect& viewport, i32 canvasWidth, i32 canvasHeight,
												  const CanvasView& view);

	} // namespace UIBuilderOps

} // namespace Shalltear
