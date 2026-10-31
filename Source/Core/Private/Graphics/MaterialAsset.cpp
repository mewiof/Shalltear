#include "Shalltear/Core/Graphics/MaterialAsset.h"

#include "Asset/EngineBuiltins.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <algorithm>
#include <cstring>
#include <deque>

namespace Shalltear {

	namespace {
		constexpr const char* STANDARD_SHADER_PATH = "engine://Shaders/Lit.hlsl";

		void RefreshKeywordHash(MaterialAsset& material) {
			Vector<str> effective{GetMemoryResource(MemTag::Asset)};
			material.GetEffectiveKeywords(effective);
			material.keywordHash = ShaderAsset::KeywordSetHash({effective.data(), effective.size()});
		}

		constexpr Guid RetiredStandardLit{0x5f787b0c8a4e9a5d, 0x03e3437f042bfc8d};

		f32 RoughnessFromShininess(f32 shininess) {
			return Math::Clamp(1.0f - (shininess - 8.0f) / 120.0f, 0.0f, 1.0f);
		}

		void DropParam(MaterialAsset& material, std::string_view name) {
			const StringHash hash(name);
			auto found = std::find_if(material.params.begin(), material.params.end(),
									  [hash](const MaterialAsset::Param& param) { return param.hash == hash; });
			if (found != material.params.end()) {
				material.params.erase(found);
			}
		}

		void MigrateFromStandardLit(MaterialAsset& material, const Path& sourcePath) {
			const f32 shininess = material.GetFloat(StringHash("shininess"), 32.0f);
			const f32 roughness = RoughnessFromShininess(shininess);

			material.shaderGuid = EngineBuiltins::LitShader;
			material.SetFloat("roughness", roughness);
			material.SetFloat("metallic", 0.0f);
			DropParam(material, "specular");
			DropParam(material, "shininess");

			if (!material.Export(sourcePath)) {
				SLT_LOG_WARN("Graphics", "MaterialAsset: '{}' migrated to Lit in memory but couldn't be rewritten", sourcePath.ToString());
				return;
			}
			SLT_LOG_INFO("Graphics", "MaterialAsset: migrated '{}' from StandardLit to Lit (roughness {:.3f} from shininess {:.1f}, metallic 0, specular dropped)", sourcePath.ToString(), roughness, shininess);
		}

		Handle<TextureAsset> ResolveTexture(const Guid& texture, const str& material) {
			if (!texture.IsValid()) {
				return {};
			}
			Asset* loaded = AssetDatabase::LoadAssetByGuid(texture);
			if (loaded && loaded->GetAssetTypeId() == StringHash("TextureAsset")) {
				return Handle<TextureAsset>(static_cast<TextureAsset*>(loaded));
			}
			if (loaded) {
				AssetDatabase::UnloadAsset(loaded);
			} else {
				SLT_LOG_WARN("Graphics", "MaterialAsset '{}': texture {} not found", material, texture);
			}
			return {};
		}
	} // anonymous namespace

	MaterialAsset::~MaterialAsset() = default;

	const MaterialAsset::Param* MaterialAsset::FindParam(StringHash name) const {
		for (const Param& param : params) {
			if (param.hash == name) {
				return &param;
			}
		}
		return nullptr;
	}

	void MaterialAsset::SetParam(std::string_view name, const Param& value) {
		StringHash hash(name);
		for (Param& param : params) {
			if (param.hash == hash) {
				param.kind = value.kind;
				std::memcpy(param.numbers, value.numbers, sizeof(param.numbers));
				param.integer = value.integer;
				return;
			}
		}
		Param added = value;
		added.name = str(name);
		added.hash = hash;
		params.push_back(std::move(added));
	}

	MaterialAsset::Param MaterialAsset::MakeParam(UniformMemberType kind) {
		Param param;
		param.kind = kind;
		return param;
	}

