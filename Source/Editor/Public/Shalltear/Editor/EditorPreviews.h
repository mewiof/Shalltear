#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <functional>
#include <span>
#include <string_view>

namespace Shalltear {

	class Asset;
	class FullScreenPass;
	class MaterialAsset;
	class Scene;

	struct AssetPreviewOrbit {
		f32 yawDeg = 0.0f;
		f32 pitchDeg = 0.0f;
		f32 zoom = 1.0f;
	};

	struct AssetPreviewImage {
		RHI::TextureHandle texture;
		Vec2I size{};
		UI::SamplerKind sampler = UI::SamplerKind::LinearClamp;
		str detail;

		// valid only until Produce returns
		std::span<const u8> pixels;
		RHI::Format pixelsFormat = RHI::Format::Unknown;
		Vector<u8> decodedPixels{GetMemoryResource(MemTag::Editor)};

		Vector<Guid> dependencies{GetMemoryResource(MemTag::Editor)};

		AABB subjectBounds = AABB::Empty();
	};

	struct AssetPreviewRequest {
		Guid guid;
		Asset* asset = nullptr;
		Handle<Scene> stage;
		ViewTargetId target = INVALID_VIEW_TARGET;
		Vec2I targetSize{};
		FullScreenPass* row = nullptr;
	};

	using AssetPreviewProducerFn = std::function<bool(const AssetPreviewRequest& request, AssetPreviewImage& out)>;

	using AssetPreviewPaneRefreshFn = std::function<void(UI::Context& context)>;

	using AssetPreviewPaneBuildFn = std::function<AssetPreviewPaneRefreshFn(UI::Context& context, UI::ElementHandle parent, const Guid& guid)>;

	struct AssetPreviewPaneDesc {
		std::function<void(const Guid& guid)> onShow;
		std::function<void(const Guid& guid)> onHide;
		AssetPreviewPaneBuildFn buildHeader;
		AssetPreviewPaneBuildFn buildBody;
	};

	namespace EditorPreviews {

		SHALLTEAR_EDITOR_API void Initialize();
		SHALLTEAR_EDITOR_API void Terminate();

		SHALLTEAR_EDITOR_API void RegisterBuiltIns();

		SHALLTEAR_EDITOR_API void RegisterProducer(StringHash assetTypeName, AssetPreviewProducerFn producer,
												   const void* owner = nullptr);

		SHALLTEAR_EDITOR_API void RegisterPane(StringHash assetTypeName, AssetPreviewPaneDesc desc,
											   const void* owner = nullptr);

		SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_EDITOR_API bool Produce(const AssetPreviewRequest& request, AssetPreviewImage& out);

		struct TypeBadge {
			EditorIcons::Icon icon;
			Color color;
		};
		SHALLTEAR_EDITOR_API TypeBadge BadgeForAssetType(StringHash assetTypeName);
		SHALLTEAR_EDITOR_API TypeBadge BadgeForFileName(std::string_view fileName);

		namespace AssetBadges {

			SHALLTEAR_EDITOR_API void RegisterBuiltIns();

			SHALLTEAR_EDITOR_API void Register(StringHash assetTypeName, EditorIcons::Icon glyph,
											   EditorTheme::ColorToken tint, const void* owner = nullptr);

			SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

		} // namespace AssetBadges

	} // namespace EditorPreviews

	namespace EditorPreviewStage {

		SHALLTEAR_EDITOR_API void AddLights(Handle<Scene> stage);

		SHALLTEAR_EDITOR_API void SetSky(Handle<Scene> stage, Handle<MaterialAsset> sky);

		SHALLTEAR_EDITOR_API bool Submit(Handle<Scene> stage, ViewTargetId target, Vec2I targetSize, const AABB& bounds,
										 const AssetPreviewOrbit& orbit, const f64* time = nullptr,
										 std::span<const Handle<RenderPass>> passes = {});

	} // namespace EditorPreviewStage

	struct AssetPreviewEntry {
		RHI::TextureHandle texture;
		Vec2I size{};
		UI::SamplerKind sampler = UI::SamplerKind::LinearClamp;
		str detail;
		u64 revision = 0;
	};

	namespace EditorPreviewStore {

		SHALLTEAR_EDITOR_API void Initialize();
		SHALLTEAR_EDITOR_API void Terminate();

		SHALLTEAR_EDITOR_API bool Request(const Guid& guid, AssetPreviewEntry& out);

		SHALLTEAR_EDITOR_API bool Peek(const Guid& guid, AssetPreviewEntry& out);

		SHALLTEAR_EDITOR_API void Drain();

		SHALLTEAR_EDITOR_API void SetCapacity(u32 previews);
		SHALLTEAR_EDITOR_API u32 GetCapacity();

		SHALLTEAR_EDITOR_API void SetFrameBudgetMs(f32 milliseconds);
		SHALLTEAR_EDITOR_API f32 GetFrameBudgetMs();

		SHALLTEAR_EDITOR_API u32 GetCachedCount();
		SHALLTEAR_EDITOR_API u64 GetCachedBytes();
		SHALLTEAR_EDITOR_API u32 GetPendingCount();

		SHALLTEAR_EDITOR_API void Invalidate(const Guid& guid);
		SHALLTEAR_EDITOR_API void InvalidateAll();

	} // namespace EditorPreviewStore

	namespace EditorPreviewOps {

		SHALLTEAR_EDITOR_API Vec2 FitImage(Vec2I imageSize, Vec2 available);

		SHALLTEAR_EDITOR_API str DescribeTexture(Vec2I size, RHI::Format format);

		SHALLTEAR_EDITOR_API bool FitCameraToBounds(const AABB& bounds, f32 aspect, f32 verticalFOVDegrees,
													f32 marginScale, const AssetPreviewOrbit& orbit,
													ViewDesc& outDesc);

		SHALLTEAR_EDITOR_API AssetPreviewOrbit OrbitAfterDrag(const AssetPreviewOrbit& from, Vec2 dragPx);
		SHALLTEAR_EDITOR_API AssetPreviewOrbit OrbitAfterZoom(const AssetPreviewOrbit& from, f32 wheelTicks);

		SHALLTEAR_EDITOR_API RHI::Format ResampleToRGBA8(std::span<const u8> src, Vec2I srcSize,
														 RHI::Format srcFormat, Vec2I dstSize, Vector<u8>& out);

	} // namespace EditorPreviewOps

} // namespace Shalltear
