#include "NodeShaderPreviews.h"

#include "GraphDefinitions.h"
#include "GraphHLSL.h"
#include "GraphSurface.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/FullScreenPass.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <format>

namespace Shalltear {

	namespace {

		constexpr std::string_view LIBRARY = GRAPH_GENERATED_DIRECTORY;

		PrimitiveShape ShapeOf(NodeShaderPreviewMesh mesh) {
			switch (mesh) {
			case NodeShaderPreviewMesh::Capsule:
				return PrimitiveShape::Capsule;
			case NodeShaderPreviewMesh::Cylinder:
				return PrimitiveShape::Cylinder;
			case NodeShaderPreviewMesh::Cube:
				return PrimitiveShape::Cube;
			case NodeShaderPreviewMesh::Quad:
				return PrimitiveShape::Quad;
			case NodeShaderPreviewMesh::Sphere:
			case NodeShaderPreviewMesh::Custom:
				break;
			}
			return PrimitiveShape::Sphere;
		}

	} // anonymous namespace

	NodeShaderPreviews::NodeShaderPreviews() = default;

	NodeShaderPreviews::~NodeShaderPreviews() {
		if (_nodes.ticket != 0) {
			ShaderCompileQueue::Cancel(_nodes.ticket);
		}
		if (_surface.ticket != 0) {
			ShaderCompileQueue::Cancel(_surface.ticket);
		}
		ReleaseMain();
		if (_mainRow) {
			Delete(_mainRow);
		}
		if (_neutral) {
			AssetDatabase::UnloadAsset(_neutral);
		}
		if (_mainTarget != INVALID_VIEW_TARGET) {
			RenderSystem::DestroyViewTarget(_mainTarget);
		}
		if (_stage.IsValid()) {
			SceneSystem::UnloadScene(_stage);
		}
		if (_meshAsset) {
			AssetDatabase::UnloadAsset(_meshAsset);
		}
	}

	void NodeShaderPreviews::Bind(const Guid& asset, std::string_view graphName, u32 serial) {
		_asset = asset;
		_graphName = str(graphName);
		const Path library = FileSystem::Resolve(Path(str(LIBRARY)));
		_nodesFile = Path(std::format("{}/{}.nodes{}.hlsl", library.ToString(), asset.ToString(), serial));
		_surfaceFile = Path(std::format("{}/{}.preview{}.hlsl", library.ToString(), asset.ToString(), serial));
		_stale = true;
	}

	void NodeShaderPreviews::Tick(const GraphDocument& document) {
		Collect();
		if (_stale && !IsCompiling()) {
			_stale = false;
			Regenerate(document);
		}
	}

	void NodeShaderPreviews::Collect() {
		if (_nodes.ticket != 0) {
			ShaderAsset* shader = nullptr;
			str messages;
			const ShaderCompileStatus status = ShaderCompileQueue::Collect(_nodes.ticket, shader, &messages);
			if (status != ShaderCompileStatus::Pending) {
				_nodes.ticket = 0;
				Map(_nodes, _nodesFile, messages, status == ShaderCompileStatus::Succeeded);
				if (status == ShaderCompileStatus::Succeeded && _atlas.Adopt(shader, _compilingProgram, _properties)) {
					_program = std::move(_compilingProgram);
					_nodes.adopted = _nodes.submitted;
					_atlasDirty = true;
					_atlasTimed = std::any_of(_program.tiles.begin(), _program.tiles.end(), [](const GraphPreview::Tile& tile) { return tile.timed; });
				}
				_compilingProgram = {};
			}
		}
		if (_surface.ticket != 0) {
			ShaderAsset* shader = nullptr;
			str messages;
			const ShaderCompileStatus status = ShaderCompileQueue::Collect(_surface.ticket, shader, &messages);
			if (status != ShaderCompileStatus::Pending) {
				_surface.ticket = 0;
				Map(_surface, _surfaceFile, messages, status == ShaderCompileStatus::Succeeded);
				_surfaceFailed = status != ShaderCompileStatus::Succeeded;
				if (status == ShaderCompileStatus::Succeeded) {
					AdoptSurface(shader);
					_surface.adopted = _surface.submitted;
				}
			}
		}
	}

	void NodeShaderPreviews::Map(Job& job, const Path& file, const str& messages, bool succeeded) {
		job.compiled.clear();
		if (!succeeded) {
			GraphHLSL::MapCompilerMessages(messages, FileSystem::Resolve(file).ToString(), job.submitted, job.lines, job.compiled);
		}
		Show();
	}

