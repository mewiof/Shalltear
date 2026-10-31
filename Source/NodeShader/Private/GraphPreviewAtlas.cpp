#include "GraphPreviewAtlas.h"

#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Shalltear {

	namespace {

		UniformMemberType MemberKind(GraphPropertyKind kind) {
			switch (kind) {
			case GraphPropertyKind::Float:
				return UniformMemberType::Float;
			case GraphPropertyKind::Integer:
			case GraphPropertyKind::Boolean:
			case GraphPropertyKind::BooleanKeyword:
			case GraphPropertyKind::EnumKeyword:
				return UniformMemberType::Int;
			case GraphPropertyKind::Vector2:
				return UniformMemberType::Vec2;
			case GraphPropertyKind::Vector3:
				return UniformMemberType::Vec3;
			case GraphPropertyKind::Vector4:
			case GraphPropertyKind::Color:
				return UniformMemberType::Vec4;
			case GraphPropertyKind::Texture2D:
			case GraphPropertyKind::Texture2DArray:
			case GraphPropertyKind::Texture3D:
			case GraphPropertyKind::Cubemap:
				break;
			}
			return UniformMemberType::Unsupported;
		}

	} // anonymous namespace

	namespace GraphPreviewMaterials {

		MaterialAsset* Make(ShaderAsset& shader, std::span<const GraphPropertyInfo> properties) {
			MaterialAsset* material = New<MaterialAsset>(MemTag::Editor);
			material->shader = Handle<ShaderAsset>(&shader);
			Vector<str> textureSlots{GetMemoryResource(MemTag::Editor)};
			shader.GetMaterialTextureSlots(textureSlots);
			material->SyncToLayout(shader.GetMaterialParamLayout(), {textureSlots.data(), textureSlots.size()},
								   {shader.memberDefaults.data(), shader.memberDefaults.size()});
			Apply(*material, properties);
			return material;
		}

		void Apply(MaterialAsset& material, std::span<const GraphPropertyInfo> properties) {
			for (const GraphPropertyInfo& property : properties) {
				const UniformMemberType kind = MemberKind(property.kind);
				if (kind == UniformMemberType::Unsupported) {
					continue;
				}
				MaterialAsset::Param value = MaterialAsset::MakeParam(kind);
				if (kind == UniformMemberType::Int) {
					value.integer = static_cast<i32>(property.defaultValue[0]);
				} else {
					std::memcpy(value.numbers, property.defaultValue, sizeof(value.numbers));
				}
				material.SetParam(GraphProperties::MemberName(property), value);
			}
			Vector<str> enabled{GetMemoryResource(MemTag::Editor)};
			for (const GraphPropertyInfo& property : properties) {
				if (str keyword = GraphProperties::DefaultKeyword(property); !keyword.empty()) {
					enabled.push_back(std::move(keyword));
				}
			}
			material.SetKeywords({enabled.data(), enabled.size()});
		}

	} // namespace GraphPreviewMaterials

	void GraphPreviewRow::Record(PassContext& context) {
		drew = false;
		if (tiles == 0 || !material.IsValid()) {
			return;
		}
		const MaterialAsset* asset = material.Get();
		const ShaderAsset* shader = asset ? asset->shader.Get() : nullptr;
		const size_t declared = shader ? std::min<size_t>(sizeof(push), shader->pushConstantSize) : 0;
		drew = context.DrawProcedural(material, {reinterpret_cast<const u8*>(&push), declared}, 6, tiles);
	}

	GraphPreviewAtlas::GraphPreviewAtlas() = default;

	GraphPreviewAtlas::~GraphPreviewAtlas() {
		Release();
		if (_target != INVALID_VIEW_TARGET) {
			RenderSystem::DestroyViewTarget(_target);
			_target = INVALID_VIEW_TARGET;
		}
		if (_stage.IsValid()) {
			SceneSystem::UnloadScene(_stage);
			_stage = {};
		}
	}

	void GraphPreviewAtlas::Release() {
		_row.material = {};
		_row.tiles = 0;
		// free the material before its shader
		if (_material) {
			Delete(_material);
			_material = nullptr;
		}
		if (_shader) {
			Delete(_shader);
			_shader = nullptr;
		}
	}

	bool GraphPreviewAtlas::Adopt(ShaderAsset* shader, const GraphPreview::Program& program, std::span<const GraphPropertyInfo> properties) {
		if (!shader) {
			return false;
		}
		const u32 tiles = static_cast<u32>(program.tiles.size());
		const Vec2I size{static_cast<i32>(GraphPreview::TILE_SIZE * GraphPreview::ATLAS_COLUMNS),
						 static_cast<i32>(GraphPreview::TILE_SIZE * (program.rows > 0 ? program.rows : 1))};
		if (!RenderSystem::IsRenderingAvailable()) {
			Delete(shader);
			return false;
		}
		if (_target == INVALID_VIEW_TARGET) {
			_target = RenderSystem::CreateViewTarget(size);
			if (_target == INVALID_VIEW_TARGET) {
				Delete(shader);
				return false;
			}
		} else if (size != _size) {
			RenderSystem::ResizeViewTarget(_target, size);
		}
		_size = size;
		if (!_stage.IsValid()) {
			_stage = SceneSystem::CreateIsolatedScene("NodeShaderPreviewAtlas");
		}
		Release();
		_shader = shader;
		_material = GraphPreviewMaterials::Make(*shader, properties);
		_rows = program.rows;
		_row.material = Handle<MaterialAsset>(_material);
		_row.tiles = tiles;
		return true;
	}

	void GraphPreviewAtlas::SetProperties(std::span<const GraphPropertyInfo> properties) {
		if (_material) {
			GraphPreviewMaterials::Apply(*_material, properties);
		}
	}

	void GraphPreviewAtlas::Submit(f64 time, Color clear) {
		if (!_material || _target == INVALID_VIEW_TARGET) {
			return;
		}
		_time = time;
		const f32 t = static_cast<f32>(time);
		_row.push.time[0] = t;
		_row.push.time[1] = std::sin(t);
		_row.push.time[2] = std::cos(t);
		_row.push.time[3] = 0.0f;

		const Handle<RenderPass> rows[] = {Handle<RenderPass>(&_row)};
		const Handle<Scene> stage[] = {_stage};
		ViewDesc desc;
		desc.clearColor = clear;
		desc.passes = rows;
		desc.scenes = stage;
		desc.time = &_time;
		desc.tonemap = false;
		RenderSystem::SubmitView(_target, desc);
	}

	RHI::TextureHandle GraphPreviewAtlas::GetTexture() const {
		return _target != INVALID_VIEW_TARGET ? RenderSystem::GetViewTargetTexture(_target) : RHI::TextureHandle{};
	}

	UI::Rect GraphPreviewAtlas::TileUV(u32 tile) const {
		const f32 columns = static_cast<f32>(GraphPreview::ATLAS_COLUMNS);
		const f32 rows = static_cast<f32>(_rows > 0 ? _rows : 1);
		return UI::Rect{static_cast<f32>(tile % GraphPreview::ATLAS_COLUMNS) / columns, static_cast<f32>(tile / GraphPreview::ATLAS_COLUMNS) / rows,
						1.0f / columns, 1.0f / rows};
	}

} // namespace Shalltear
