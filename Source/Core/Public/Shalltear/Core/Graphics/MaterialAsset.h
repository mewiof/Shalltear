#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Graphics/ShaderReflection.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"

#include <optional>
#include <span>

namespace Shalltear {

	class ShaderAsset;
	class TextureAsset;

	class SHALLTEAR_CORE_API MaterialAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(MaterialAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		MaterialAsset() = default;
		~MaterialAsset() override;

		struct Param {
			str name;
			StringHash hash;
			UniformMemberType kind = UniformMemberType::Unsupported;
			f32 numbers[4] = {};
			i32 integer = 0;
		};
		struct TextureParam {
			str name;
			StringHash hash;
			Guid texture;
			Handle<TextureAsset> resolved;
		};

		Guid shaderGuid;
		Handle<ShaderAsset> shader;

		Vector<Param> params{GetMemoryResource(MemTag::Asset)};
		Vector<TextureParam> textures{GetMemoryResource(MemTag::Asset)};

		// sorted, only SetKeywords writes it
		Vector<str> keywords{GetMemoryResource(MemTag::Asset)};
		u64 keywordHash = 0;

		std::optional<RHI::BlendMode> blend;
		std::optional<RHI::CullMode> cull;

		f32 alphaClipThreshold = 0.0f;
		void SetAlphaClipThreshold(f32 threshold);

		// lower renders first, never crosses blend classes
		i32 sortingPriority = 0;

		RHI::ShaderRenderState ResolveRenderState(const RHI::ShaderRenderState& shaderState) const;

		void GetEffectiveKeywords(Vector<str>& out) const;

		void SetKeywords(std::span<const str> enabled);

		const Param* FindParam(StringHash name) const;
		void SetParam(std::string_view name, const Param& value);
		static Param MakeParam(UniformMemberType kind);
		static Param MakeParam(UniformMemberType kind, const ShaderMemberDefault& declared);

		f32 GetFloat(StringHash name, f32 fallback = 0.0f) const;
		Color GetColor(StringHash name, Color fallback = Color::White()) const;
		Handle<TextureAsset> GetTexture(StringHash name) const;

		bool HasFloat(StringHash name) const;
		bool HasColor(StringHash name) const;
		bool HasTexture(StringHash name) const;

		void SetFloat(std::string_view name, f32 value);
		void SetColor(std::string_view name, Color value);
		void SetVec2(std::string_view name, Vec2 value);
		void SetVec3(std::string_view name, Vec3 value);
		void SetInt(std::string_view name, i32 value);
		void SetTexture(std::string_view name, const Guid& texture);

		// out must be >= layout.sizeBytes
		void PackBlock(const UniformBlockLayout& layout, std::span<u8> out,
					   std::span<const ShaderMemberDefault> defaults = {}) const;

		u32 SyncToLayout(const UniformBlockLayout* layout, std::span<const str> textureSlots,
						 std::span<const ShaderMemberDefault> defaults = {});

		void ApplyKeywordBindings(std::span<const ShaderKeywordBinding> bindings);

		static bool IsOrphanParam(std::string_view name, const UniformBlockLayout* layout, std::span<const str> textureSlots);

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;
		StringHash GetAssetTypeId() const override { return MaterialAsset::GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;
	protected:
		void OnLoad() override;
		void OnUnload() override;
	};

	class SHALLTEAR_CORE_API MaterialAssetImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("MaterialAsset"); }
		Asset* Import(const Path& sourcePath) override;

		// bump when the cooked record changes shape
		u32 GetImporterVersion() const override { return 2; }

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return "Material"; }
		std::string_view GetCreateDefaultFileName() const override { return "New Material.mat"; }
		Asset* CreateAsset() override;
	};

} // namespace Shalltear