	void NodeShaderPreviews::Show() {
		_shown.assign(_diagnostics.begin(), _diagnostics.end());
		for (const Job* job : {&_surface, &_nodes}) {
			for (const GraphDiagnostic& diagnostic : job->compiled) {
				const bool said = std::any_of(_shown.begin(), _shown.end(), [&](const GraphDiagnostic& held) {
					return held.node == diagnostic.node && held.message == diagnostic.message;
				});
				if (!said) {
					_shown.push_back(diagnostic);
				}
			}
		}
	}

	bool NodeShaderPreviews::Submit(Job& job, const Path& file, const str& text, const Vector<u32>& lines) {
		if (text.empty() || text == job.adopted) {
			return false;
		}
		FileSystem::CreateDirectory(FileSystem::Resolve(Path(str(LIBRARY))));
		if (!FileSystem::WriteTextFile(file, text)) {
			SLT_LOG_ERROR("NodeShader", "couldn't write a preview's shader to '{}'", file.ToString());
			return false;
		}
		job.submitted = text;
		job.lines = lines;
		job.compiled.clear();
		Vector<str> keywords{GetMemoryResource(MemTag::Editor)};
		if (&job == &_surface) {
			for (const GraphPropertyInfo& property : _properties) {
				if (str keyword = GraphProperties::DefaultKeyword(property); !keyword.empty()) {
					keywords.push_back(std::move(keyword));
				}
			}
		}
		job.ticket = ShaderCompileQueue::Submit(file, {keywords.data(), keywords.size()});
		return job.ticket != 0;
	}

	void NodeShaderPreviews::Regenerate(const GraphDocument& document) {
		_diagnostics.clear();
		_properties.clear();
		_definitions.Clear();
		Vector<GraphDiagnostic> ignored{GetMemoryResource(MemTag::Editor)};
		GraphProperties::ParseBlackboard(document, _properties, ignored);
		GraphSubgraphs::CollectKeywords(document, _definitions, _properties, ignored);

		str surface;
		Vector<u32> surfaceLines{GetMemoryResource(MemTag::Editor)};
		Vector<GraphDiagnostic> surfaceDiagnostics{GetMemoryResource(MemTag::Editor)};
		const bool generated =
			document.type == GRAPH_SUBGRAPH_DOCUMENT_TYPE
				? GraphSurface::GenerateSubgraphPreview(document, _graphName, surface, surfaceDiagnostics, &_definitions, _asset, &surfaceLines)
				: GraphSurface::Generate(document, _graphName, surface, surfaceDiagnostics, &_definitions, &surfaceLines);
		if (generated) {
			_surfaceText = surface;
			Submit(_surface, _surfaceFile, surface, surfaceLines);
		}
		for (GraphDiagnostic& diagnostic : surfaceDiagnostics) {
			_diagnostics.push_back(std::move(diagnostic));
		}

		GraphPreview::Program program;
		GraphPreview::Generate(document, _graphName, program, &_definitions, _asset);
		for (const GraphDiagnostic& diagnostic : program.diagnostics) {
			const bool said = std::any_of(_diagnostics.begin(), _diagnostics.end(), [&](const GraphDiagnostic& held) {
				return held.node == diagnostic.node && held.message == diagnostic.message;
			});
			if (!said) {
				_diagnostics.push_back(diagnostic);
			}
		}
		if (program.text.empty()) {
			if (_atlas.HasProgram()) {
				_program = {};
				_nodes.adopted.clear();
				_atlas.Clear();
			}
		} else if (Submit(_nodes, _nodesFile, program.text, program.lineNodes)) {
			_compilingProgram = std::move(program);
		} else if (program.text == _nodes.adopted) {
			_atlas.SetProperties(_properties);
			_atlasDirty = true;
		}
		if (_mainMaterial) {
			GraphPreviewMaterials::Apply(*_mainMaterial, _properties);
			_mainDirty = true;
		}
		Show();
	}

	i32 NodeShaderPreviews::TileOf(u32 node) const {
		for (size_t i = 0; i < _program.tiles.size(); ++i) {
			if (_program.tiles[i].node == node) {
				return static_cast<i32>(i);
			}
		}
		return -1;
	}

	void NodeShaderPreviews::SubmitViews(f64 time, bool animate, Color atlasClear) {
		const bool clockMoved = animate && time != _time;
		if (clockMoved) {
			_time = time;
		}
		if (_atlas.HasProgram() && (_atlasDirty || (clockMoved && _atlasTimed))) {
			_atlas.Submit(_time, atlasClear);
			_atlasDirty = false;
		}
		if (_mainMaterial && _mainTarget != INVALID_VIEW_TARGET && (_mainDirty || (clockMoved && _mainTimed))) {
			const Handle<RenderPass> row[] = {Handle<RenderPass>(_mainRow)};
			const bool fullscreen = _mainRow && _mainRow->material.IsValid();
			if (EditorPreviewStage::Submit(_stage, _mainTarget, _mainSize, _bounds, _orbit, &_time,
										   fullscreen ? std::span<const Handle<RenderPass>>(row) : std::span<const Handle<RenderPass>>())) {
				_mainDirty = false;
			}
		}
	}

