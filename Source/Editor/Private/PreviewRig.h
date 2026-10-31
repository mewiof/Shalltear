#pragma once

#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class FullScreenPass;
	class MaterialAsset;
	class Scene;
	struct AssetPreviewRequest;
	struct AssetPreviewImage;
	struct AssetPreviewOrbit;

	namespace EditorPreviewsInternal {

		void AddPreviewLights(Handle<Scene> stage, Handle<MaterialAsset> sky = {});

		bool SubmitPreviewView(Handle<Scene> stage, ViewTargetId target, Vec2I targetSize, const AABB& bounds,
							   const AssetPreviewOrbit& orbit, const f64* time = nullptr, std::span<const Handle<RenderPass>> passes = {});

		class LentRows {
		public:
			explicit LentRows(FullScreenPass* row);
			std::span<const Handle<RenderPass>> Get() const { return {_rows, _count}; }
		private:
			Handle<RenderPass> _rows[1];
			size_t _count = 0;
		};

		bool RenderPreview(const AssetPreviewRequest& request, const AABB& bounds, AssetPreviewImage& out);

		Handle<MaterialAsset> SharedDefaultMaterial();
		void ReleaseSharedDefaultMaterial();

	} // namespace EditorPreviewsInternal

} // namespace Shalltear