	MaterialAsset::Param MaterialAsset::MakeParam(UniformMemberType kind, const ShaderMemberDefault& declared) {
		Param param = MakeParam(kind);
		u32 floatCount = UniformMemberFloatCount(kind);
		if (floatCount > 0) {
			for (u32 i = 0; i < floatCount && i < declared.count; ++i) {
				param.numbers[i] = declared.numbers[i];
			}
		} else if (declared.count > 0 && (kind == UniformMemberType::Int || kind == UniformMemberType::Bool)) {
			param.integer = static_cast<i32>(declared.numbers[0]);
		}
		return param;
	}

	f32 MaterialAsset::GetFloat(StringHash name, f32 fallback) const {
		const Param* param = FindParam(name);
		return param && param->kind == UniformMemberType::Float ? param->numbers[0] : fallback;
	}

	Color MaterialAsset::GetColor(StringHash name, Color fallback) const {
		const Param* param = FindParam(name);
		if (!param || param->kind != UniformMemberType::Vec4) {
			return fallback;
		}
		return Color(param->numbers[0], param->numbers[1], param->numbers[2], param->numbers[3]);
	}

	Handle<TextureAsset> MaterialAsset::GetTexture(StringHash name) const {
		for (const TextureParam& param : textures) {
			if (param.hash == name) {
				return param.resolved;
			}
		}
		return {};
	}

	void MaterialAsset::SetFloat(std::string_view name, f32 value) {
		Param param = MakeParam(UniformMemberType::Float);
		param.numbers[0] = value;
		SetParam(name, param);
	}

	void MaterialAsset::SetColor(std::string_view name, Color value) {
		Param param = MakeParam(UniformMemberType::Vec4);
		param.numbers[0] = value.r;
		param.numbers[1] = value.g;
		param.numbers[2] = value.b;
		param.numbers[3] = value.a;
		SetParam(name, param);
	}

	void MaterialAsset::SetVec2(std::string_view name, Vec2 value) {
		Param param = MakeParam(UniformMemberType::Vec2);
		param.numbers[0] = value.x;
		param.numbers[1] = value.y;
		SetParam(name, param);
	}

	void MaterialAsset::SetVec3(std::string_view name, Vec3 value) {
		Param param = MakeParam(UniformMemberType::Vec3);
		param.numbers[0] = value.x;
		param.numbers[1] = value.y;
		param.numbers[2] = value.z;
		SetParam(name, param);
	}

	void MaterialAsset::SetInt(std::string_view name, i32 value) {
		Param param = MakeParam(UniformMemberType::Int);
		param.integer = value;
		SetParam(name, param);
	}

	void MaterialAsset::SetTexture(std::string_view name, const Guid& texture) {
		StringHash hash(name);
		auto found = std::find_if(textures.begin(), textures.end(),
								  [hash](const TextureParam& param) { return param.hash == hash; });
		if (found == textures.end()) {
			textures.push_back({str(name), hash, {}, {}});
			found = textures.end() - 1;
		}
		const Handle<TextureAsset> previous = found->resolved;
		found->texture = texture;
		found->resolved = {};
		if (GetLoadState() != AssetLoadState::Loaded) {
			return;
		}
		found->resolved = ResolveTexture(texture, virtualPath);
		if (Asset* held = previous.Get()) {
			AssetDatabase::UnloadAsset(held);
		}
	}

	void MaterialAsset::SetKeywords(std::span<const str> enabled) {
		keywords.clear();
		for (const str& keyword : enabled) {
			if (!keyword.empty()) {
				keywords.push_back(keyword);
			}
		}
		std::sort(keywords.begin(), keywords.end());
		keywords.erase(std::unique(keywords.begin(), keywords.end()), keywords.end());
		RefreshKeywordHash(*this);
	}

	void MaterialAsset::GetEffectiveKeywords(Vector<str>& out) const {
		out.assign(keywords.begin(), keywords.end());
		if (alphaClipThreshold > 0.0f) {
			out.push_back(str(ShaderAlphaClipKeyword));
		}
		std::sort(out.begin(), out.end());
		out.erase(std::unique(out.begin(), out.end()), out.end());
	}