	void NodeShaderPreviews::EnsureStage() {
		if (_stage.IsValid()) {
			return;
		}
		_stage = SceneSystem::CreateIsolatedScene("NodeShaderMainPreview");
		_subject = SceneSystem::CreateObject("Subject", _stage);
		if (_subject.IsValid()) {
			_subject->AddComponent<MeshRenderer>();
		}
		EditorPreviewStage::AddLights(_stage);
		ApplyMesh();
	}

	void NodeShaderPreviews::ApplyMesh() {
		if (!_subject.IsValid()) {
			return;
		}
		Asset* loaded = _mesh == NodeShaderPreviewMesh::Custom && _customMesh.IsValid()
							? AssetDatabase::LoadAssetByGuid(_customMesh)
							: AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(ShapeOf(_mesh))));
		if (loaded && loaded->GetAssetTypeId() != MeshAsset::GetReflectType()->name) {
			AssetDatabase::UnloadAsset(loaded);
			loaded = nullptr;
		}
		if (!loaded) {
			return;
		}
		if (_meshAsset) {
			AssetDatabase::UnloadAsset(_meshAsset);
		}
		_meshAsset = loaded;
		MeshAsset* mesh = static_cast<MeshAsset*>(loaded);
		_bounds = mesh->bounds;
		if (Handle<MeshRenderer> renderer = _subject->GetComponent<MeshRenderer>(); renderer.IsValid()) {
			renderer->SetMesh(Handle<MeshAsset>(mesh));
		}
		_mainDirty = true;
	}

	void NodeShaderPreviews::SetMesh(NodeShaderPreviewMesh mesh, const Guid& custom) {
		if (mesh == _mesh && custom == _customMesh) {
			return;
		}
		_mesh = mesh;
		_customMesh = custom;
		ApplyMesh();
	}

	void NodeShaderPreviews::SetMainSize(Vec2I size) {
		if (size.x <= 0 || size.y <= 0 || size == _mainSize || !RenderSystem::IsRenderingAvailable()) {
			return;
		}
		_mainSize = size;
		if (_mainTarget == INVALID_VIEW_TARGET) {
			_mainTarget = RenderSystem::CreateViewTarget(size);
		} else {
			RenderSystem::ResizeViewTarget(_mainTarget, size);
		}
		_mainDirty = true;
	}

	void NodeShaderPreviews::SetOrbit(const AssetPreviewOrbit& orbit) {
		_orbit = orbit;
		_mainDirty = true;
	}

	RHI::TextureHandle NodeShaderPreviews::GetMainTexture() const {
		return _mainTarget != INVALID_VIEW_TARGET ? RenderSystem::GetViewTargetTexture(_mainTarget) : RHI::TextureHandle{};
	}

	void NodeShaderPreviews::AdoptSurface(ShaderAsset* shader) {
		if (!shader) {
			return;
		}
		EnsureStage();
		MaterialAsset* material = GraphPreviewMaterials::Make(*shader, _properties);
		const bool fullscreen = shader->integrator == ShaderTemplate::None;
		if (fullscreen && !_neutral) {
			_neutral = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
		}
		if (fullscreen && !_mainRow) {
			_mainRow = New<FullScreenPass>(MemTag::Editor);
		}
		if (_mainRow) {
			_mainRow->material = fullscreen ? Handle<MaterialAsset>(material) : Handle<MaterialAsset>();
		}
		const bool sky = shader->integrator == ShaderTemplate::Sky;
		EditorPreviewStage::SetSky(_stage, sky ? Handle<MaterialAsset>(material) : Handle<MaterialAsset>());
		if (_subject.IsValid()) {
			_subject->SetActive(!sky);
			if (Handle<MeshRenderer> renderer = _subject->GetComponent<MeshRenderer>(); renderer.IsValid()) {
				MaterialAsset* worn = fullscreen ? static_cast<MaterialAsset*>(_neutral) : sky ? nullptr
																							   : material;
				renderer->SetMaterial(0, Handle<MaterialAsset>(worn));
			}
		}
		ReleaseMain();
		_mainShader = shader;
		_mainMaterial = material;
		_mainTimed = GraphSurface::ReadsClock(_surface.submitted);
		_mainDirty = true;
	}

	void NodeShaderPreviews::ReleaseMain() {
		if (_mainMaterial) {
			Delete(_mainMaterial);
			_mainMaterial = nullptr;
		}
		if (_mainShader) {
			Delete(_mainShader);
			_mainShader = nullptr;
		}
	}

} // namespace Shalltear
