#include "Shalltear/Editor/UIDocumentOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"

#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/UIBuilderOps.h"

#include <algorithm>

namespace Shalltear::UIDocumentOps {

	namespace {

		constexpr u32 INVALID = UIBuilderOps::INVALID_NODE;

		template <typename Fn>
		bool WalkNodes(UIDocumentNode& node, UIDocumentNode* parent, u32 childIndex, u32& index, const Fn& visit) {
			if (!visit(node, parent, childIndex, index)) {
				return false;
			}
			++index;
			if (!node.templateName.empty()) {
				return true;
			}
			for (u32 i = 0; i < static_cast<u32>(node.children.size()); ++i) {
				if (!WalkNodes(node.children[i], &node, i, index, visit)) {
					return false;
				}
			}
			return true;
		}

		bool HasRoot(const UIDocumentAsset& document) {
			return !document.root.tag.empty() || !document.root.templateName.empty();
		}

		using NodePath = Vector<u32>;

		bool PathOf(UIDocumentAsset& document, u32 index, NodePath& out) {
			out.clear();
			if (index == INVALID || !HasRoot(document)) {
				return false;
			}
			bool found = false;
			std::function<bool(UIDocumentNode&, u32&)> walk = [&](UIDocumentNode& node, u32& walked) {
				if (walked == index) {
					found = true;
					return false;
				}
				++walked;
				if (!node.templateName.empty()) {
					return true;
				}
				for (u32 i = 0; i < static_cast<u32>(node.children.size()); ++i) {
					out.push_back(i);
					if (!walk(node.children[i], walked)) {
						return false;
					}
					out.pop_back();
				}
				return true;
			};
			u32 walked = 0;
			walk(document.root, walked);
			return found;
		}

		UIDocumentNode* ResolvePath(UIDocumentAsset& document, const NodePath& path) {
			UIDocumentNode* node = &document.root;
			for (u32 step : path) {
				if (step >= node->children.size()) {
					return nullptr;
				}
				node = &node->children[step];
			}
			return node;
		}

		UIDocumentNode* Resolve(UIDocumentAsset& document, u32 index, UIDocumentNode** outParent, u32* outChildIndex) {
			if (index == INVALID || !HasRoot(document)) {
				return nullptr;
			}
			UIDocumentNode* found = nullptr;
			u32 walked = 0;
			WalkNodes(document.root, nullptr, 0, walked,
					  [&](UIDocumentNode& node, UIDocumentNode* parent, u32 childIndex, u32 nodeIndex) {
						  if (nodeIndex != index) {
							  return true;
						  }
						  found = &node;
						  if (outParent) {
							  *outParent = parent;
						  }
						  if (outChildIndex) {
							  *outChildIndex = childIndex;
						  }
						  return false;
					  });
			return found;
		}

		void CopyAttributes(const Vector<UIDocumentAttribute>& from, Vector<UIDocumentAttribute>& to) {
			to = Vector<UIDocumentAttribute>{GetMemoryResource(MemTag::Asset)};
			to.reserve(from.size());
			for (const UIDocumentAttribute& attribute : from) {
				to.push_back(UIDocumentAttribute{attribute.name, attribute.value});
			}
		}

		void CopyOverrides(const Vector<UIDocumentOverride>& from, Vector<UIDocumentOverride>& to) {
			to = Vector<UIDocumentOverride>{GetMemoryResource(MemTag::Asset)};
			to.reserve(from.size());
			for (const UIDocumentOverride& source : from) {
				UIDocumentOverride copy;
				copy.elementId = source.elementId;
				copy.text = source.text;
				CopyAttributes(source.attributes, copy.attributes);
				to.push_back(std::move(copy));
			}
		}

		const UIDocumentTemplate* FindTemplate(const UIDocumentAsset& document, std::string_view alias) {
			for (const UIDocumentTemplate& declared : document.templates) {
				if (declared.name == alias) {
					return &declared;
				}
			}
			return nullptr;
		}

		UIDocumentNode* FindById(UIDocumentNode& tree, std::string_view id) {
			if (tree.id == id) {
				return &tree;
			}
			for (UIDocumentNode& child : tree.children) {
				if (UIDocumentNode* found = FindById(child, id)) {
					return found;
				}
			}
			return nullptr;
		}

