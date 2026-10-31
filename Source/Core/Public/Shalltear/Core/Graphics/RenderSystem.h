#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	class Camera;
	class Environment;
	class GraphicsProfile;
	class Light;
	class MeshRenderer;
	class ReflectionProbe;
	class RenderPass;
	class Scene;
	class SceneObject;
	class ShaderAsset;
	class SkinnedMeshRenderer;
	class Window;
	namespace RHI {
		class CommandList;
		class Device;
		struct TextureHandle;
	} //namespace RHI

	class SHALLTEAR_CORE_API IWindowOverlay {
	public:
		virtual ~IWindowOverlay() = default;

		virtual void CollectOverlayWindows(Vector<Handle<Window>>& out) = 0;

		// no pass open, backbuffer must stay in RenderTarget
		virtual u32 RecordOverlay(Handle<Window> window, RHI::CommandList& cmd,
								  RHI::TextureHandle backbuffer, Vec2I sizePx) = 0;

		virtual bool HasOffscreenWork() { return false; }

		virtual u32 RecordOffscreen(RHI::CommandList& cmd) {
			(void)cmd;
			return 0;
		}
	};

	using ViewTargetId = u32;
	inline constexpr ViewTargetId INVALID_VIEW_TARGET = 0;

	// SRGB color
	struct DebugLine {
		Vec3 a;
		Vec3 b;
		Color32 color = Color32::White();
	};

	enum class ViewFill : u8 { Solid = 0,
							   Wireframe,
							   SolidAndWireframe };

	enum class DebugView : u8 {
		None = 0,
		Normals,
		Depth,
		Cascades
	};

	enum class DebugOverlay : u8 {
		None = 0,
		DirectionalAtlas,
		PunctualAtlas
	};

	struct ViewOutline {
		Handle<SceneObject> object;
		Color32 color = Color32::White();
	};

	inline constexpr u32 MAX_OUTLINE_COLORS = 4;

	struct ViewPick {
		Vec2I pixel{}; // view target pxs, top left origin
		u64 tag = 0;
	};

	struct PickAnswer {
		u64 tag = 0;
		Handle<SceneObject> object;
	};

	struct ViewDesc {
		Mat4 view = Mat4::Identity();
		Mat4 projection = Mat4::Identity();
		Vec3 cameraPosition{};
		Color clearColor = Color::Black();
		ViewFill fill = ViewFill::Solid;
		DebugView debugView = DebugView::None;
		// SRGB
		Color32 wireColor = Color32::White();
		std::span<const Handle<RenderPass>> passes;
		std::span<const DebugLine> lines;
		std::span<const DebugLine> overlayLines;
		std::span<const Handle<Scene>> scenes;
		std::span<const StringHash> skippedProceduralTypes;
		bool retainCascadeFit = false;
		const Mat4* shadowFitView = nullptr;
		const Mat4* shadowFitProjection = nullptr;
		DebugOverlay debugOverlay = DebugOverlay::None;
		const f64* time = nullptr;
		bool tonemap = true;
		Handle<Camera> camera;
		std::span<const ViewOutline> outlines;
		// view pxs
		f32 outlineWidth = 2.0f;
		f32 outlineHiddenOpacity = 0.3f;
		const ViewPick* pick = nullptr;
		bool radianceCapture = false;
	};

	inline constexpr u32 MAX_SHADOW_CASCADES = 4;

	struct CascadeFit {
		Mat4 viewProjection;
		f32 worldTexelSize = 0.0f;
		f32 farDepth = 0.0f;
	};

	struct GraphicsStats {
		u64 vramUsedBytes = 0;
		u64 vramBudgetBytes = 0;
		u32 liveBuffers = 0;
		u32 liveTextures = 0;
		u32 liveSamplers = 0;
		u32 livePipelines = 0;
		u32 drawCalls = 0;
		u32 triangles = 0;
		u32 lightsBinned = 0;
		u32 lightsDropped = 0;
		u32 clusterLightsDropped = 0;
		u32 shadowMapsRendered = 0;
		u32 shadowLights = 0;
		u32 shadowLightsDropped = 0;
		u32 proceduralDrawn = 0;
		u32 proceduralCulled = 0;
	};

	class SHALLTEAR_CORE_API RenderSystem {
	public:
		static void Initialize();
		static void Terminate();
		static bool IsInitialized();

		static bool IsRenderingAvailable();

		static void RenderAll();

		static RHI::Device* GetDevice();

		static void SetWindowOverlay(IWindowOverlay* overlay);

		static GraphicsStats GetStats();

		static u32 GetSampleCount();

		static const GraphicsProfile* GetActiveProfile();

		static i32 GetReflectionProbeResolution();

		static ViewTargetId CreateViewTarget(Vec2I sizePx);
		static void ResizeViewTarget(ViewTargetId target, Vec2I sizePx);
		static void DestroyViewTarget(ViewTargetId target);
		static RHI::TextureHandle GetViewTargetTexture(ViewTargetId target);
		static Vec2I GetViewTargetSize(ViewTargetId target);

		static void SubmitView(ViewTargetId target, const ViewDesc& desc);

		static u32 GetCascadeFit(ViewTargetId target, std::span<CascadeFit> out);

		static bool TakePickAnswer(ViewTargetId target, PickAnswer& out);

		static void SetSceneCamerasRenderToWindows(bool enabled);
		static bool GetSceneCamerasRenderToWindows();

		static bool ReadbackViewTarget(ViewTargetId target, Vector<u8>& outRGBA8, Vec2I& outSize);

		static bool ReadbackViewTargetRadiance(ViewTargetId target, Vector<u8>& outRGBA16F, Vec2I& outSize);

		static void RegisterCamera(Handle<Camera> camera);
		static void UnregisterCamera(Handle<Camera> camera);

		static std::span<const Handle<Camera>> GetCameras();

		static void RegisterLight(Handle<Light> light);
		static void UnregisterLight(Handle<Light> light);
		static void RegisterEnvironment(Handle<Environment> environment);
		static void UnregisterEnvironment(Handle<Environment> environment);
		static void RegisterReflectionProbe(Handle<ReflectionProbe> probe);
		static void UnregisterReflectionProbe(Handle<ReflectionProbe> probe);
		static void RegisterMeshRenderer(Handle<MeshRenderer> renderer);
		static void UnregisterMeshRenderer(Handle<MeshRenderer> renderer);
		static void RegisterSkinnedMeshRenderer(Handle<SkinnedMeshRenderer> renderer);
		static void UnregisterSkinnedMeshRenderer(Handle<SkinnedMeshRenderer> renderer);

		static bool ReadbackWindowSceneColor(Handle<Window> window, Vector<u8>& outRGBA16F, Vec2I& outSize);

		static void NotifyShaderFileChanged(const str& physicalPath);
	};

} // namespace Shalltear