	void MaterialAsset::SetAlphaClipThreshold(f32 threshold) {
		alphaClipThreshold = threshold;
		RefreshKeywordHash(*this);
	}

	RHI::ShaderRenderState MaterialAsset::ResolveRenderState(const RHI::ShaderRenderState& shaderState) const {
		RHI::ShaderRenderState resolved = shaderState;
		if (blend) {
			resolved.blend = *blend;
		}
		if (cull) {
			resolved.cull = *cull;
		}
		if (resolved.blend != RHI::BlendMode::Opaque && resolved.depth == RHI::DepthMode::TestWrite) {
			resolved.depth = RHI::DepthMode::TestOnly;
		}
		return resolved;
	}

	void MaterialAsset::PackBlock(const UniformBlockLayout& layout, std::span<u8> out,
								  std::span<const ShaderMemberDefault> defaults) const {
		if (out.size() < layout.sizeBytes) {
			return;
		}
		u8* bytes = out.data();
		std::memset(bytes, 0, layout.sizeBytes);
		for (const UniformMember& member : layout.members) {
			if (member.name.empty() || member.offset + member.sizeBytes > layout.sizeBytes) {
				continue;
			}
			const StringHash name(std::string_view{member.name});
			const Param* param = FindParam(name);

			Param declared;
			if (!param) {
				for (const ShaderMemberDefault& fallback : defaults) {
					if (StringHash(std::string_view{fallback.name}) == name) {
						declared = MakeParam(member.type, fallback);
						param = &declared;
						break;
					}
				}
			}

			if (!param || param->kind != member.type) {
				continue;
			}
			u32 floatCount = UniformMemberFloatCount(member.type);
			if (floatCount > 0) {
				std::memcpy(bytes + member.offset, param->numbers, std::min<u32>(member.sizeBytes, floatCount * static_cast<u32>(sizeof(f32))));
				continue;
			}
			switch (member.type) {
			case UniformMemberType::Int:
				std::memcpy(bytes + member.offset, &param->integer, std::min<u32>(member.sizeBytes, sizeof(i32)));
				break;
			case UniformMemberType::Bool: {
				u32 value = param->integer != 0 ? 1u : 0u;
				std::memcpy(bytes + member.offset, &value, std::min<u32>(member.sizeBytes, sizeof(u32)));
				break;
			}
			default:
				break;
			}
		}
	}