		void CollectInstances(UIDocumentNode& tree, Vector<UIDocumentNode*>& out) {
			if (!tree.templateName.empty()) {
				out.push_back(&tree);
				return;
			}
			for (UIDocumentNode& child : tree.children) {
				CollectInstances(child, out);
			}
		}

		void ApplyOverrideToNode(UIDocumentNode& target, const UIDocumentOverride& override) {
			for (const UIDocumentAttribute& attribute : override.attributes) {
				bool replaced = false;
				for (UIDocumentAttribute& existing : target.attributes) {
					if (existing.name == attribute.name) {
						existing.value = attribute.value;
						replaced = true;
						break;
					}
				}
				if (!replaced) {
					target.attributes.push_back(UIDocumentAttribute{attribute.name, attribute.value});
				}
			}
			if (!override.text.empty()) {
				target.text = override.text;
			}
		}

		void MergeOverrideInto(UIDocumentNode& instance, const UIDocumentOverride& outer) {
			for (UIDocumentOverride& existing : instance.overrides) {
				if (existing.elementId != outer.elementId) {
					continue;
				}
				for (const UIDocumentAttribute& attribute : outer.attributes) {
					bool replaced = false;
					for (UIDocumentAttribute& inner : existing.attributes) {
						if (inner.name == attribute.name) {
							inner.value = attribute.value;
							replaced = true;
							break;
						}
					}
					if (!replaced) {
						existing.attributes.push_back(UIDocumentAttribute{attribute.name, attribute.value});
					}
				}
				if (!outer.text.empty()) {
					existing.text = outer.text;
				}
				return;
			}
			UIDocumentOverride copy;
			copy.elementId = outer.elementId;
			copy.text = outer.text;
			CopyAttributes(outer.attributes, copy.attributes);
			instance.overrides.push_back(std::move(copy));
		}

		str AdoptTemplate(UIDocumentAsset& into, const UIDocumentTemplate& source) {
			for (const UIDocumentTemplate& declared : into.templates) {
				if (declared.src == source.src) {
					return declared.name;
				}
			}
			str name = source.name;
			for (u32 suffix = 2; FindTemplate(into, name) != nullptr; ++suffix) {
				name = source.name + "-" + std::to_string(suffix);
			}
			UIDocumentTemplate adopted;
			adopted.name = name;
			adopted.src = source.src;
			adopted.document = source.document;
			into.templates.push_back(std::move(adopted));
			return name;
		}

		void AdoptSheet(UIDocumentAsset& into, const UIDocumentSheet& source) {
			for (const UIDocumentSheet& declared : into.sheets) {
				if (declared.src == source.src) {
					return;
				}
			}
			UIDocumentSheet adopted;
			adopted.src = source.src;
			adopted.sheet = source.sheet;
			into.sheets.push_back(std::move(adopted));
		}

		void RebindAliases(UIDocumentNode& tree, const UIDocumentAsset& from, UIDocumentAsset& into) {
			if (!tree.templateName.empty()) {
				if (const UIDocumentTemplate* declared = FindTemplate(from, tree.templateName)) {
					tree.templateName = AdoptTemplate(into, *declared);
				}
				return;
			}
			for (UIDocumentNode& child : tree.children) {
				RebindAliases(child, from, into);
			}
		}

		bool ComposeInstance(UIDocumentAsset& document, const UIDocumentNode& instance, UIDocumentNode& out, str& error) {
			const UIDocumentTemplate* declared = FindTemplate(document, instance.templateName);
			if (!declared) {
				error = str("<instance> uses a template this document doesn't declare: '") + instance.templateName + "'";
				return false;
			}
			const UIDocumentAsset* source = declared->document.Get();
			if (!source) {
				error = str("the template '") + declared->name + "' (" + declared->src + ") isn't loaded (nothing to unpack)";
				return false;
			}

			out = CloneNode(source->root);
			for (const UIDocumentSheet& sheet : source->sheets) {
				AdoptSheet(document, sheet);
			}
			RebindAliases(out, *source, document);

			if (!instance.id.empty()) {
				out.id = instance.id;
			}
			for (u8 i = 0; i < instance.classCount; ++i) {
				if (out.classCount < UI::MAX_ELEMENT_CLASSES) {
					out.classes[out.classCount++] = instance.classes[i];
				}
			}
			if (!instance.style.empty()) {
				out.style = out.style.empty() ? instance.style
											  : out.style + (out.style.back() == ';' ? " " : "; ") + instance.style;
			}

			Vector<UIDocumentNode*> nested{GetMemoryResource(MemTag::Editor)};
			CollectInstances(out, nested);
			for (const UIDocumentOverride& override : instance.overrides) {
				if (UIDocumentNode* target = FindById(out, override.elementId)) {
					ApplyOverrideToNode(*target, override);
					continue;
				}
				for (UIDocumentNode* deeper : nested) {
					MergeOverrideInto(*deeper, override);
				}
			}
			return true;
		}

