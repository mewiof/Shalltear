#include "FBXParser.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringUtil.h"

#include "ufbx.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <span>

namespace Shalltear {

	namespace {

		void* FBXAlloc(void*, size_t size) {
			return Allocate(size, MemTag::Asset);
		}

		void FBXFree(void*, void* pointer, size_t) {
			Free(pointer);
		}

		void ConfigureAllocator(ufbx_allocator_opts& allocator) {
			allocator.allocator.alloc_fn = &FBXAlloc;
			allocator.allocator.free_fn = &FBXFree;
		}

		Vec3 ToVec3(const ufbx_vec3& v) {
			return Vec3(static_cast<f32>(v.x), static_cast<f32>(v.y), static_cast<f32>(v.z));
		}

		Quat ToQuat(const ufbx_quat& q) {
			return Quat(static_cast<f32>(q.x), static_cast<f32>(q.y), static_cast<f32>(q.z), static_cast<f32>(q.w));
		}

		Mat4 ToMat4(const ufbx_matrix& m) {
			Mat4 out = Mat4::Identity();
			for (size_t c = 0; c < 4; ++c) {
				out.data[c * 4 + 0] = static_cast<f32>(m.cols[c].x);
				out.data[c * 4 + 1] = static_cast<f32>(m.cols[c].y);
				out.data[c * 4 + 2] = static_cast<f32>(m.cols[c].z);
			}
			return out;
		}

		std::string_view ToView(const ufbx_string& s) {
			return std::string_view(s.data, s.length);
		}

		template <size_t N>
		ufbx_string FromLiteral(const char (&text)[N]) {
			return ufbx_string{text, N - 1};
		}

		// never absolute_filename (exporting machine's path)
		str ResolveExternalImage(std::string_view relative, const str& baseDir) {
			str normalized = StringUtil::Replace(relative, "\\", "/");
			return (Path(baseDir) / normalized).ToString();
		}

		str ImageNameFromPath(std::string_view relative) {
			str normalized = StringUtil::Replace(relative, "\\", "/");
			return std::filesystem::path(normalized).stem().string();
		}

		str ImageExtensionFromPath(std::string_view relative, std::string_view fallback) {
			str normalized = StringUtil::Replace(relative, "\\", "/");
			str extension = StringUtil::ToLower(std::filesystem::path(normalized).extension().string());
			return extension.empty() ? str(fallback) : extension;
		}

		f32 MapValue(const ufbx_material_map& map, f32 whenAbsent) {
			return map.has_value ? static_cast<f32>(map.value_real) : whenAbsent;
		}

		i32 MapImage(const ufbx_material_map& map, size_t imageCount) {
			if (map.texture == nullptr || !map.texture->has_file ||
				map.texture->file_index >= imageCount) {
				return -1;
			}
			return static_cast<i32>(map.texture->file_index);
		}

		i32 PackedMetallicRoughness(const ufbx_material_pbr_maps& pbr, size_t imageCount) {
			const i32 metalness = MapImage(pbr.metalness, imageCount);
			const i32 roughness = MapImage(pbr.roughness, imageCount);
			return metalness >= 0 && metalness == roughness ? metalness : -1;
		}

		bool DropsAuthoredContent(const ufbx_material& source, std::span<const ufbx_texture* const> bound) {
			for (size_t i = 0; i < source.textures.count; ++i) {
				const ufbx_texture* attached = source.textures.data[i].texture;
				if (attached == nullptr) {
					continue;
				}
				if (std::find(bound.begin(), bound.end(), attached) == bound.end()) {
					return true;
				}
			}
			return false;
		}

