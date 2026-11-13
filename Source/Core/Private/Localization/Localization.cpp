#include "Shalltear/Core/Localization.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Settings.h"

#include "Localization/LocalizationInternal.h"
#include "Localization/LocalizationSettings.h"
#include "Platform/Process.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct LocalizationState {
			str language;
			bool followSystem = true;
			str systemLanguage;
			bool systemRead = false;
			Vector<Handle<StringTableAsset>> added{GetMemoryResource(MemTag::Core)};
			Vector<Handle<StringTableAsset>> found{GetMemoryResource(MemTag::Core)};
			bool scanned = false;
			u32 changes = 0;

			str candidatesFor;
			Vector<str> candidates{GetMemoryResource(MemTag::Core)};
		};

		LocalizationState* _state = nullptr;

		LocalizationState& State() {
			if (!_state) {
				_state = New<LocalizationState>(MemTag::Core);
			}
			return *_state;
		}

		const LocalizationSettings* Category() {
			return Settings::DeclareCategory<LocalizationSettings>("Localization", SettingScope::Project);
		}

		char Lower(char c) {
			return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
		}

		char Upper(char c) {
			return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
		}

		void AppendCandidates(std::string_view code, Vector<str>& out) {
			auto push = [&out](std::string_view candidate) {
				if (!candidate.empty() && std::find(out.begin(), out.end(), candidate) == out.end()) {
					out.emplace_back(candidate);
				}
			};
			push(code);
			if (code == "zh-CN" || code == "zh-SG") {
				push("zh-Hans");
			} else if (code == "zh-TW" || code == "zh-HK" || code == "zh-MO") {
				push("zh-Hant");
			}
			for (size_t dash = code.rfind('-'); dash != std::string_view::npos; dash = code.rfind('-')) {
				code = code.substr(0, dash);
				push(code);
			}
		}

		bool AliveAll(const Vector<Handle<StringTableAsset>>& tables) {
			return std::all_of(tables.begin(), tables.end(), [](const Handle<StringTableAsset>& table) { return table.Get() != nullptr; });
		}

		void Rescan(LocalizationState& state) {
			for (const Handle<StringTableAsset>& table : state.found) {
				if (StringTableAsset* loaded = table.Get()) {
					AssetDatabase::UnloadAsset(loaded);
				}
			}
			state.found.clear();
			Vector<Guid> ids{GetMemoryResource(MemTag::Core)};
			AssetDatabase::FindAssets(StringTableAsset::GetReflectType()->name, ids);
			for (const Guid& id : ids) {
				Asset* loaded = AssetDatabase::LoadAssetByGuid(id);
				if (!loaded) {
					continue;
				}
				if (loaded->GetAssetTypeId() != StringTableAsset::GetReflectType()->name) {
					AssetDatabase::UnloadAsset(loaded);
					continue;
				}
				state.found.emplace_back(static_cast<StringTableAsset*>(loaded));
			}
			state.scanned = true;
			++state.changes;
		}

		LocalizationState& Ready() {
			LocalizationState& state = State();
			if (!state.scanned || !AliveAll(state.found)) {
				Rescan(state);
			}
			return state;
		}

		const Vector<str>& Candidates(LocalizationState& state) {
			const str language = Localization::GetLanguage();
			const str fallback = Localization::GetDefaultLanguage();
			const str signature = language + "|" + fallback;
			if (state.candidatesFor != signature || state.candidates.empty()) {
				state.candidatesFor = signature;
				state.candidates.clear();
				AppendCandidates(language, state.candidates);
				AppendCandidates(fallback, state.candidates);
			}
			return state.candidates;
		}

	} // anonymous namespace

	namespace Localization {

		str NormalizeLanguage(std::string_view code) {
			const size_t cut = code.find_first_of(".@");
			code = code.substr(0, cut);
			while (!code.empty() && (code.front() == ' ' || code.front() == '\t')) {
				code.remove_prefix(1);
			}
			while (!code.empty() && (code.back() == ' ' || code.back() == '\t')) {
				code.remove_suffix(1);
			}
			if (code == "C" || code == "POSIX") {
				return {};
			}
			str out;
			size_t part = 0;
			size_t start = 0;
			for (size_t i = 0; i <= code.size(); ++i) {
				if (i < code.size() && code[i] != '-' && code[i] != '_') {
					continue;
				}
				const std::string_view piece = code.substr(start, i - start);
				if (!piece.empty()) {
					if (!out.empty()) {
						out.push_back('-');
					}
					for (size_t c = 0; c < piece.size(); ++c) {
						if (part == 0) {
							out.push_back(Lower(piece[c]));
						} else if (piece.size() == 4) {
							out.push_back(c == 0 ? Upper(piece[c]) : Lower(piece[c]));
						} else if (piece.size() == 2) {
							out.push_back(Upper(piece[c]));
						} else {
							out.push_back(piece[c]);
						}
					}
					++part;
				}
				start = i + 1;
			}
			return out;
		}

		void SetLanguage(std::string_view code) {
			LocalizationState& state = State();
			const str normalized = NormalizeLanguage(code);
			if (!state.followSystem && state.language == normalized) {
				return;
			}
			state.language = normalized;
			state.followSystem = false;
			++state.changes;
		}

		void UseSystemLanguage() {
			LocalizationState& state = State();
			if (state.followSystem) {
				return;
			}
			state.followSystem = true;
			++state.changes;
		}

		str GetSystemLanguage() {
			LocalizationState& state = State();
			if (!state.systemRead) {
				state.systemLanguage = NormalizeLanguage(Platform::GetUserLanguage());
				state.systemRead = true;
			}
			return state.systemLanguage;
		}

		str GetLanguage() {
			const LocalizationState& state = State();
			return state.followSystem ? GetSystemLanguage() : state.language;
		}

		str GetDefaultLanguage() {
			const LocalizationSettings* settings = Category();
			return NormalizeLanguage(settings ? std::string_view(settings->defaultLanguage) : std::string_view("en"));
		}

		bool TryTranslate(std::string_view key, str& out) {
			if (key.empty()) {
				return false;
			}
			LocalizationState& state = Ready();
			for (const str& language : Candidates(state)) {
				auto search = [&](const Handle<StringTableAsset>& handle) {
					const StringTableAsset* table = handle.Get();
					const i32 column = table ? table->FindLanguage(language) : -1;
					const std::string_view text = column >= 0 ? table->Find(key, static_cast<u32>(column)) : std::string_view{};
					if (text.empty()) {
						return false;
					}
					out.assign(text.begin(), text.end());
					return true;
				};
				if (std::any_of(state.added.rbegin(), state.added.rend(), search) ||
					std::any_of(state.found.begin(), state.found.end(), search)) {
					return true;
				}
			}
			return false;
		}

		str Translate(std::string_view key) {
			str text;
			return TryTranslate(key, text) ? text : str(key);
		}

		void AddTable(Handle<StringTableAsset> table) {
			LocalizationState& state = State();
			if (!table.Get()) {
				return;
			}
			std::erase(state.added, table);
			state.added.push_back(table);
			++state.changes;
		}

		void RemoveTable(Handle<StringTableAsset> table) {
			LocalizationState& state = State();
			const size_t before = state.added.size();
			std::erase(state.added, table);
			if (state.added.size() != before) {
				++state.changes;
			}
		}

		void ReloadTables() {
			Rescan(State());
		}

		u32 GetGeneration() {
			const LocalizationState& state = Ready();
			u32 generation = state.changes;
			for (const Vector<Handle<StringTableAsset>>* list : {&state.added, &state.found}) {
				for (const Handle<StringTableAsset>& handle : *list) {
					const StringTableAsset* table = handle.Get();
					generation += table ? table->version * 2654435761u + 1u : 0u;
				}
			}
			return generation;
		}

	} // namespace Localization

	namespace LocalizationInternal {

		void Terminate() {
			if (_state) {
				Delete(_state);
				_state = nullptr;
			}
		}

	} // namespace LocalizationInternal

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::LocalizationSettings)