		void UnpackAll(UIDocumentAsset& document, UIDocumentNode& tree, str& error, bool& ok) {
			if (!ok) {
				return;
			}
			if (!tree.templateName.empty()) {
				UIDocumentNode composed;
				if (!ComposeInstance(document, tree, composed, error)) {
					ok = false;
					return;
				}
				tree = std::move(composed);
				UnpackAll(document, tree, error, ok);
				return;
			}
			for (UIDocumentNode& child : tree.children) {
				UnpackAll(document, child, error, ok);
			}
		}

		void PruneUnusedTemplates(UIDocumentAsset& document) {
			Vector<UIDocumentNode*> instances{GetMemoryResource(MemTag::Editor)};
			CollectInstances(document.root, instances);
			auto used = [&instances](std::string_view alias) {
				for (const UIDocumentNode* instance : instances) {
					if (instance->templateName == alias) {
						return true;
					}
				}
				return false;
			};
			for (size_t i = document.templates.size(); i-- > 0;) {
				if (!used(document.templates[i].name)) {
					document.templates.erase(document.templates.begin() + static_cast<ptrdiff_t>(i));
				}
			}
		}

	} // anonymous namespace

	UIDocumentNode CloneNode(const UIDocumentNode& node) {
		UIDocumentNode copy;
		copy.tag = node.tag;
		copy.id = node.id;
		for (u8 i = 0; i < UI::MAX_ELEMENT_CLASSES; ++i) {
			copy.classes[i] = node.classes[i];
		}
		copy.classCount = node.classCount;
		copy.style = node.style;
		copy.text = node.text;
		copy.templateName = node.templateName;
		CopyAttributes(node.attributes, copy.attributes);
		CopyOverrides(node.overrides, copy.overrides);
		copy.children = Vector<UIDocumentNode>{GetMemoryResource(MemTag::Asset)};
		copy.children.reserve(node.children.size());
		for (const UIDocumentNode& child : node.children) {
			copy.children.push_back(CloneNode(child));
		}
		return copy;
	}

	void Capture(const UIDocumentAsset& document, Snapshot& out) {
		out.root = CloneNode(document.root);
		out.sheets = Vector<UIDocumentSheet>{GetMemoryResource(MemTag::Editor)};
		for (const UIDocumentSheet& sheet : document.sheets) {
			out.sheets.push_back(UIDocumentSheet{sheet.src, sheet.sheet});
		}
		out.templates = Vector<UIDocumentTemplate>{GetMemoryResource(MemTag::Editor)};
		for (const UIDocumentTemplate& declared : document.templates) {
			out.templates.push_back(UIDocumentTemplate{declared.name, declared.src, declared.document});
		}
	}

	void Restore(UIDocumentAsset& document, const Snapshot& snapshot) {
		document.root = CloneNode(snapshot.root);
		document.sheets.clear();
		for (const UIDocumentSheet& sheet : snapshot.sheets) {
			document.sheets.push_back(UIDocumentSheet{sheet.src, sheet.sheet});
		}
		document.templates.clear();
		for (const UIDocumentTemplate& declared : snapshot.templates) {
			document.templates.push_back(UIDocumentTemplate{declared.name, declared.src, declared.document});
		}
	}

	bool Validate(const UIDocumentAsset& document, str& error) {
		UIDocumentAsset probe;
		return probe.Parse(document.Write(), error);
	}