	bool MaterialAsset::Import(const Path& sourcePath) {
		str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		if (text.empty()) {
			SLT_LOG_ERROR("Graphics", "MaterialAsset: '{}' unreadable/empty", sourcePath.ToString());
			return false;
		}
		LinearAllocator arena(text.size() + 64, MemTag::Asset);
		JSONValue root = JSONParse(text, arena);
		if (!root.IsObject()) {
			SLT_LOG_ERROR("Graphics", "MaterialAsset: '{}' isn't a JSON object", sourcePath.ToString());
			return false;
		}

		shaderGuid = Guid::FromString(root["shader"].AsString());

		params.clear();
		textures.clear();

		const JSONValue& floatObject = root["floats"];
		for (size_t i = 0; i < floatObject.MemberCount(); ++i) {
			std::string_view name = floatObject.KeyAt(i);
			SetFloat(name, static_cast<f32>(floatObject.ChildAt(i).AsNumber()));
		}
		const JSONValue& colorObject = root["colors"];
		for (size_t i = 0; i < colorObject.MemberCount(); ++i) {
			std::string_view name = colorObject.KeyAt(i);
			const JSONValue& value = colorObject.ChildAt(i);
			Color color(
				static_cast<f32>(value[size_t{0}].AsNumber(1.0)),
				static_cast<f32>(value[size_t{1}].AsNumber(1.0)),
				static_cast<f32>(value[size_t{2}].AsNumber(1.0)),
				static_cast<f32>(value[size_t{3}].AsNumber(1.0)));
			SetColor(name, color);
		}
		const JSONValue& vec2Object = root["vec2s"];
		for (size_t i = 0; i < vec2Object.MemberCount(); ++i) {
			const JSONValue& value = vec2Object.ChildAt(i);
			SetVec2(vec2Object.KeyAt(i), Vec2(static_cast<f32>(value[size_t{0}].AsNumber()),
											  static_cast<f32>(value[size_t{1}].AsNumber())));
		}
		const JSONValue& vec3Object = root["vec3s"];
		for (size_t i = 0; i < vec3Object.MemberCount(); ++i) {
			const JSONValue& value = vec3Object.ChildAt(i);
			SetVec3(vec3Object.KeyAt(i), Vec3(static_cast<f32>(value[size_t{0}].AsNumber()),
											  static_cast<f32>(value[size_t{1}].AsNumber()),
											  static_cast<f32>(value[size_t{2}].AsNumber())));
		}
		const JSONValue& intObject = root["ints"];
		for (size_t i = 0; i < intObject.MemberCount(); ++i) {
			SetInt(intObject.KeyAt(i), static_cast<i32>(intObject.ChildAt(i).AsNumber()));
		}
		const JSONValue& textureObject = root["textures"];
		for (size_t i = 0; i < textureObject.MemberCount(); ++i) {
			std::string_view name = textureObject.KeyAt(i);
			SetTexture(name, Guid::FromString(textureObject.ChildAt(i).AsString()));
		}

		blend.reset();
		cull.reset();
		const JSONValue& blendValue = root["blend"];
		if (blendValue.IsString()) {
			RHI::BlendMode parsed = RHI::BlendMode::Opaque;
			if (RHI::ParseBlendMode(blendValue.AsString(), parsed)) {
				blend = parsed;
			} else {
				SLT_LOG_WARN("Graphics", "MaterialAsset: '{}' has an unknown blend mode '{}'",
							 sourcePath.ToString(), str(blendValue.AsString()));
			}
		}
		const JSONValue& cullValue = root["cull"];
		if (cullValue.IsString()) {
			RHI::CullMode parsed = RHI::CullMode::Back;
			if (RHI::ParseCullMode(cullValue.AsString(), parsed)) {
				cull = parsed;
			} else {
				SLT_LOG_WARN("Graphics", "MaterialAsset: '{}' has an unknown cull mode '{}'",
							 sourcePath.ToString(), str(cullValue.AsString()));
			}
		}
		SetAlphaClipThreshold(static_cast<f32>(root["alphaClip"].AsNumber(0.0)));
		sortingPriority = static_cast<i32>(root["sortingPriority"].AsNumber(0.0));

		const JSONValue& keywordArray = root["keywords"];
		Vector<str> enabled{GetMemoryResource(MemTag::Asset)};
		for (size_t i = 0; i < keywordArray.Size(); ++i) {
			enabled.push_back(str(keywordArray[i].AsString()));
		}
		SetKeywords({enabled.data(), enabled.size()});

		if (shaderGuid == RetiredStandardLit) {
			MigrateFromStandardLit(*this, sourcePath);
		}
		return true;
	}

