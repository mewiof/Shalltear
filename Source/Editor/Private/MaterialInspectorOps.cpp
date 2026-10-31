#include "Shalltear/Editor/MaterialInspectorOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/ProjectLayout.h"
#include "Shalltear/Core/StringUtil.h"

#include <algorithm>
#include <filesystem>

namespace Shalltear::MaterialInspectorOps {

	namespace {

		namespace fs = std::filesystem;

		Row MakeMemberRow(const ShaderAsset& shader, const UniformMember& member) {
			Row row;
			row.name = member.name;
			row.member = member.type;
			row.label = str(shader.FindMemberLabel(member.name));

			switch (member.type) {
			case UniformMemberType::Float:
			case UniformMemberType::Int:
			case UniformMemberType::Bool: {
				if (shader.FindMemberHint(member.name) == ShaderHint::Toggle) {
					row.kind = RowKind::Toggle;
				} else if (const ShaderMemberRange* range = shader.FindMemberRange(member.name)) {
					row.kind = RowKind::Slider;
					row.minValue = range->minValue;
					row.maxValue = range->maxValue;
				} else {
					row.kind = RowKind::Number;
				}
				break;
			}
			case UniformMemberType::Vec3:
			case UniformMemberType::Vec4: {
				switch (shader.FindMemberHint(member.name)) {
				case ShaderHint::Color:
					row.kind = RowKind::Color;
					break;
				case ShaderHint::HDR:
					row.kind = RowKind::HDRColor;
					break;
				case ShaderHint::None:
				case ShaderHint::Toggle:
				case ShaderHint::Hidden:
					row.kind = RowKind::Vector;
					break;
				}
				break;
			}
			case UniformMemberType::Vec2:
				row.kind = RowKind::Vector;
				break;
			case UniformMemberType::Unsupported:
				row.kind = RowKind::Unsupported;
				break;
			}
			return row;
		}

		void AppendIfAny(Vector<Group>& out, Group&& group) {
			if (group.rows.empty()) {
				return;
			}
			out.push_back(std::move(group));
		}

		bool Claimed(const Vector<str>& claimed, std::string_view name) {
			for (const str& taken : claimed) {
				if (taken == name) {
					return true;
				}
			}
			return false;
		}

		bool Hidden(const ShaderAsset& shader, std::string_view name) {
			return shader.FindMemberHint(name) == ShaderHint::Hidden;
		}

		Row MakeTextureRow(const ShaderAsset& shader, const str& slot) {
			Row row;
			row.name = slot;
			row.kind = RowKind::Texture;
			row.label = str(shader.FindMemberLabel(slot));
			return row;
		}

		bool SpeaksTheMaterialVocabulary(const ShaderPragmas& pragmas) {
			return pragmas.integrator != ShaderTemplate::None || !pragmas.memberDefaults.empty() ||
				   !pragmas.textureDefaults.empty() || !pragmas.keywords.empty() ||
				   !pragmas.memberRanges.empty() || !pragmas.memberHints.empty() ||
				   !pragmas.memberGroups.empty() || !pragmas.memberLabels.empty();
		}

		struct ShaderSource {
			str extension;
			ShaderSourceFn isMaterialShader = nullptr;
			const void* owner = nullptr;
		};

		struct ShaderSources {
			Vector<ShaderSource> entries{GetMemoryResource(MemTag::Editor)};
		};

		ShaderSources* _shaderSources = nullptr;

		ShaderSource* FindShaderSource(std::string_view lowered) {
			if (!_shaderSources) {
				return nullptr;
			}
			for (ShaderSource& entry : _shaderSources->entries) {
				if (entry.extension == lowered) {
					return &entry;
				}
			}
			return nullptr;
		}

		bool OfferedAsMaterialShader(std::string_view extension, std::string_view source, const Path& virtualPath) {
			if (extension == ".hlsl") {
				ShaderPragmas pragmas;
				ShaderAsset::ParsePragmas(source, virtualPath, pragmas);
				return !pragmas.kernel && SpeaksTheMaterialVocabulary(pragmas);
			}
			const ShaderSource* registered = FindShaderSource(extension);
			return registered && registered->isMaterialShader(source);
		}