	Location LocationOf(const UIDocumentAsset& document, u32 node) {
		Location location;
		UIDocumentAsset& mutableDocument = const_cast<UIDocumentAsset&>(document);
		UIDocumentNode* parent = nullptr;
		u32 childIndex = 0;
		if (!Resolve(mutableDocument, node, &parent, &childIndex) || !parent) {
			return location;
		}
		location.parent = UIBuilderOps::INVALID_NODE;
		u32 walked = 0;
		WalkNodes(mutableDocument.root, nullptr, 0, walked,
				  [&](UIDocumentNode& candidate, UIDocumentNode*, u32, u32 nodeIndex) {
					  if (&candidate != parent) {
						  return true;
					  }
					  location.parent = nodeIndex;
					  return false;
				  });
		location.childIndex = childIndex;
		return location;
	}

	u32 ChildAt(const UIDocumentAsset& document, u32 parent, u32 childIndex) {
		UIDocumentAsset& mutableDocument = const_cast<UIDocumentAsset&>(document);
		UIDocumentNode* node = Resolve(mutableDocument, parent, nullptr, nullptr);
		if (!node || childIndex >= node->children.size()) {
			return INVALID;
		}
		const UIDocumentNode* wanted = &node->children[childIndex];
		u32 found = INVALID;
		u32 walked = 0;
		WalkNodes(mutableDocument.root, nullptr, 0, walked,
				  [&](UIDocumentNode& candidate, UIDocumentNode*, u32, u32 nodeIndex) {
					  if (&candidate != wanted) {
						  return true;
					  }
					  found = nodeIndex;
					  return false;
				  });
		return found;
	}

	u32 GetChildCount(const UIDocumentAsset& document, u32 node) {
		UIDocumentAsset& mutableDocument = const_cast<UIDocumentAsset&>(document);
		const UIDocumentNode* resolved = Resolve(mutableDocument, node, nullptr, nullptr);
		return resolved ? static_cast<u32>(resolved->children.size()) : 0u;
	}

	bool IsInSubtree(const UIDocumentAsset& document, u32 ancestor, u32 node) {
		if (ancestor == INVALID || node == INVALID) {
			return false;
		}
		if (ancestor == node) {
			return true;
		}
		UIDocumentAsset& mutableDocument = const_cast<UIDocumentAsset&>(document);
		const UIDocumentNode* root = Resolve(mutableDocument, ancestor, nullptr, nullptr);
		const UIDocumentNode* wanted = Resolve(mutableDocument, node, nullptr, nullptr);
		if (!root || !wanted) {
			return false;
		}
		bool found = false;
		u32 walked = 0;
		WalkNodes(const_cast<UIDocumentNode&>(*root), nullptr, 0, walked,
				  [&](UIDocumentNode& candidate, UIDocumentNode*, u32, u32) {
					  if (&candidate != wanted) {
						  return true;
					  }
					  found = true;
					  return false;
				  });
		return found;
	}

	void GetOutermost(const UIDocumentAsset& document, std::span<const u32> nodes, Vector<u32>& out) {
		out.clear();
		UIDocumentAsset& mutableDocument = const_cast<UIDocumentAsset&>(document);
		for (u32 node : nodes) {
			if (!Resolve(mutableDocument, node, nullptr, nullptr) || std::find(out.begin(), out.end(), node) != out.end()) {
				continue;
			}
			bool nested = false;
			for (u32 other : nodes) {
				nested = nested || (other != node && IsInSubtree(document, other, node));
			}
			if (!nested) {
				out.push_back(node);
			}
		}
		std::sort(out.begin(), out.end());
	}

	bool CanTakeChildren(const UIDocumentAsset& document, u32 node) {
		UIDocumentAsset& mutableDocument = const_cast<UIDocumentAsset&>(document);
		const UIDocumentNode* resolved = Resolve(mutableDocument, node, nullptr, nullptr);
		return resolved && resolved->templateName.empty() && resolved->text.empty();
	}

	namespace {

		template <typename Fn>
		bool Guarded(UIDocumentAsset& document, str& error, const Fn& mutate) {
			Snapshot before;
			Capture(document, before);
			if (!mutate()) {
				Restore(document, before);
				return false;
			}
			if (!Validate(document, error)) {
				Restore(document, before);
				return false;
			}
			return true;
		}

	} // anonymous namespace