	bool MaterialAsset::Export(const Path& destPath) const {
		std::deque<str> pool;
		JSONValue root = JSONValue::MakeObject();

		pool.push_back(shaderGuid.ToString());
		root.AddMember("shader", JSONValue::MakeString(pool.back()));

		JSONValue floatObject = JSONValue::MakeObject();
		JSONValue colorObject = JSONValue::MakeObject();
		JSONValue vec2Object = JSONValue::MakeObject();
		JSONValue vec3Object = JSONValue::MakeObject();
		JSONValue intObject = JSONValue::MakeObject();
		auto pushNumbers = [](JSONValue& object, const Param& param) {
			JSONValue array = JSONValue::MakeArray();
			u32 count = UniformMemberFloatCount(param.kind);
			for (u32 i = 0; i < count; ++i) {
				array.Push(JSONValue::MakeFloat(param.numbers[i]));
			}
			object.AddMember(param.name, std::move(array));
		};
		for (const Param& param : params) {
			switch (param.kind) {
			case UniformMemberType::Float:
				floatObject.AddMember(param.name, JSONValue::MakeFloat(param.numbers[0]));
				break;
			case UniformMemberType::Vec4:
				pushNumbers(colorObject, param);
				break;
			case UniformMemberType::Vec2:
				pushNumbers(vec2Object, param);
				break;
			case UniformMemberType::Vec3:
				pushNumbers(vec3Object, param);
				break;
			case UniformMemberType::Int:
			case UniformMemberType::Bool:
				intObject.AddMember(param.name, JSONValue::MakeNumber(static_cast<f64>(param.integer)));
				break;
			case UniformMemberType::Unsupported:
				break;
			}
		}
		root.AddMember("floats", std::move(floatObject));
		root.AddMember("colors", std::move(colorObject));
		if (vec2Object.MemberCount() > 0) {
			root.AddMember("vec2s", std::move(vec2Object));
		}
		if (vec3Object.MemberCount() > 0) {
			root.AddMember("vec3s", std::move(vec3Object));
		}
		if (intObject.MemberCount() > 0) {
			root.AddMember("ints", std::move(intObject));
		}

		JSONValue textureObject = JSONValue::MakeObject();
		for (const TextureParam& param : textures) {
			pool.push_back(param.texture.ToString());
			textureObject.AddMember(param.name, JSONValue::MakeString(pool.back()));
		}
		root.AddMember("textures", std::move(textureObject));

		if (blend) {
			root.AddMember("blend", JSONValue::MakeString(RHI::BlendModeName(*blend)));
		}
		if (cull) {
			root.AddMember("cull", JSONValue::MakeString(RHI::CullModeName(*cull)));
		}
		if (alphaClipThreshold != 0.0f) {
			root.AddMember("alphaClip", JSONValue::MakeFloat(alphaClipThreshold));
		}
		if (sortingPriority != 0) {
			root.AddMember("sortingPriority", JSONValue::MakeNumber(static_cast<f64>(sortingPriority)));
		}

		if (!keywords.empty()) {
			JSONValue keywordArray = JSONValue::MakeArray();
			for (const str& keyword : keywords) {
				keywordArray.Push(JSONValue::MakeString(keyword));
			}
			root.AddMember("keywords", std::move(keywordArray));
		}

		return FileSystem::WriteTextFile(destPath, JSONSerialize(root, true));
	}

	bool MaterialAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		(void)payload;

