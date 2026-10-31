#pragma once

#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Style.h"

#include <span>
#include <string_view>

namespace Shalltear::EditorInternal {

	struct ParsedSelector {
		StringHash tag;
		StringHash id;
		StringHash klass;
		u8 pseudo = UI::PSEUDO_NONE;
		i32 specificity = 0;
		bool valid = false;
	};

	inline ParsedSelector ParseSelectorText(std::string_view selector) {
		ParsedSelector out;
		if (selector.empty()) {
			return out;
		}
		UI::StyleSheet probe;
		probe.Parse(str(selector) + "{}");
		if (probe.GetRules().size() != 1) {
			return out;
		}
		const UI::StyleRule& parsed = probe.GetRules()[0];
		out.tag = parsed.targetTag;
		out.id = parsed.targetId;
		out.klass = parsed.targetClass;
		out.pseudo = parsed.pseudoState;
		out.specificity = parsed.specificity;
		out.valid = true;
		return out;
	}

	inline bool SelectorMatchesIdentity(const ParsedSelector& selector, StringHash tag, StringHash id,
										std::span<const StringHash> classes) {
		if (!selector.valid) {
			return false;
		}
		if (selector.tag.GetValue() != 0 && selector.tag != tag) {
			return false;
		}
		if (selector.id.GetValue() != 0 && selector.id != id) {
			return false;
		}
		if (selector.klass.GetValue() != 0) {
			for (StringHash klass : classes) {
				if (klass == selector.klass) {
					return true;
				}
			}
			return false;
		}
		return true;
	}

} // namespace Shalltear::EditorInternal