		void CollectShadersUnder(const fs::path& physicalRoot, std::string_view virtualPrefix, bool engine,
								 Vector<ShaderChoice>& out) {
			std::error_code ec;
			if (!fs::is_directory(physicalRoot, ec) || ec) {
				return;
			}
			for (fs::recursive_directory_iterator it(physicalRoot, fs::directory_options::skip_permission_denied, ec),
				 end;
				 it != end; it.increment(ec)) {
				if (ec) {
					break;
				}
				if (!it->is_regular_file(ec) || ec) {
					continue;
				}
				const fs::path& file = it->path();
				str extension = StringUtil::ToLower(file.extension().generic_string());
				if (extension != ".hlsl" && !FindShaderSource(extension)) {
					continue;
				}
				str relative = fs::relative(file, physicalRoot, ec).generic_string();
				if (ec || relative.empty()) {
					continue;
				}
				str virtualPath = str(virtualPrefix) + relative;

				Guid guid = AssetDatabase::LookupGuid(Path(virtualPath));
				if (!guid.IsValid()) {
					continue;
				}
				str source = FileSystem::ReadTextFile(Path(virtualPath), MemTag::Editor);
				if (source.empty() || !OfferedAsMaterialShader(extension, source, Path(virtualPath))) {
					continue;
				}

				ShaderChoice choice;
				choice.guid = guid;
				choice.displayName = str(file.stem().generic_string());
				choice.virtualPath = std::move(virtualPath);
				choice.engine = engine;
				out.push_back(std::move(choice));
			}
		}

	} // anonymous namespace

	void CollectShaderChoices(Vector<ShaderChoice>& out) {
		out.clear();

		CollectShadersUnder(fs::path(FileSystem::Resolve(Path("engine://Shaders")).ToString()),
							"engine://Shaders/", true, out);
		size_t projectBegin = out.size();

		for (std::string_view contentRoot : ProjectLayout::GetContentRoots()) {
			str prefix = str("project://") + str(contentRoot) + "/";
			Path physical = FileSystem::Resolve(Path(str("project://") + str(contentRoot)));
			if (physical.ToString().empty()) {
				continue;
			}
			CollectShadersUnder(fs::path(physical.ToString()), prefix, false, out);
		}

		auto byName = [](const ShaderChoice& a, const ShaderChoice& b) { return a.displayName < b.displayName; };
		std::stable_sort(out.begin(), out.begin() + static_cast<ptrdiff_t>(projectBegin), byName);
		std::stable_sort(out.begin() + static_cast<ptrdiff_t>(projectBegin), out.end(), byName);

		Vector<u8> collides(GetMemoryResource(MemTag::Editor));
		collides.resize(out.size(), 0);
		for (size_t i = 0; i < out.size(); ++i) {
			for (size_t j = i + 1; j < out.size(); ++j) {
				if (out[i].displayName == out[j].displayName) {
					collides[i] = 1;
					collides[j] = 1;
				}
			}
		}
		for (size_t i = 0; i < out.size(); ++i) {
			if (collides[i]) {
				out[i].displayName = out[i].virtualPath;
			}
		}
	}