		bool EmitMeshPart(const ufbx_mesh& source, const ufbx_mesh_part& part, i32 fileMaterial,
						  const ufbx_skin_deformer* skin,
						  Vector<u32>& triangleScratch, Vector<MeshVertex>& vertexScratch, Vector<u32>& indexScratch,
						  Vector<ModelSkinInfluence>& influenceScratch,
						  Vector<u32>& controlPointScratch, Vector<u32>* outControlPoints,
						  ModelMesh& mesh, str& error) {
			if (part.num_triangles == 0) {
				return true;
			}

			const size_t indexCount = part.num_triangles * 3;
			vertexScratch.clear();
			vertexScratch.resize(indexCount);
			indexScratch.clear();
			indexScratch.resize(indexCount);
			influenceScratch.clear();
			influenceScratch.resize(skin != nullptr ? indexCount : 0);
			controlPointScratch.clear();
			controlPointScratch.resize(outControlPoints != nullptr ? indexCount : 0);
			triangleScratch.clear();
			triangleScratch.resize(source.max_face_triangles * 3);

			AABB partBounds = AABB::Empty();

			size_t written = 0;
			for (size_t f = 0; f < part.num_faces; ++f) {
				const ufbx_face face = source.faces.data[part.face_indices.data[f]];
				const size_t triangles = ufbx_triangulate_face(triangleScratch.data(), triangleScratch.size(), &source, face);
				for (size_t corner = 0; corner < triangles * 3; ++corner) {
					if (written >= indexCount) {
						error = std::format("mesh '{}' triangulated to more corners than its face counts declare", mesh.name);
						return false;
					}
					const u32 index = triangleScratch[corner];

					MeshVertex vertex;
					vertex.position = ToVec3(ufbx_get_vertex_vec3(&source.vertex_position, index));

					if (source.vertex_normal.exists) {
						vertex.normal = ToVec3(ufbx_get_vertex_vec3(&source.vertex_normal, index));
					}
					if (source.vertex_tangent.exists) {
						vertex.tangent = ToVec3(ufbx_get_vertex_vec3(&source.vertex_tangent, index));
						if (source.vertex_bitangent.exists) {
							Vec3 bitangent = ToVec3(ufbx_get_vertex_vec3(&source.vertex_bitangent, index));
							vertex.tangentSign = Vec3::Dot(Vec3::Cross(vertex.normal, vertex.tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
						}
					}
					if (source.vertex_uv.exists) {
						const ufbx_vec2 uv = ufbx_get_vertex_vec2(&source.vertex_uv, index);
						vertex.u = static_cast<f32>(uv.x);
						vertex.v = 1.0f - static_cast<f32>(uv.y);
					}
					if (source.vertex_color.exists) {
						const ufbx_vec4 c = ufbx_get_vertex_vec4(&source.vertex_color, index);
						vertex.color = Color(static_cast<f32>(c.x), static_cast<f32>(c.y),
											 static_cast<f32>(c.z), static_cast<f32>(c.w))
										   .ToColor32();
					}

					if (skin != nullptr) {
						ModelSkinInfluence influence;
						const u32 vertexIndex = source.vertex_indices.data[index];
						if (vertexIndex < skin->vertices.count) {
							const ufbx_skin_vertex bound = skin->vertices.data[vertexIndex];
							const u32 lanes = bound.num_weights < 4 ? bound.num_weights : 4;
							for (u32 lane = 0; lane < lanes; ++lane) {
								const ufbx_skin_weight bone = skin->weights.data[bound.weight_begin + lane];
								influence.joints[lane] = static_cast<u16>(bone.cluster_index);
								influence.weights[lane] = static_cast<f32>(bone.weight);
							}
						}
						NormalizeInfluence(influence);
						influenceScratch[written] = influence;
					}

					if (outControlPoints != nullptr) {
						controlPointScratch[written] = source.vertex_indices.data[index];
					}

					vertexScratch[written++] = vertex;
				}
			}

			if (written == 0) {
				return true;
			}

			ufbx_allocator_opts weldAllocator{};
			ConfigureAllocator(weldAllocator);
			ufbx_error weldError;
			ufbx_vertex_stream streams[3];
			streams[0].data = vertexScratch.data();
			streams[0].vertex_count = written;
			streams[0].vertex_size = sizeof(MeshVertex);
			size_t streamCount = 1;
			if (skin != nullptr) {
				streams[streamCount].data = influenceScratch.data();
				streams[streamCount].vertex_count = written;
				streams[streamCount].vertex_size = sizeof(ModelSkinInfluence);
				++streamCount;
			}
			if (outControlPoints != nullptr) {
				streams[streamCount].data = controlPointScratch.data();
				streams[streamCount].vertex_count = written;
				streams[streamCount].vertex_size = sizeof(u32);
				++streamCount;
			}
			const size_t uniqueVertices = ufbx_generate_indices(streams, streamCount, indexScratch.data(), written, &weldAllocator, &weldError);
			if (weldError.type != UFBX_ERROR_NONE) {
				char described[512];
				ufbx_format_error(described, sizeof(described), &weldError);
				error = std::format("mesh '{}' couldn't be indexed: {}", mesh.name, described);
				return false;
			}

			const u32 baseVertex = static_cast<u32>(mesh.vertices.size());
			const u32 firstIndex = static_cast<u32>(mesh.indices.size());
			for (size_t i = 0; i < uniqueVertices; ++i) {
				mesh.bounds.Encapsulate(vertexScratch[i].position);
				partBounds.Encapsulate(vertexScratch[i].position);
				mesh.vertices.push_back(vertexScratch[i]);
			}
			if (skin != nullptr) {
				mesh.influences.resize(baseVertex);
				for (size_t i = 0; i < uniqueVertices; ++i) {
					mesh.influences.push_back(influenceScratch[i]);
				}
			}
			if (!mesh.influences.empty()) {
				mesh.influences.resize(mesh.vertices.size());
			}
			if (outControlPoints != nullptr) {
				outControlPoints->resize(baseVertex);
				for (size_t i = 0; i < uniqueVertices; ++i) {
					outControlPoints->push_back(controlPointScratch[i]);
				}
			}
			for (size_t i = 0; i < written; ++i) {
				mesh.indices.push_back(baseVertex + indexScratch[i]);
			}

			SubMesh submesh;
			submesh.firstIndex = firstIndex;
			submesh.indexCount = static_cast<u32>(written);
			submesh.bounds = partBounds;
			submesh.materialSlot = MaterialSlotFor(mesh, fileMaterial);
			mesh.submeshes.push_back(submesh);
			return true;
		}

		struct BlendChannelSlot {
			u32 elementId = 0;
			i32 mesh = -1;
			i32 channel = -1;
		};

		void ReadBlendShapes(const ufbx_mesh& source, const Vector<u32>& controlPoints, i32 meshIndex,
							 ModelMesh& mesh, Vector<BlendChannelSlot>& slots) {
			if (source.blend_deformers.count == 0 || controlPoints.empty()) {
				return;
			}

			const size_t pointCount = source.num_vertices;
			Vector<u32> firstVertex(pointCount + 1, 0u, GetMemoryResource(MemTag::Asset));
			for (const u32 point : controlPoints) {
				if (point < pointCount) {
					++firstVertex[point + 1];
				}
			}
			for (size_t p = 0; p < pointCount; ++p) {
				firstVertex[p + 1] += firstVertex[p];
			}
			Vector<u32> cursor(firstVertex.begin(), firstVertex.end(), GetMemoryResource(MemTag::Asset));
			Vector<u32> verticesOfPoint(controlPoints.size(), 0u, GetMemoryResource(MemTag::Asset));
			for (size_t v = 0; v < controlPoints.size(); ++v) {
				const u32 point = controlPoints[v];
				if (point < pointCount) {
					verticesOfPoint[cursor[point]++] = static_cast<u32>(v);
				}
			}

			for (size_t d = 0; d < source.blend_deformers.count; ++d) {
				const ufbx_blend_deformer& deformer = *source.blend_deformers.data[d];
				for (size_t c = 0; c < deformer.channels.count; ++c) {
					const ufbx_blend_channel& channel = *deformer.channels.data[c];

					ModelBlendChannel out;
					out.name = str(ToView(channel.name));
					if (out.name.empty()) {
						out.name = std::format("shape{}", c);
					}

					for (size_t k = 0; k < channel.keyframes.count; ++k) {
						const ufbx_blend_keyframe& keyframe = channel.keyframes.data[k];
						if (keyframe.shape == nullptr) {
							continue;
						}
						const ufbx_blend_shape& shape = *keyframe.shape;

						ModelBlendShape target;
						target.targetWeight = static_cast<f32>(keyframe.target_weight) * 100.0f;

						for (size_t o = 0; o < shape.num_offsets; ++o) {
							const u32 point = shape.offset_vertices.data[o];
							if (point >= pointCount) {
								continue;
							}
							const Vec3 position = ToVec3(shape.position_offsets.data[o]);
							const bool hasNormals = o < shape.normal_offsets.count;
							const Vec3 normal = hasNormals ? ToVec3(shape.normal_offsets.data[o]) : Vec3{};
							for (u32 slot = firstVertex[point]; slot < firstVertex[point + 1]; ++slot) {
								target.offsetVertices.push_back(verticesOfPoint[slot]);
								target.positionOffsets.push_back(position);
								if (hasNormals) {
									target.normalOffsets.push_back(normal);
								}
							}
						}
						if (!target.normalOffsets.empty()) {
							target.normalOffsets.resize(target.positionOffsets.size());
						}
						if (!target.offsetVertices.empty()) {
							out.shapes.push_back(std::move(target));
						}
					}

					std::stable_sort(out.shapes.begin(), out.shapes.end(),
									 [](const ModelBlendShape& a, const ModelBlendShape& b) { return a.targetWeight < b.targetWeight; });
					if (!out.shapes.empty()) {
						BlendChannelSlot slot;
						slot.elementId = channel.element.element_id;
						slot.mesh = meshIndex;
						slot.channel = static_cast<i32>(mesh.blendChannels.size());
						slots.push_back(slot);
						mesh.blendChannels.push_back(std::move(out));
					}
				}
			}
		}

		template <typename KeyList, typename ToValue>
		void EmitBakedChannel(const KeyList& keys, bool constant, i32 node, ModelChannel channel,
							  ToValue toValue, ModelParseResult& result, ModelTakeRange& take,
							  i32 blendChannel = -1) {
			if (keys.count == 0) {
				return;
			}
			const size_t emitted = constant ? 1 : keys.count;

			ModelTrack track;
			track.node = node;
			track.channel = channel;
			track.blendChannel = blendChannel;
			track.interp = ModelInterpolation::Linear;
			track.times.reserve(emitted);
			track.values.reserve(emitted);
			for (size_t i = 0; i < emitted; ++i) {
				track.times.push_back(static_cast<f32>(keys.data[i].time));
				track.values.push_back(toValue(keys.data[i].value));
			}

			take.tracks.push_back(static_cast<i32>(result.tracks.size()));
			result.tracks.push_back(std::move(track));
		}

		void EmitTakes(const ufbx_scene& scene, std::span<const i32> ufbxNodeToSlot,
					   std::span<const BlendChannelSlot> blendChannelSlots,
					   const Path& sourcePhysicalPath, ModelParseResult& result) {
			ufbx_bake_opts options{};
			ConfigureAllocator(options.temp_allocator);
			ConfigureAllocator(options.result_allocator);

			for (size_t s = 0; s < scene.anim_stacks.count; ++s) {
				const ufbx_anim_stack& stack = *scene.anim_stacks.data[s];

				ufbx_error error;
				ufbx_baked_anim* baked = ufbx_bake_anim(&scene, stack.anim, &options, &error);
				if (baked == nullptr) {
					char described[512];
					ufbx_format_error(described, sizeof(described), &error);
					SLT_LOG_WARN("Graphics", "FBX: take '{}' of '{}' couldn't be evaluated (dropped): {}",
								 ToView(stack.name), sourcePhysicalPath.GetFilename(), described);
					continue;
				}

				ModelTakeRange take;
				take.name = str(ToView(stack.name));
				take.begin = static_cast<f32>(stack.time_begin);
				take.end = static_cast<f32>(stack.time_end);

				for (size_t n = 0; n < baked->nodes.count; ++n) {
					const ufbx_baked_node& node = baked->nodes.data[n];
					const i32 slot = node.typed_id < ufbxNodeToSlot.size() ? ufbxNodeToSlot[node.typed_id] : -1;
					if (slot < 0) {
						continue;
					}
					EmitBakedChannel(node.translation_keys, node.constant_translation, slot, ModelChannel::Translation, [](const ufbx_vec3& v) { return Vec4(ToVec3(v), 0.0f); }, result, take);
					EmitBakedChannel(node.rotation_keys, node.constant_rotation, slot, ModelChannel::Rotation, [](const ufbx_quat& q) { const Quat r = ToQuat(q); return Vec4(r.x, r.y, r.z, r.w); }, result, take);
					EmitBakedChannel(node.scale_keys, node.constant_scale, slot, ModelChannel::Scale, [](const ufbx_vec3& v) { return Vec4(ToVec3(v), 0.0f); }, result, take);
				}

				for (size_t e = 0; e < baked->elements.count && !blendChannelSlots.empty(); ++e) {
					const ufbx_baked_element& element = baked->elements.data[e];
					const BlendChannelSlot* slot = nullptr;
					for (const BlendChannelSlot& candidate : blendChannelSlots) {
						if (candidate.elementId == element.element_id) {
							slot = &candidate;
							break;
						}
					}
					if (slot == nullptr) {
						continue;
					}
					for (size_t p = 0; p < element.props.count; ++p) {
						const ufbx_baked_prop& prop = element.props.data[p];
						if (ToView(prop.name) != "DeformPercent") {
							continue;
						}
						for (size_t n = 0; n < result.nodes.size(); ++n) {
							if (result.nodes[n].mesh != slot->mesh) {
								continue;
							}
							// already percent, 0-100
							EmitBakedChannel(prop.keys, prop.constant_value, static_cast<i32>(n), ModelChannel::MorphWeight, [](const ufbx_vec3& v) { return Vec4(static_cast<f32>(v.x), 0.0f, 0.0f, 0.0f); }, result, take, slot->channel);
						}
					}
				}

				ufbx_free_baked_anim(baked);
				result.takes.push_back(std::move(take));
			}
		}

	} // anonymous namespace

	ModelParseResult ParseFBX(const Path& sourcePhysicalPath, const ModelParseOptions& parseOptions) {
		ModelParseResult result;

		ufbx_load_opts options{};
		ConfigureAllocator(options.temp_allocator);
		ConfigureAllocator(options.result_allocator);

		options.target_unit_meters = 1.0;

		options.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;

		options.target_axes = ufbx_axes_left_handed_y_up;

		options.handedness_conversion_axis = UFBX_MIRROR_AXIS_Z;

		options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;

		options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
		options.scale_helper_name = FromLiteral("(scale compensation)");
		options.geometry_transform_helper_name = FromLiteral("(geometry transform)");

		options.generate_missing_normals = true;

		options.ignore_animation = !parseOptions.tracks;

		options.skip_skin_vertices = false;

		options.clean_skin_weights = true;

		options.load_external_files = false;
		options.ignore_missing_external_files = true;

		const str& physical = sourcePhysicalPath.ToString();
		options.filename = ufbx_string{physical.c_str(), physical.size()};

		Buffer file = FileSystem::ReadFile(sourcePhysicalPath, MemTag::Asset);
		if (!file || file.size == 0) {
			result.error = "file unreadable or empty";
			return result;
		}

		ufbx_error error;
		ufbx_scene* scene = ufbx_load_memory(file.data, file.size, &options, &error);
		if (!scene) {
			char described[1024];
			ufbx_format_error(described, sizeof(described), &error);
			result.error = str(described);
			return result;
		}

		const str baseDir = std::filesystem::path(physical).parent_path().generic_string();

		for (size_t i = 0; i < scene->texture_files.count; ++i) {
			const ufbx_texture_file& file = scene->texture_files.data[i];
			std::string_view relative = ToView(file.relative_filename);
			if (relative.empty()) {
				relative = ToView(file.filename);
			}

			ModelImage image;
			image.name = SanitizeName(ImageNameFromPath(relative), "image", static_cast<u32>(i));
			image.extension = ImageExtensionFromPath(relative, ".png");
			if (file.content.size > 0) {
				const u8* bytes = static_cast<const u8*>(file.content.data);
				image.embeddedBytes.assign(bytes, bytes + file.content.size);
			} else if (!relative.empty()) {
				image.externalPath = ResolveExternalImage(relative, baseDir);
			}
			result.images.push_back(std::move(image));
		}

		size_t simplifiedMaterials = 0;
		for (size_t i = 0; i < scene->materials.count; ++i) {
			const ufbx_material& source = *scene->materials.data[i];
			ModelMaterial material;
			material.name = SanitizeName(ToView(source.name), "material", static_cast<u32>(i));

			const ufbx_material_pbr_maps& pbr = source.pbr;
			const f32 baseFactor = static_cast<f32>(pbr.base_factor.value_real);
			material.baseColorFactor = Color(
				static_cast<f32>(pbr.base_color.value_vec4.x) * baseFactor,
				static_cast<f32>(pbr.base_color.value_vec4.y) * baseFactor,
				static_cast<f32>(pbr.base_color.value_vec4.z) * baseFactor,
				MapValue(pbr.opacity, 1.0f));
			material.metallicFactor = MapValue(pbr.metalness, 0.0f);
			material.roughnessFactor = MapValue(pbr.roughness, 1.0f);

			if (pbr.emission_color.has_value) {
				const f32 emissionFactor = MapValue(pbr.emission_factor, 1.0f);
				material.emissiveFactor = Vec3(
					static_cast<f32>(pbr.emission_color.value_vec3.x) * emissionFactor,
					static_cast<f32>(pbr.emission_color.value_vec3.y) * emissionFactor,
					static_cast<f32>(pbr.emission_color.value_vec3.z) * emissionFactor);
			}

			const size_t imageCount = result.images.size();
			material.baseColorImage = MapImage(pbr.base_color, imageCount);
			material.metallicRoughnessImage = PackedMetallicRoughness(pbr, imageCount);
			material.normalImage = MapImage(pbr.normal_map, imageCount);
			material.occlusionImage = MapImage(pbr.ambient_occlusion, imageCount);
			material.emissiveImage = MapImage(pbr.emission_color, imageCount);

			const ufbx_texture* bound[] = {
				material.baseColorImage >= 0 ? pbr.base_color.texture : nullptr,
				material.metallicRoughnessImage >= 0 ? pbr.metalness.texture : nullptr,
				material.metallicRoughnessImage >= 0 ? pbr.roughness.texture : nullptr,
				material.normalImage >= 0 ? pbr.normal_map.texture : nullptr,
				material.occlusionImage >= 0 ? pbr.ambient_occlusion.texture : nullptr,
				material.emissiveImage >= 0 ? pbr.emission_color.texture : nullptr,
			};
			if (DropsAuthoredContent(source, bound)) {
				++simplifiedMaterials;
			}
			result.materials.push_back(std::move(material));
		}

		if (simplifiedMaterials > 0) {
			SLT_LOG_WARN("Graphics", "FBX: {} of {} material(s) in '{}' have texture maps the metallic-roughness mapping drops",
						 simplifiedMaterials, scene->materials.count, sourcePhysicalPath.GetFilename());
		}

		Vector<str> meshNames{GetMemoryResource(MemTag::Asset)};
		Vector<u32> triangleScratch{GetMemoryResource(MemTag::Asset)};
		Vector<MeshVertex> vertexScratch{GetMemoryResource(MemTag::Asset)};
		Vector<u32> indexScratch{GetMemoryResource(MemTag::Asset)};
		Vector<ModelSkinInfluence> influenceScratch{GetMemoryResource(MemTag::Asset)};
		Vector<u32> controlPointScratch{GetMemoryResource(MemTag::Asset)};
		Vector<u32> meshControlPoints{GetMemoryResource(MemTag::Asset)};
		Vector<BlendChannelSlot> blendChannelSlots{GetMemoryResource(MemTag::Asset)};
		size_t nonLinearSkins = 0;
		size_t extraDeformers = 0;
		for (size_t i = 0; i < scene->meshes.count; ++i) {
			const ufbx_mesh& source = *scene->meshes.data[i];

			const ufbx_skin_deformer* skin = nullptr;
			if (source.skin_deformers.count > 0) {
				skin = source.skin_deformers.data[0];
				extraDeformers += source.skin_deformers.count - 1;
				if (skin->skinning_method != UFBX_SKINNING_METHOD_LINEAR) {
					++nonLinearSkins;
				}
			}

			std::string_view rawName = ToView(source.name);
			if (rawName.empty() && source.instances.count == 1) {
				rawName = ToView(source.instances.data[0]->name);
			}

			ModelMesh mesh;
			mesh.name = UniquifyName(SanitizeName(rawName, "mesh", static_cast<u32>(i)), meshNames);

			const bool wantsControlPoints = source.blend_deformers.count > 0;
			meshControlPoints.clear();
			for (size_t p = 0; p < source.material_parts.count; ++p) {
				const ufbx_mesh_part& part = source.material_parts.data[p];
				i32 fileMaterial = -1;
				if (part.index < source.materials.count) {
					fileMaterial = static_cast<i32>(source.materials.data[part.index]->typed_id);
				}
				if (!EmitMeshPart(source, part, fileMaterial, skin, triangleScratch, vertexScratch, indexScratch, influenceScratch,
								  controlPointScratch, wantsControlPoints ? &meshControlPoints : nullptr, mesh, result.error)) {
					ufbx_free_scene(scene);
					return result;
				}
			}
			if (wantsControlPoints) {
				ReadBlendShapes(source, meshControlPoints, static_cast<i32>(i), mesh, blendChannelSlots);
			}
			result.meshes.push_back(std::move(mesh));
		}

		if (nonLinearSkins > 0) {
			SLT_LOG_WARN("Graphics", "FBX: {} skinned mesh(es) in '{}' ask for dual-quaternion skinning (imported as linear blend)",
						 nonLinearSkins, sourcePhysicalPath.GetFilename());
		}
		if (extraDeformers > 0) {
			SLT_LOG_WARN("Graphics", "FBX: '{}' stacks {} extra skin deformer(s) on a mesh (only the 1st is imported)",
						 sourcePhysicalPath.GetFilename(), extraDeformers);
		}

		struct PendingNode {
			const ufbx_node* node;
			i32 parentSlot;
		};
		Vector<PendingNode> pending{GetMemoryResource(MemTag::Asset)};
		Vector<u8> visited{GetMemoryResource(MemTag::Asset)};
		visited.resize(scene->nodes.count, 0);
		Vector<i32> ufbxNodeToSlot{GetMemoryResource(MemTag::Asset)};
		ufbxNodeToSlot.resize(scene->nodes.count, -1);
		pending.reserve(scene->root_node->children.count);
		for (size_t i = scene->root_node->children.count; i-- > 0;) {
			pending.push_back({scene->root_node->children.data[i], -1});
		}
		while (!pending.empty()) {
			const PendingNode current = pending.back();
			pending.pop_back();
			const ufbx_node& node = *current.node;
			if (node.typed_id < visited.size()) {
				if (visited[node.typed_id]) {
					continue;
				}
				visited[node.typed_id] = 1;
			}

			ModelNode entry;
			entry.name = str(ToView(node.name));
			entry.parent = current.parentSlot;

			entry.position = ToVec3(node.local_transform.translation);
			entry.rotation = ToQuat(node.local_transform.rotation);
			entry.scale = ToVec3(node.local_transform.scale);
			entry.localMatrix = Mat4::TRS(entry.position, entry.rotation, entry.scale);

			if (node.mesh != nullptr && node.mesh->typed_id < result.meshes.size()) {
				entry.mesh = static_cast<i32>(node.mesh->typed_id);
				if (node.mesh->skin_deformers.count > 0) {
					entry.skin = static_cast<i32>(node.mesh->skin_deformers.data[0]->typed_id);
				}
			}

			const i32 slot = static_cast<i32>(result.nodes.size());
			if (node.typed_id < ufbxNodeToSlot.size()) {
				ufbxNodeToSlot[node.typed_id] = slot;
			}
			result.nodes.push_back(std::move(entry));

			for (size_t i = node.children.count; i-- > 0;) {
				pending.push_back({node.children.data[i], slot});
			}
		}

		size_t bonelessClusters = 0;
		for (size_t i = 0; i < scene->skin_deformers.count; ++i) {
			const ufbx_skin_deformer& deformer = *scene->skin_deformers.data[i];
			ModelSkin entry;
			entry.joints.reserve(deformer.clusters.count);
			entry.inverseBind.reserve(deformer.clusters.count);
			for (size_t c = 0; c < deformer.clusters.count; ++c) {
				const ufbx_skin_cluster& cluster = *deformer.clusters.data[c];
				i32 slot = -1;
				if (cluster.bone_node != nullptr && cluster.bone_node->typed_id < ufbxNodeToSlot.size()) {
					slot = ufbxNodeToSlot[cluster.bone_node->typed_id];
				}
				if (slot < 0) {
					++bonelessClusters;
				}
				entry.joints.push_back(slot);
				entry.inverseBind.push_back(ToMat4(cluster.geometry_to_bone));
			}
			result.skins.push_back(std::move(entry));
		}
		if (bonelessClusters > 0) {
			SLT_LOG_WARN("Graphics", "FBX: {} skin cluster(s) in '{}' use no bone this import has a node for (influences bind to nothing)",
						 bonelessClusters, sourcePhysicalPath.GetFilename());
		}

		if (parseOptions.tracks) {
			EmitTakes(*scene, std::span<const i32>(ufbxNodeToSlot.data(), ufbxNodeToSlot.size()),
					  std::span<const BlendChannelSlot>(blendChannelSlots.data(), blendChannelSlots.size()),
					  sourcePhysicalPath, result);
		}

		if (result.nodes.empty() && result.meshes.empty()) {
			result.error = "the file has no nodes/geometry";
			ufbx_free_scene(scene);
			return result;
		}

		if (!result.meshes.empty()) {
			bool reachedGeometry = false;
			for (const ModelNode& node : result.nodes) {
				if (node.mesh >= 0 && !result.meshes[static_cast<size_t>(node.mesh)].vertices.empty()) {
					reachedGeometry = true;
					break;
				}
			}
			if (!reachedGeometry) {
				SLT_LOG_WARN("Graphics", "FBX: '{}' has {} mesh(es) but no triangles reach this import (importing the hierarchy only)", physical, result.meshes.size());
			}
		}

		for (size_t i = 0; i < scene->metadata.warnings.count; ++i) {
			const ufbx_warning& warning = scene->metadata.warnings.data[i];
			SLT_LOG_WARN("Graphics", "FBX: {} ({} occurrence(s)) in '{}'", ToView(warning.description), warning.count, physical);
		}

		ufbx_free_scene(scene);
		result.success = true;
		return result;
	}

} // namespace Shalltear