		descriptor.WriteAssetRef(shaderGuid);
		descriptor.Write(static_cast<u32>(params.size()));
		descriptor.Write(static_cast<u32>(textures.size()));
		for (const Param& param : params) {
			descriptor.WriteString(param.name);
			descriptor.Write(static_cast<u8>(param.kind));
			u32 floatCount = UniformMemberFloatCount(param.kind);
			for (u32 i = 0; i < floatCount; ++i) {
				descriptor.Write(param.numbers[i]);
			}
			if (param.kind == UniformMemberType::Int || param.kind == UniformMemberType::Bool) {
				descriptor.Write(param.integer);
			}
		}
		for (const TextureParam& param : textures) {
			descriptor.WriteString(param.name);
			descriptor.WriteAssetRef(param.texture);
		}
		descriptor.Write(static_cast<u32>(keywords.size()));
		for (const str& keyword : keywords) {
			descriptor.WriteString(keyword);
		}
		descriptor.Write(static_cast<u8>(blend ? 1u : 0u));
		descriptor.Write(static_cast<u8>(blend ? static_cast<u8>(*blend) : 0u));
		descriptor.Write(static_cast<u8>(cull ? 1u : 0u));
		descriptor.Write(static_cast<u8>(cull ? static_cast<u8>(*cull) : 0u));
		descriptor.Write(alphaClipThreshold);
		descriptor.Write(sortingPriority);
		return true;
	}

	bool MaterialAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		(void)payload;

		Guid shader;
		u32 paramCount = 0;
		u32 textureCount = 0;
		descriptor.Read(shader);
		descriptor.Read(paramCount);
		descriptor.Read(textureCount);
		if (descriptor.Failed()) {
			return false;
		}

		Vector<Param> readParams{GetMemoryResource(MemTag::Asset)};
		Vector<TextureParam> readTextures{GetMemoryResource(MemTag::Asset)};
		for (u32 i = 0; i < paramCount && !descriptor.Failed(); ++i) {
			Param param;
			std::string_view name = descriptor.ReadString();
			param.name = str(name);
			param.hash = StringHash(name);
			u8 kind = 0;
			descriptor.Read(kind);
			param.kind = kind <= static_cast<u8>(UniformMemberType::Bool)
							 ? static_cast<UniformMemberType>(kind)
							 : UniformMemberType::Unsupported;
			u32 floatCount = UniformMemberFloatCount(param.kind);
			for (u32 c = 0; c < floatCount; ++c) {
				descriptor.Read(param.numbers[c]);
			}
			if (param.kind == UniformMemberType::Int || param.kind == UniformMemberType::Bool) {
				descriptor.Read(param.integer);
			}
			readParams.push_back(std::move(param));
		}
		for (u32 i = 0; i < textureCount && !descriptor.Failed(); ++i) {
			TextureParam param;
			std::string_view name = descriptor.ReadString();
			param.name = str(name);
			param.hash = StringHash(name);
			descriptor.Read(param.texture);
			readTextures.push_back(std::move(param));
		}
		Vector<str> readKeywords{GetMemoryResource(MemTag::Asset)};
		u32 keywordCount = 0;
		descriptor.Read(keywordCount);
		for (u32 i = 0; i < keywordCount && !descriptor.Failed(); ++i) {
			readKeywords.push_back(str(descriptor.ReadString()));
		}

		u8 hasBlend = 0;
		u8 blendValue = 0;
		u8 hasCull = 0;
		u8 cullValue = 0;
		f32 readThreshold = 0.0f;
		i32 readPriority = 0;
		descriptor.Read(hasBlend);
		descriptor.Read(blendValue);
		descriptor.Read(hasCull);
		descriptor.Read(cullValue);
		descriptor.Read(readThreshold);
		descriptor.Read(readPriority);
		if (descriptor.Failed()) {
			return false;
		}

		shaderGuid = shader;
		params = std::move(readParams);
		textures = std::move(readTextures);
		blend.reset();
		if (hasBlend && blendValue < static_cast<u8>(RHI::BlendMode::Count)) {
			blend = static_cast<RHI::BlendMode>(blendValue);
		}
		cull.reset();
		if (hasCull && cullValue < static_cast<u8>(RHI::CullMode::Count)) {
			cull = static_cast<RHI::CullMode>(cullValue);
		}
		sortingPriority = readPriority;
		alphaClipThreshold = readThreshold;
		SetKeywords({readKeywords.data(), readKeywords.size()});
		return true;
	}

	void MaterialAsset::OnLoad() {
		if (shaderGuid.IsValid()) {
			Asset* loaded = AssetDatabase::LoadAssetByGuid(shaderGuid);
			const bool isShader = loaded && loaded->GetAssetTypeId() == StringHash("ShaderAsset");
			if (isShader && !static_cast<ShaderAsset*>(loaded)->IsKernel()) {
				shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(loaded));
			} else if (isShader) {
				SLT_LOG_WARN("Graphics", "MaterialAsset '{}': shader guid resolves to a compute kernel (not a shader)", virtualPath);
				AssetDatabase::UnloadAsset(loaded);
			} else if (loaded) {
				SLT_LOG_WARN("Graphics", "MaterialAsset '{}': shader guid resolves to a non-shader asset", virtualPath);
				AssetDatabase::UnloadAsset(loaded);
			} else {
				SLT_LOG_WARN("Graphics", "MaterialAsset '{}': shader {} not found", virtualPath, shaderGuid);
			}
		}
		if (ShaderAsset* resolved = shader.Get()) {
			Vector<str> effective{GetMemoryResource(MemTag::Asset)};
			GetEffectiveKeywords(effective);
			if (!effective.empty()) {
				resolved->GetOrCompileVariant({effective.data(), effective.size()});
			}
		}
		for (TextureParam& param : textures) {
			param.resolved = ResolveTexture(param.texture, virtualPath);
		}
	}

	void MaterialAsset::OnUnload() {
		if (Asset* loadedShader = shader.Get()) {
			AssetDatabase::UnloadAsset(loadedShader);
		}
		shader = {};
		for (TextureParam& param : textures) {
			if (Asset* loadedTexture = param.resolved.Get()) {
				AssetDatabase::UnloadAsset(loadedTexture);
			}
			param.resolved = {};
		}
	}

	bool MaterialAsset::HasFloat(StringHash name) const {
		const Param* param = FindParam(name);
		return param && param->kind == UniformMemberType::Float;
	}

	bool MaterialAsset::HasColor(StringHash name) const {
		const Param* param = FindParam(name);
		return param && param->kind == UniformMemberType::Vec4;
	}

	bool MaterialAsset::HasTexture(StringHash name) const {
		for (const TextureParam& param : textures) {
			if (param.hash == name) {
				return true;
			}
		}
		return false;
	}

	u32 MaterialAsset::SyncToLayout(const UniformBlockLayout* layout, std::span<const str> textureSlots,
									std::span<const ShaderMemberDefault> defaults) {
		u32 added = 0;
		auto declaredDefault = [&](std::string_view name) -> const ShaderMemberDefault* {
			for (const ShaderMemberDefault& declared : defaults) {
				if (declared.name == name) {
					return &declared;
				}
			}
			return nullptr;
		};
		if (layout) {
			for (const UniformMember& member : layout->members) {
				if (member.name.empty() || member.type == UniformMemberType::Unsupported) {
					continue;
				}
				StringHash key(std::string_view{member.name});
				const Param* existing = FindParam(key);
				if (existing && existing->kind == member.type) {
					continue;
				}
				const ShaderMemberDefault* declared = declaredDefault(member.name);
				SetParam(member.name, declared ? MakeParam(member.type, *declared) : MakeParam(member.type));
				++added;
			}
		}

		for (const str& slot : textureSlots) {
			if (slot.empty()) {
				continue;
			}
			if (!HasTexture(StringHash(std::string_view{slot}))) {
				SetTexture(slot, Guid{});
				++added;
			}
		}
		return added;
	}

	bool MaterialAsset::IsOrphanParam(std::string_view name, const UniformBlockLayout* layout, std::span<const str> textureSlots) {
		if (layout) {
			for (const UniformMember& member : layout->members) {
				if (member.name == name) {
					return false;
				}
			}
		}
		for (const str& slot : textureSlots) {
			if (slot == name) {
				return false;
			}
		}
		return true;
	}

	Asset* MaterialAssetImporter::Import(const Path& sourcePath) {
		MaterialAsset* asset = New<MaterialAsset>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	Asset* MaterialAssetImporter::CreateAsset() {
		MaterialAsset* asset = New<MaterialAsset>(MemTag::Asset);

		Asset* shaderAsset = AssetDatabase::LoadAsset(Path(STANDARD_SHADER_PATH));
		if (shaderAsset) {
			asset->shaderGuid = shaderAsset->assetId;

			ShaderAsset* shader = static_cast<ShaderAsset*>(shaderAsset);
			Vector<str> textureSlots{GetMemoryResource(MemTag::Asset)};
			shader->GetMaterialTextureSlots(textureSlots);
			asset->SyncToLayout(shader->GetMaterialParamLayout(), {textureSlots.data(), textureSlots.size()},
								{shader->memberDefaults.data(), shader->memberDefaults.size()});

			AssetDatabase::UnloadAsset(shaderAsset);
		} else {
			SLT_LOG_WARN("Graphics", "MaterialAssetImporter: '{}' unavailable (new material has a null shader)", STANDARD_SHADER_PATH);
		}
		return asset;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::MaterialAsset)
