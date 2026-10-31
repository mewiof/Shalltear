#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/EnumFlags.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Element.h"
#include "Shalltear/Core/UI/Style.h"
#include "Shalltear/Core/UI/UIAtlas.h"

#include <span>
#include <string_view>

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	namespace UI {
		class Context;
	}

	namespace UIDebugOps {

		class SHALLTEAR_EDITOR_API NameDictionary {
		public:
			NameDictionary();

			struct Sheet {
				StringHash tag;
				String name;
				String source;
			};

			void AddCandidateSheet(std::string_view name, std::string_view source);

			void Rebuild(const UI::Context& context);

			[[nodiscard]] std::string_view Lookup(StringHash hash) const;

			[[nodiscard]] str Render(StringHash hash) const;

			void AddName(std::string_view name);

			[[nodiscard]] u32 GetCount() const;
			[[nodiscard]] std::span<const Sheet> GetSheets() const;

			struct Residue {
				u32 total = 0;
				u32 unresolved = 0;
				u32 ids = 0;
				u32 idsUnresolved = 0;
				u32 tags = 0;
				u32 tagsUnresolved = 0;
				u32 classes = 0;
				u32 classesUnresolved = 0;
				[[nodiscard]] f32 Fraction() const { return total == 0 ? 0.0f : static_cast<f32>(unresolved) / static_cast<f32>(total); }
			};
			[[nodiscard]] Residue MeasureResidue(const UI::Context& context) const;
		private:
			void Add(std::string_view name);
			void AddSheet(StringHash tag, std::string_view name, std::string_view source);

			HashMap<StringHash, String> _names{GetMemoryResource(MemTag::Editor)};
			Vector<Sheet> _sheets{GetMemoryResource(MemTag::Editor)};
			Vector<Sheet> _candidates{GetMemoryResource(MemTag::Editor)};
			Vector<String> _typed{GetMemoryResource(MemTag::Editor)};
		};

		struct Target {
			enum class Kind : u8 {
				Window,
				Surface
			};
			Kind kind = Kind::Window;
			u64 id = 0;
			str label;
		};

		SHALLTEAR_EDITOR_API void EnumerateTargets(Vector<Target>& out);

		[[nodiscard]] SHALLTEAR_EDITOR_API UI::Context* ResolveTarget(const Target& target);

		[[nodiscard]] SHALLTEAR_EDITOR_API Target TargetForContext(const UI::Context& context);

		SHALLTEAR_EDITOR_API bool OpenDebuggerOn(const Target& target);

		struct Row {
			UI::ElementHandle element;
			u32 depth = 0;
			u32 childCount = 0;
			u64 key = 0;
		};

		struct Snapshot {
			Vector<Row> rows{GetMemoryResource(MemTag::Editor)};
			u32 revision = 0;
		};

		SHALLTEAR_EDITOR_API void BuildSnapshot(const UI::Context& context, Snapshot& out);

		inline constexpr std::string_view MarkerClass = "slt-uidebug-overlay";

		SHALLTEAR_EDITOR_API str RowLabel(const UI::Context& context, UI::ElementHandle element,
										  const NameDictionary& names);

		SHALLTEAR_EDITOR_API void PathTo(const UI::Context& context, UI::ElementHandle element, Vector<u32>& out);

		SHALLTEAR_EDITOR_API UI::ElementHandle Resolve(const UI::Context& context, std::span<const u32> path);

		struct ElementIdentity {
			StringHash id{};
			StringHash typeTag{};
			StringHash classes[UI::MAX_ELEMENT_CLASSES]{};
			bool valid = false;
		};

		[[nodiscard]] SHALLTEAR_EDITOR_API ElementIdentity IdentityOf(const UI::Context& context,
																	  UI::ElementHandle element);

		[[nodiscard]] SHALLTEAR_EDITOR_API UI::ElementHandle ResolveSame(const UI::Context& context,
																		 std::span<const u32> path,
																		 const ElementIdentity& identity);

		[[nodiscard]] SHALLTEAR_EDITOR_API u64 PathKey(std::span<const u32> path);

		SHALLTEAR_EDITOR_API bool ExpandAncestors(std::span<const u32> path, Vector<u64>& collapsed);

		struct TreeRow {
			UI::ElementHandle element;
			u64 key = 0;
			u32 depth = 0;
			bool hasChildren = false;
			bool expanded = false;
			bool matched = false;
			str label;
		};

		struct TreeViewDesc {
			std::string_view query;
			std::span<const u64> collapsed;
		};

		SHALLTEAR_EDITOR_API void BuildTreeView(const UI::Context& context, const Snapshot& snapshot,
												const NameDictionary& names, const TreeViewDesc& desc,
												Vector<TreeRow>& out);

		struct Geometry {
			UI::Rect worldBound;
			UI::Rect worldClip;
			UI::Rect contentRect;
			UI::Rect layout;
			UI::Edges margin;
			UI::Edges border;
			UI::Edges padding;
			bool clipped = false;
			bool operator==(const Geometry&) const = default;
		};

		SHALLTEAR_EDITOR_API void BuildGeometry(const UI::Context& context, UI::ElementHandle element, Geometry& out);

		SHALLTEAR_EDITOR_API str FormatNumber(f32 value);

		[[nodiscard]] SHALLTEAR_EDITOR_API UI::ElementHandle PickAt(const UI::Context& context, Vec2 pointPx);

		enum class HighlightRing : u8 {
			Margin = 0,
			Border,
			Padding,
			Content,
			Count
		};

		inline constexpr size_t HIGHLIGHT_BAND_COUNT = static_cast<size_t>(HighlightRing::Count);

		struct HighlightBand {
			UI::Rect box;
			UI::Edges ring;
		};

		struct Highlight {
			HighlightBand bands[HIGHLIGHT_BAND_COUNT];
		};

		SHALLTEAR_EDITOR_API void BuildHighlight(const UI::Context& context, UI::ElementHandle element,
												 Highlight& out);

		enum class TextBoxKinds : u8 {
			None = 0,
			Characters = 1u << 0,
			Words = 1u << 1,
			Lines = 1u << 2,
		};
		SLT_ENUM_CLASS_FLAGS(TextBoxKinds)

		struct TextBox {
			UI::Rect box;
			TextBoxKinds kind = TextBoxKinds::None;
		};

		struct TextOverlayStats {
			u32 runs = 0;
			u32 boxes = 0;
			u32 wordlessRuns = 0;
		};

		SHALLTEAR_EDITOR_API TextOverlayStats BuildTextBoxes(const UI::Context& context, UI::ElementHandle element,
															 TextBoxKinds kinds, Vector<TextBox>& out);

		SHALLTEAR_EDITOR_API TextOverlayStats CollectTextBoxes(const UI::Context& context, TextBoxKinds kinds,
															   Vector<TextBox>& out);

		struct AtlasPageRow {
			std::string_view name;
			UI::AtlasPage page;
		};

		SHALLTEAR_EDITOR_API void BuildAtlasPages(Vector<AtlasPageRow>& out);

		inline constexpr std::string_view NoAtlasText = "No atlas page (headless: nothing was rasterized)";

		struct AttributeRow {
			enum class Edit : u8 {
				None = 0,
				Id,
				TypeTag,
				Classes,
				Visible,
				Hoverable,
				Focusable,
				Tint,
				UserTag,
				ScrollOffset,
				Text,
			};

			std::string_view name;
			str value;
			Edit edit = Edit::None;
		};

		SHALLTEAR_EDITOR_API void BuildAttributes(const UI::Context& context, UI::ElementHandle element,
												  const NameDictionary& names, Vector<AttributeRow>& out);

		struct SheetRow {
			StringHash tag;
			std::string_view name;
			u32 ruleCount = 0;
			bool readable = false;
		};

		SHALLTEAR_EDITOR_API void BuildSheets(const NameDictionary& names, Vector<SheetRow>& out);

		struct Match {
			std::string_view sheet;
			std::string_view selector;
			std::string_view body;
			u32 rule = 0;
			i32 specificity = 0;
			u8 pseudo = UI::PSEUDO_NONE;
			bool applies = false;
		};

		SHALLTEAR_EDITOR_API void BuildMatches(const UI::Context& context, UI::ElementHandle element,
											   const NameDictionary& names, Vector<Match>& out);

		enum class ValueSource : u8 {
			Default = 0,
			Sheet,
			Inline,
		};

		struct PropertyRow {
			std::string_view name;
			str value;
			ValueSource source = ValueSource::Default;
			std::string_view sheet;
		};

		SHALLTEAR_EDITOR_API void BuildProperties(const UI::Context& context, UI::ElementHandle element,
												  const NameDictionary& names, Vector<PropertyRow>& out);

		struct StyleEdits {
			struct Declaration {
				StringHash name;
				str value;
			};

			UI::Style captured;
			UI::Style written;
			Vector<Declaration> declarations{GetMemoryResource(MemTag::Editor)};
			bool armed = false;

			void Clear() {
				declarations.clear();
				armed = false;
			}
			[[nodiscard]] bool IsEdited(StringHash name) const {
				for (const Declaration& declaration : declarations) {
					if (declaration.name == name) {
						return true;
					}
				}
				return false;
			}
		};

		SHALLTEAR_EDITOR_API bool SetProperty(UI::Context& context, UI::ElementHandle element, StyleEdits& edits,
											  StringHash name, std::string_view value);

		[[nodiscard]] SHALLTEAR_EDITOR_API bool CanRevert(const UI::Context& context, UI::ElementHandle element,
														  const StyleEdits& edits, StringHash name);

		SHALLTEAR_EDITOR_API bool RevertProperty(UI::Context& context, UI::ElementHandle element, StyleEdits& edits,
												 StringHash name);

		SHALLTEAR_EDITOR_API void SetClasses(UI::Context& context, UI::ElementHandle element, std::string_view text,
											 NameDictionary& names);

	} // namespace UIDebugOps

} // namespace Shalltear