	void BuildLayout(const ShaderAsset& shader, Vector<Group>& out) {
		out.clear();

		const bool sky = shader.integrator == ShaderTemplate::Sky;

		if (!sky) {
			Group surface;
			surface.name = str(SurfaceOptionsGroup);
			surface.builtIn = true;
			surface.rows.push_back(Row{str("Blend"), RowKind::Blend, UniformMemberType::Unsupported, 0.0f, 1.0f, {}});
			surface.rows.push_back(Row{str("Cull"), RowKind::Cull, UniformMemberType::Unsupported, 0.0f, 1.0f, {}});
			surface.rows.push_back(Row{str("Alpha Clip"), RowKind::AlphaClip, UniformMemberType::Unsupported, 0.0f, 1.0f, {}});
			out.push_back(std::move(surface));
		}

		const UniformBlockLayout* layout = shader.GetMaterialParamLayout();
		Vector<str> textureSlots{GetMemoryResource(MemTag::Editor)};
		shader.GetMaterialTextureSlots(textureSlots);

		Vector<str> claimed{GetMemoryResource(MemTag::Editor)};

		for (const ShaderMemberGroup& declared : shader.memberGroups) {
			Group group;
			group.name = declared.name;
			for (const str& memberName : declared.members) {
				if (Claimed(claimed, memberName) || Hidden(shader, memberName)) {
					continue;
				}
				const UniformMember* member = nullptr;
				if (layout) {
					for (const UniformMember& candidate : layout->members) {
						if (candidate.name == memberName) {
							member = &candidate;
							break;
						}
					}
				}
				if (member) {
					group.rows.push_back(MakeMemberRow(shader, *member));
					claimed.push_back(memberName);
					continue;
				}
				for (const str& slot : textureSlots) {
					if (slot == memberName) {
						group.rows.push_back(MakeTextureRow(shader, slot));
						claimed.push_back(memberName);
						break;
					}
				}
			}
			AppendIfAny(out, std::move(group));
		}

		Group parameters;
		parameters.name = str(ParametersGroup);
		if (layout) {
			for (const UniformMember& member : layout->members) {
				if (member.name.empty() || Claimed(claimed, member.name) || Hidden(shader, member.name)) {
					continue;
				}
				parameters.rows.push_back(MakeMemberRow(shader, member));
			}
		}
		AppendIfAny(out, std::move(parameters));

		Group textures;
		textures.name = str(TexturesGroup);
		for (const str& slot : textureSlots) {
			if (Claimed(claimed, slot) || Hidden(shader, slot)) {
				continue;
			}
			textures.rows.push_back(MakeTextureRow(shader, slot));
		}
		AppendIfAny(out, std::move(textures));

		Group keywords;
		keywords.name = str(KeywordsGroup);
		for (const str& keyword : shader.keywords) {
			if (keyword == ShaderAlphaClipKeyword) {
				continue;
			}
			const ShaderKeywordBinding* bound = nullptr;
			for (const ShaderKeywordBinding& binding : shader.keywordBindings) {
				bound = binding.keyword == keyword ? &binding : bound;
			}
			if (bound && bound->value >= 0) {
				const bool drawn = std::any_of(keywords.rows.begin(), keywords.rows.end(), [&](const Row& row) {
					return row.kind == RowKind::KeywordEnum && row.keywordMember == bound->member;
				});
				if (drawn) {
					continue;
				}
				Row group;
				group.name = bound->member;
				group.kind = RowKind::KeywordEnum;
				group.keywordMember = bound->member;
				group.label = str(shader.FindMemberLabel(bound->member));
				for (const ShaderKeywordBinding& binding : shader.keywordBindings) {
					if (binding.member != bound->member) {
						continue;
					}
					if (group.entries.size() <= static_cast<size_t>(binding.value)) {
						group.entries.resize(static_cast<size_t>(binding.value) + 1);
					}
					group.entries[static_cast<size_t>(binding.value)] = {binding.keyword, binding.entry.empty() ? binding.keyword : binding.entry};
				}
				keywords.rows.push_back(std::move(group));
				continue;
			}
			Row row;
			row.name = keyword;
			row.kind = RowKind::Keyword;
			row.keywordMember = str(shader.FindKeywordMember(keyword));
			if (!row.keywordMember.empty()) {
				row.label = str(shader.FindMemberLabel(row.keywordMember));
			}
			keywords.rows.push_back(std::move(row));
		}
		AppendIfAny(out, std::move(keywords));

		if (!sky) {
			Group advanced;
			advanced.name = str(AdvancedOptionsGroup);
			advanced.builtIn = true;
			advanced.rows.push_back(Row{str("Sorting Priority"), RowKind::SortingPriority, UniformMemberType::Unsupported, 0.0f, 1.0f, {}});
			out.push_back(std::move(advanced));
		}
	}

	bool RegisterShaderSource(std::string_view extension, ShaderSourceFn isMaterialShader, const void* owner) {
		if (extension.size() < 2 || extension.front() != '.' || !isMaterialShader) {
			return false;
		}
		const str lowered = StringUtil::ToLower(extension);
		if (lowered == ".hlsl") {
			return false;
		}
		if (ShaderSource* held = FindShaderSource(lowered)) {
			if (held->owner != owner) {
				SLT_LOG_WARN("Editor", "'{}' is already a shader source another module describes (2nd registration ignored)", lowered);
				return false;
			}
			held->isMaterialShader = isMaterialShader;
			return true;
		}
		if (!_shaderSources) {
			_shaderSources = New<ShaderSources>(MemTag::Editor);
		}
		_shaderSources->entries.push_back(ShaderSource{lowered, isMaterialShader, owner});
		return true;
	}

	u32 UnregisterShaderSourcesOwnedBy(const void* owner) {
		if (!owner || !_shaderSources) {
			return 0;
		}
		return static_cast<u32>(std::erase_if(_shaderSources->entries, [owner](const ShaderSource& entry) { return entry.owner == owner; }));
	}

	void TerminateShaderSources() {
		if (_shaderSources) {
			Delete(_shaderSources);
			_shaderSources = nullptr;
		}
	}

} // namespace Shalltear::MaterialInspectorOps
