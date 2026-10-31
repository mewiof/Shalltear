#pragma once

#include "NodeShaderAPI.h"

#include "GraphCompiler.h"
#include "GraphDefinitions.h"
#include "GraphPreview.h"
#include "GraphPreviewAtlas.h"
#include "GraphProperties.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/ShaderCompileQueue.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorPreviews.h"

#include <string_view>

namespace Shalltear {

	class FullScreenPass;
	class MaterialAsset;
	class MeshAsset;
	class MeshRenderer;
	class Scene;
	class SceneObject;
	class ShaderAsset;
	struct GraphDocument;

	enum class NodeShaderPreviewMesh : u8 {
		Sphere,
		Capsule,
		Cylinder,
		Cube,
		Quad,
		Custom
	};

	class SHALLTEAR_NODESHADER_API NodeShaderPreviews {
	public:
		static constexpr std::string_view MESH_SETTING = "previewMesh";

		NodeShaderPreviews();
		~NodeShaderPreviews();
		NodeShaderPreviews(const NodeShaderPreviews&) = delete;
		NodeShaderPreviews& operator=(const NodeShaderPreviews&) = delete;

		void Bind(const Guid& asset, std::string_view graphName, u32 serial);

		void Invalidate() { _stale = true; }

		void Tick(const GraphDocument& document);

		void SubmitViews(f64 time, bool animate, Color atlasClear);

		bool IsCompiling() const { return _nodes.ticket != 0 || _surface.ticket != 0; }
		std::span<const GraphDiagnostic> GetDiagnostics() const { return _shown; }
		bool SurfaceFailed() const { return _surfaceFailed; }
		const str& GetSurfaceText() const { return _surfaceText; }

		GraphPreviewAtlas& GetAtlas() { return _atlas; }
		i32 TileOf(u32 node) const;

		void SetMesh(NodeShaderPreviewMesh mesh, const Guid& custom);
		NodeShaderPreviewMesh GetMesh() const { return _mesh; }
		void SetMainSize(Vec2I size);
		void SetOrbit(const AssetPreviewOrbit& orbit);
		const AssetPreviewOrbit& GetOrbit() const { return _orbit; }
		RHI::TextureHandle GetMainTexture() const;
		ViewTargetId GetMainTarget() const { return _mainTarget; }
		bool HasMainPreview() const { return _mainMaterial != nullptr; }

		const Path& GetNodesFile() const { return _nodesFile; }
		const Path& GetSurfaceFile() const { return _surfaceFile; }
	private:
		struct Job {
			ShaderCompileTicket ticket = 0;
			str submitted;
			str adopted;
			Vector<u32> lines{GetMemoryResource(MemTag::Editor)};
			Vector<GraphDiagnostic> compiled{GetMemoryResource(MemTag::Editor)};
		};

		void Collect();
		void Regenerate(const GraphDocument& document);
		bool Submit(Job& job, const Path& file, const str& text, const Vector<u32>& lines);
		void Map(Job& job, const Path& file, const str& messages, bool succeeded);
		void Show();
		void AdoptSurface(ShaderAsset* shader);
		void EnsureStage();
		void ApplyMesh();
		void ReleaseMain();

		Guid _asset{};
		str _graphName;
		Path _nodesFile;
		Path _surfaceFile;
		bool _stale = true;

		Job _nodes;
		Job _surface;
		GraphDefinitions _definitions;
		GraphPreview::Program _compilingProgram;
		GraphPreview::Program _program;
		Vector<GraphPropertyInfo> _properties{GetMemoryResource(MemTag::Editor)};
		Vector<GraphDiagnostic> _diagnostics{GetMemoryResource(MemTag::Editor)};
		Vector<GraphDiagnostic> _shown{GetMemoryResource(MemTag::Editor)};
		str _surfaceText;
		bool _surfaceFailed = false;

		GraphPreviewAtlas _atlas;
		bool _atlasDirty = false;
		bool _atlasTimed = false;

		Handle<Scene> _stage;
		Handle<SceneObject> _subject;
		ViewTargetId _mainTarget = INVALID_VIEW_TARGET;
		Vec2I _mainSize{};
		AssetPreviewOrbit _orbit{};
		NodeShaderPreviewMesh _mesh = NodeShaderPreviewMesh::Sphere;
		Guid _customMesh{};
		Asset* _meshAsset = nullptr;
		AABB _bounds = AABB::Empty();
		ShaderAsset* _mainShader = nullptr;
		MaterialAsset* _mainMaterial = nullptr;
		FullScreenPass* _mainRow = nullptr;
		Asset* _neutral = nullptr;
		bool _mainDirty = false;
		bool _mainTimed = false;
		f64 _time = 0.0;
	};

} // namespace Shalltear