	bool Insert(UIDocumentAsset& document, u32 parent, u32 childIndex, const UIDocumentNode& node, str& error) {
		return Guarded(document, error, [&]() {
			UIDocumentNode* target = Resolve(document, parent, nullptr, nullptr);
			if (!target) {
				error = "there is no element at that index to insert into";
				return false;
			}
			if (!target->templateName.empty()) {
				error = "an <instance> expands its template and takes no children";
				return false;
			}
			if (!target->text.empty()) {
				error = str("'") + UIBuilderOps::RowLabel(*target) + "' has text content (an element can't have both)";
				return false;
			}
			u32 at = std::min(childIndex, static_cast<u32>(target->children.size()));
			target->children.insert(target->children.begin() + static_cast<ptrdiff_t>(at), CloneNode(node));
			return true;
		});
	}

	bool Remove(UIDocumentAsset& document, u32 node, str& error) {
		return Guarded(document, error, [&]() {
			UIDocumentNode* parent = nullptr;
			u32 childIndex = 0;
			UIDocumentNode* target = Resolve(document, node, &parent, &childIndex);
			if (!target) {
				error = "there is no element at that index";
				return false;
			}
			if (!parent) {
				error = "a document has 1 root element. This one can't be deleted";
				return false;
			}
			parent->children.erase(parent->children.begin() + static_cast<ptrdiff_t>(childIndex));
			return true;
		});
	}

	bool Duplicate(UIDocumentAsset& document, u32 node, str& error) {
		return Guarded(document, error, [&]() {
			UIDocumentNode* parent = nullptr;
			u32 childIndex = 0;
			UIDocumentNode* target = Resolve(document, node, &parent, &childIndex);
			if (!target) {
				error = "there is no element at that index";
				return false;
			}
			if (!parent) {
				error = "a document has 1 root element. This one can't be duplicated";
				return false;
			}
			UIDocumentNode copy = CloneNode(*target);
			parent->children.insert(parent->children.begin() + static_cast<ptrdiff_t>(childIndex) + 1, std::move(copy));
			return true;
		});
	}

	bool Move(UIDocumentAsset& document, u32 node, u32 newParent, u32 childIndex, str& error) {
		const u32 run[] = {node};
		return MoveNodes(document, run, newParent, childIndex, error);
	}

	bool MoveNodes(UIDocumentAsset& document, std::span<const u32> nodes, u32 newParent, u32 childIndex, str& error) {
		for (u32 node : nodes) {
			if (IsInSubtree(document, node, newParent)) {
				error = "an element can't be moved inside itself";
				return false;
			}
		}
		Vector<u32> moving{GetMemoryResource(MemTag::Editor)};
		GetOutermost(document, nodes, moving);
		if (moving.empty()) {
			error = "there is no element at that index";
			return false;
		}
		return Guarded(document, error, [&]() {
			UIDocumentNode* destination = Resolve(document, newParent, nullptr, nullptr);
			if (!destination) {
				error = "there is no element at that index";
				return false;
			}
			if (!destination->templateName.empty()) {
				error = "an <instance> expands its template and takes no children";
				return false;
			}
			if (!destination->text.empty()) {
				error = str("'") + UIBuilderOps::RowLabel(*destination) + "' has text content (an element can't have both)";
				return false;
			}

			NodePath destinationPath{GetMemoryResource(MemTag::Editor)};
			Vector<NodePath> paths{GetMemoryResource(MemTag::Editor)};
			if (!PathOf(document, newParent, destinationPath)) {
				error = "there is no element at that index";
				return false;
			}
			for (u32 node : moving) {
				NodePath& path = paths.emplace_back();
				if (!PathOf(document, node, path)) {
					error = "there is no element at that index";
					return false;
				}
				if (path.empty()) {
					error = "a document has 1 root element. This one can't be moved";
					return false;
				}
			}

			auto isMovingChild = [&](u32 position) {
				for (const NodePath& path : paths) {
					if (path.size() == destinationPath.size() + 1 && path.back() == position &&
						std::equal(destinationPath.begin(), destinationPath.end(), path.begin())) {
						return true;
					}
				}
				return false;
			};
			u32 neighbor = static_cast<u32>(destination->children.size());
			for (u32 position = childIndex; position < neighbor; ++position) {
				if (!isMovingChild(position)) {
					neighbor = position;
					break;
				}
			}

			Vector<UIDocumentNode> lifted{GetMemoryResource(MemTag::Editor)};
			for (size_t i = paths.size(); i-- > 0;) {
				const NodePath& path = paths[i];
				const size_t depth = path.size() - 1;
				NodePath parentPath(path.begin(), path.begin() + static_cast<ptrdiff_t>(depth),
									GetMemoryResource(MemTag::Editor));
				UIDocumentNode* parent = ResolvePath(document, parentPath);
				if (!parent || path[depth] >= parent->children.size()) {
					error = "there is no element at that index";
					return false;
				}
				lifted.push_back(std::move(parent->children[path[depth]]));
				parent->children.erase(parent->children.begin() + static_cast<ptrdiff_t>(path[depth]));

				const bool sharesPrefix =
					destinationPath.size() >= depth &&
					std::equal(path.begin(), path.begin() + static_cast<ptrdiff_t>(depth), destinationPath.begin());
				if (sharesPrefix && destinationPath.size() > depth && destinationPath[depth] > path[depth]) {
					--destinationPath[depth];
				}
				if (sharesPrefix && destinationPath.size() == depth && path[depth] < neighbor) {
					--neighbor;
				}
			}

			UIDocumentNode* placed = ResolvePath(document, destinationPath);
			if (!placed) {
				error = "there is no element at that index";
				return false;
			}
			u32 at = std::min(neighbor, static_cast<u32>(placed->children.size()));
			for (size_t i = lifted.size(); i-- > 0;) {
				placed->children.insert(placed->children.begin() + static_cast<ptrdiff_t>(at++), std::move(lifted[i]));
			}
			return true;
		});
	}

