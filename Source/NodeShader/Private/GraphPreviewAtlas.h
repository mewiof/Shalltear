#pragma once

#include "NodeShaderAPI.h"

#include "GraphPreview.h"
#include "GraphProperties.h"

#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"

#include <span>

namespace Shalltear {

	class MaterialAsset;
	class Scene;
	class ShaderAsset;

	namespace GraphPreviewMaterials {

		// caller owns it, frees it before its shader
		SHALLTEAR_NODESHADER_API MaterialAsset* Make(ShaderAsset& shader, std::span<const GraphPropertyInfo> properties);

		SHALLTEAR_NODESHADER_API void Apply(MaterialAsset& material, std::span<const GraphPropertyInfo> properties);

	} // namespace GraphPreviewMaterials

	class GraphPreviewRow : public RenderPass {
	public:
		Handle<MaterialAsset> material;
		u32 tiles = 0;
		GraphPreview::Push push{};
		bool drew = false;

		void Record(PassContext& context) override;
	};

	class SHALLTEAR_NODESHADER_API GraphPreviewAtlas {
	public:
		GraphPreviewAtlas();
		~GraphPreviewAtlas();
		GraphPreviewAtlas(const GraphPreviewAtlas&) = delete;
		GraphPreviewAtlas& operator=(const GraphPreviewAtlas&) = delete;

		// takes ownership of shader even when it returns false
		bool Adopt(ShaderAsset* shader, const GraphPreview::Program& program, std::span<const GraphPropertyInfo> properties);

		void SetProperties(std::span<const GraphPropertyInfo> properties);

		void Clear() { Release(); }

		void Submit(f64 time, Color clear);

		bool HasProgram() const { return _material != nullptr; }
		u32 GetTileCount() const { return _row.tiles; }
		RHI::TextureHandle GetTexture() const;
		ViewTargetId GetTarget() const { return _target; }
		UI::Rect TileUV(u32 tile) const;
	private:
		void Release();

		ViewTargetId _target = INVALID_VIEW_TARGET;
		Vec2I _size{};
		u32 _rows = 0;
		Handle<Scene> _stage;
		ShaderAsset* _shader = nullptr;
		MaterialAsset* _material = nullptr;
		GraphPreviewRow _row;
		f64 _time = 0.0;
	};

} // namespace Shalltear
