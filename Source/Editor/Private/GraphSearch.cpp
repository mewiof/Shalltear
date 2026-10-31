#include "Shalltear/Editor/GraphSearch.h"

#include <algorithm>

namespace Shalltear::GraphSearchOps {

	namespace {

		enum class Match : u32 {
			Word,
			WordStart,
			Synonym,
			Category,
			Inside,
			None
		};

		char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
		bool IsUpper(char c) { return c >= 'A' && c <= 'Z'; }
		bool IsLowerCase(char c) { return c >= 'a' && c <= 'z'; }
		bool IsWordByte(char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				   static_cast<unsigned char>(c) >= 0x80;
		}

		struct Range {
			size_t begin = 0;
			size_t end = 0;
		};

		void WordRanges(std::string_view text, Vector<Range>& out) {
			out.clear();
			constexpr size_t NONE = std::string_view::npos;
			size_t start = NONE;
			char previous = 0;
			for (size_t at = 0; at < text.size(); ++at) {
				const char c = text[at];
				if (!IsWordByte(c)) {
					if (start != NONE) {
						out.push_back(Range{start, at});
						start = NONE;
					}
					previous = 0;
					continue;
				}
				if (start != NONE && IsUpper(c) && IsLowerCase(previous)) {
					out.push_back(Range{start, at});
					start = NONE;
				}
				if (start == NONE) {
					start = at;
				}
				previous = c;
			}
			if (start != NONE) {
				out.push_back(Range{start, text.size()});
			}
		}

		void Words(std::string_view text, Vector<str>& out) {
			out.clear();
			Vector<Range> ranges{GetMemoryResource(MemTag::Editor)};
			WordRanges(text, ranges);
			for (const Range& range : ranges) {
				str word(text.substr(range.begin, range.end - range.begin));
				for (char& c : word) {
					c = Lower(c);
				}
				out.push_back(std::move(word));
			}
		}

		bool StartsWithFolded(std::string_view text, std::string_view prefix) {
			if (text.size() < prefix.size()) {
				return false;
			}
			for (size_t at = 0; at < prefix.size(); ++at) {
				if (Lower(text[at]) != Lower(prefix[at])) {
					return false;
				}
			}
			return true;
		}

		str Squash(const Vector<str>& words) {
			str joined;
			for (const str& word : words) {
				joined += word;
			}
			return joined;
		}

		bool StartsWith(std::string_view text, std::string_view prefix) {
			return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
		}

		bool AnyStartsWith(const Vector<str>& words, std::string_view term) {
			return std::any_of(words.begin(), words.end(), [term](const str& word) { return StartsWith(word, term); });
		}

		struct Scored {
			u32 index = 0;
			bool wholePrefix = false;
			u32 score = 0;
			size_t length = 0;
		};

	} // anonymous namespace

	void Rank(std::string_view query, std::span<const GraphSearchEntry> library, Vector<u32>& out) {
		out.clear();
		Vector<str> terms{GetMemoryResource(MemTag::Editor)};
		Words(query, terms);
		if (terms.empty()) {
			out.reserve(library.size());
			for (u32 index = 0; index < static_cast<u32>(library.size()); ++index) {
				out.push_back(index);
			}
			return;
		}
		const str squashedQuery = Squash(terms);

		Vector<Scored> kept{GetMemoryResource(MemTag::Editor)};
		Vector<str> name{GetMemoryResource(MemTag::Editor)};
		Vector<str> synonyms{GetMemoryResource(MemTag::Editor)};
		Vector<str> category{GetMemoryResource(MemTag::Editor)};
		for (u32 index = 0; index < static_cast<u32>(library.size()); ++index) {
			const GraphSearchEntry& entry = library[index];
			Words(entry.name, name);
			Words(entry.synonyms, synonyms);
			Words(entry.category, category);
			const str squashedName = Squash(name);

			u32 score = 0;
			bool matched = true;
			for (const str& term : terms) {
				Match best = Match::None;
				if (std::find(name.begin(), name.end(), term) != name.end()) {
					best = Match::Word;
				} else if (AnyStartsWith(name, term)) {
					best = Match::WordStart;
				} else if (AnyStartsWith(synonyms, term)) {
					best = Match::Synonym;
				} else if (AnyStartsWith(category, term)) {
					best = Match::Category;
				} else if (squashedName.find(term) != str::npos) {
					best = Match::Inside;
				}
				if (best == Match::None) {
					matched = false;
					break;
				}
				score += static_cast<u32>(best);
			}
			if (matched) {
				kept.push_back(Scored{index, StartsWith(squashedName, squashedQuery), score, entry.name.size()});
			}
		}

		std::sort(kept.begin(), kept.end(), [](const Scored& left, const Scored& right) {
			if (left.wholePrefix != right.wholePrefix) {
				return left.wholePrefix;
			}
			if (left.score != right.score) {
				return left.score < right.score;
			}
			if (left.length != right.length) {
				return left.length < right.length;
			}
			return left.index < right.index;
		});
		out.reserve(kept.size());
		for (const Scored& scored : kept) {
			out.push_back(scored.index);
		}
	}

	void Highlights(std::string_view query, std::string_view name, Vector<GraphSearchSpan>& out) {
		out.clear();
		Vector<str> terms{GetMemoryResource(MemTag::Editor)};
		Words(query, terms);
		if (terms.empty() || name.empty()) {
			return;
		}
		str joined;
		Vector<u32> from{GetMemoryResource(MemTag::Editor)};
		for (size_t at = 0; at < name.size(); ++at) {
			if (IsWordByte(name[at])) {
				joined.push_back(Lower(name[at]));
				from.push_back(static_cast<u32>(at));
			}
		}
		Vector<u8> lit(name.size(), 0, GetMemoryResource(MemTag::Editor));
		for (const str& term : terms) {
			for (size_t at = joined.find(term); at != str::npos; at = joined.find(term, at + 1)) {
				for (size_t byte = at; byte < at + term.size(); ++byte) {
					lit[from[byte]] = 1;
				}
			}
		}
		for (u32 at = 0; at < static_cast<u32>(name.size()); ++at) {
			if (lit[at] == 0) {
				continue;
			}
			if (out.empty() || out.back().end != at) {
				out.push_back(GraphSearchSpan{at, at + 1});
			} else {
				out.back().end = at + 1;
			}
		}
	}

	str Completion(std::string_view query, std::string_view name) {
		if (query.empty() || !IsWordByte(query.back())) {
			return {};
		}
		Vector<Range> words{GetMemoryResource(MemTag::Editor)};
		WordRanges(query, words);
		const Range last = words.back();
		const std::string_view typed = query.substr(last.begin, last.end - last.begin);
		WordRanges(name, words);
		for (const Range& range : words) {
			const std::string_view word = name.substr(range.begin, range.end - range.begin);
			if (StartsWithFolded(word, typed)) {
				return str(word.substr(typed.size()));
			}
		}
		return {};
	}

} // namespace Shalltear::GraphSearchOps