	UIDocumentNode MakeElement(std::string_view tag) {
		UIDocumentNode node;
		node.tag = str(tag);
		node.attributes = Vector<UIDocumentAttribute>{GetMemoryResource(MemTag::Asset)};
		if (const UI::WidgetDesc* widget = UI::WidgetRegistry::Find(tag)) {
			for (const UI::WidgetAttribute& attribute : widget->attributes) {
				str value;
				switch (attribute.type) {
				case UI::WidgetAttributeType::Bool:
					value = "false";
					break;
				case UI::WidgetAttributeType::Number:
					value = "0";
					break;
				case UI::WidgetAttributeType::String:
					value = str(tag);
					break;
				}
				node.attributes.push_back(UIDocumentAttribute{str(attribute.name), value});
			}
		}
		return node;
	}

	str UniqueId(const UIDocumentAsset& document, std::string_view base) {
		Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Editor)};
		UIBuilderOps::BuildRows(document, rows);
		auto taken = [&rows](std::string_view candidate) {
			for (const UIBuilderOps::Row& row : rows) {
				if (row.node->id == candidate) {
					return true;
				}
			}
			return false;
		};
		str candidate{base};
		for (u32 suffix = 2; taken(candidate); ++suffix) {
			candidate = str(base) + "-" + std::to_string(suffix);
		}
		return candidate;
	}

	namespace {

		template <typename Fn>
		bool SetField(UIDocumentAsset& document, u32 node, str& error, const Fn& write) {
			return Guarded(document, error, [&]() {
				UIDocumentNode* target = Resolve(document, node, nullptr, nullptr);
				if (!target) {
					error = "there is no element at that index";
					return false;
				}
				write(*target);
				return true;
			});
		}

	} // anonymous namespace

	bool SetId(UIDocumentAsset& document, u32 node, std::string_view id, str& error) {
		return SetField(document, node, error, [id](UIDocumentNode& target) { target.id = str(id); });
	}

	bool SetText(UIDocumentAsset& document, u32 node, std::string_view text, str& error) {
		return SetField(document, node, error, [text](UIDocumentNode& target) { target.text = str(text); });
	}

	bool SetStyle(UIDocumentAsset& document, u32 node, std::string_view style, str& error) {
		return SetField(document, node, error, [style](UIDocumentNode& target) { target.style = str(style); });
	}

	bool SetClasses(UIDocumentAsset& document, u32 node, std::span<const std::string_view> classes, str& error) {
		if (classes.size() > UI::MAX_ELEMENT_CLASSES) {
			error = str("an element can have at most ") + std::to_string(UI::MAX_ELEMENT_CLASSES) + " classes";
			return false;
		}
		Vector<str> copied{GetMemoryResource(MemTag::Editor)};
		for (std::string_view name : classes) {
			if (!name.empty()) {
				copied.push_back(str(name));
			}
		}
		return SetField(document, node, error, [&copied](UIDocumentNode& target) {
			target.classCount = 0;
			for (u8 i = 0; i < UI::MAX_ELEMENT_CLASSES; ++i) {
				target.classes[i].clear();
			}
			for (const str& name : copied) {
				target.classes[target.classCount++] = name;
			}
		});
	}

	bool SetAttribute(UIDocumentAsset& document, u32 node, std::string_view name, std::string_view value, str& error) {
		return SetField(document, node, error, [name, value](UIDocumentNode& target) {
			for (UIDocumentAttribute& attribute : target.attributes) {
				if (attribute.name == name) {
					attribute.value = str(value);
					return;
				}
			}
			target.attributes.push_back(UIDocumentAttribute{str(name), str(value)});
		});
	}

	bool CreateTemplate(UIDocumentAsset& document, u32 node, std::string_view virtualPath, str& error) {
		UIDocumentNode* target = Resolve(document, node, nullptr, nullptr);
		if (!target) {
			error = "there is no element at that index";
			return false;
		}
		if (LocationOf(document, node).parent == UIBuilderOps::INVALID_NODE) {
			error = "a document's root element is already the whole document. Make a template from something inside it";
			return false;
		}

		UIDocumentAsset extracted;
		extracted.root = CloneNode(*target);

		Path path{str(virtualPath)};
		if (Guid created = AssetDatabase::CreateAsset(path, StringHash("UIDocumentAsset")); !created.IsValid()) {
			error = str("couldn't create '") + str(virtualPath) + "' (a file may already be there)";
			return false;
		}
		Asset* asset = AssetDatabase::LoadAsset(path);
		UIDocumentAsset* created = asset && asset->GetAssetTypeId() == StringHash("UIDocumentAsset")
									   ? static_cast<UIDocumentAsset*>(asset)
									   : nullptr;
		if (!created) {
			if (asset) {
				AssetDatabase::UnloadAsset(asset);
			}
			error = str("'") + str(virtualPath) + "' was created but doesn't load as a layout document";
			return false;
		}
		created->root = std::move(extracted.root);
		if (!Validate(*created, error)) {
			AssetDatabase::UnloadAsset(created);
			return false;
		}
		AssetDatabase::SaveAsset(created);

		str alias = path.GetFilename();
		if (size_t dot = alias.find_last_of('.'); dot != str::npos) {
			alias.erase(dot);
		}
		const bool ok = Guarded(document, error, [&]() {
			UIDocumentTemplate declaration;
			declaration.src = str(virtualPath);
			declaration.document = Handle<UIDocumentAsset>(created);
			declaration.name = alias;
			for (u32 suffix = 2; FindTemplate(document, declaration.name) != nullptr; ++suffix) {
				declaration.name = alias + "-" + std::to_string(suffix);
			}
			str instanceAlias = declaration.name;
			document.templates.push_back(std::move(declaration));

			UIDocumentNode* replace = Resolve(document, node, nullptr, nullptr);
			if (!replace) {
				error = "there is no element at that index";
				return false;
			}
			UIDocumentNode instance;
			instance.tag = "instance";
			instance.templateName = instanceAlias;
			*replace = std::move(instance);
			return true;
		});
		if (!ok) {
			AssetDatabase::UnloadAsset(created);
		}
		return ok;
	}

	bool Unpack(UIDocumentAsset& document, u32 instance, bool completely, str& error) {
		return Guarded(document, error, [&]() {
			UIDocumentNode* target = Resolve(document, instance, nullptr, nullptr);
			if (!target) {
				error = "there is no element at that index";
				return false;
			}
			if (target->templateName.empty()) {
				error = str("'") + UIBuilderOps::RowLabel(*target) + "' isn't an <instance> (nothing to unpack)";
				return false;
			}
			UIDocumentNode composed;
			if (!ComposeInstance(document, *target, composed, error)) {
				return false;
			}
			*target = std::move(composed);
			if (completely) {
				bool ok = true;
				UnpackAll(document, *target, error, ok);
				if (!ok) {
					return false;
				}
			}
			PruneUnusedTemplates(document);
			return true;
		});
	}

	bool Save(UIDocumentAsset& document) {
		if (document.virtualPath.empty()) {
			SLT_LOG_WARN("Editor", "UIBuilder: this document has no path to save to");
			return false;
		}
		EditorAssetOps::SaveAsset(Handle<Asset>(&document));
		return true;
	}

} // namespace Shalltear::UIDocumentOps
