#include "glTFParser.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/StringUtil.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <span>

namespace Shalltear {

	namespace {

		bool DecodeBase64(std::string_view text, Vector<u8>& out) {
			auto valueOf = [](char c) -> i32 {
				if (c >= 'A' && c <= 'Z') {
					return c - 'A';
				}
				if (c >= 'a' && c <= 'z') {
					return c - 'a' + 26;
				}
				if (c >= '0' && c <= '9') {
					return c - '0' + 52;
				}
				if (c == '+') {
					return 62;
				}
				if (c == '/') {
					return 63;
				}
				return -1;
			};

			out.clear();
			out.reserve((text.size() / 4) * 3);
			u32 accumulator = 0;
			u32 bits = 0;
			for (char c : text) {
				if (c == '=' || c == '\n' || c == '\r') {
					continue;
				}
				i32 value = valueOf(c);
				if (value < 0) {
					return false;
				}
				accumulator = (accumulator << 6) | static_cast<u32>(value);
				bits += 6;
				if (bits >= 8) {
					bits -= 8;
					out.push_back(static_cast<u8>((accumulator >> bits) & 0xFF));
				}
			}
			return true;
		}

		enum glTFComponentType : i64 {
			glTFByte = 5120,
			glTFUbyte = 5121,
			glTFShort = 5122,
			glTFUshort = 5123,
			glTFUint = 5125,
			glTFFloat = 5126,
		};

		u32 ComponentSize(i64 componentType) {
			switch (componentType) {
			case glTFByte:
			case glTFUbyte:
				return 1;
			case glTFShort:
			case glTFUshort:
				return 2;
			case glTFUint:
			case glTFFloat:
				return 4;
			default:
				return 0;
			}
		}

		u32 ComponentCount(std::string_view type) {
			if (type == "SCALAR") {
				return 1;
			}
			if (type == "VEC2") {
				return 2;
			}
			if (type == "VEC3") {
				return 3;
			}
			if (type == "VEC4") {
				return 4;
			}
			if (type == "MAT4") {
				return 16;
			}
			return 0;
		}

		struct AccessorView {
			const u8* data = nullptr;
			u32 stride = 0;
			u32 count = 0;
			i64 componentType = 0;
			u32 components = 0;
			bool normalized = false;
			bool valid = false;
		};

		struct ParseContext {
			const JSONValue* root = nullptr;
			Vector<Vector<u8>>* buffers = nullptr;
			str baseDir;
		};

		AccessorView ResolveAccessor(const ParseContext& context, i64 accessorIndex, str& error) {
			AccessorView view;
			const JSONValue& accessors = (*context.root)["accessors"];
			if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= accessors.Size()) {
				error = std::format("accessor {} out of range", accessorIndex);
				return view;
			}
			const JSONValue& accessor = accessors[static_cast<size_t>(accessorIndex)];
			if (accessor.HasKey("sparse")) {
				error = "sparse accessors aren't supported";
				return view;
			}

			view.componentType = accessor["componentType"].AsInt();
			view.components = ComponentCount(accessor["type"].AsString());
			view.count = static_cast<u32>(accessor["count"].AsInt());
			view.normalized = accessor["normalized"].AsBool(false);
			u32 componentSize = ComponentSize(view.componentType);
			if (view.components == 0 || componentSize == 0 || view.count == 0) {
				error = std::format("accessor {} malformed", accessorIndex);
				return view;
			}

			i64 bufferViewIndex = accessor["bufferView"].AsInt(-1);
			if (bufferViewIndex < 0) {
				error = std::format("accessor {} has no bufferView (zero-filled accessors unsupported)", accessorIndex);
				return view;
			}
			const JSONValue& bufferViews = (*context.root)["bufferViews"];
			if (static_cast<size_t>(bufferViewIndex) >= bufferViews.Size()) {
				error = std::format("bufferView {} out of range", bufferViewIndex);
				return view;
			}
			const JSONValue& bufferView = bufferViews[static_cast<size_t>(bufferViewIndex)];

			i64 bufferIndex = bufferView["buffer"].AsInt();
			if (bufferIndex < 0 || static_cast<size_t>(bufferIndex) >= context.buffers->size()) {
				error = std::format("buffer {} out of range", bufferIndex);
				return view;
			}
			const Vector<u8>& buffer = (*context.buffers)[static_cast<size_t>(bufferIndex)];

			u64 viewOffset = static_cast<u64>(bufferView["byteOffset"].AsInt(0));
			u64 viewLength = static_cast<u64>(bufferView["byteLength"].AsInt(0));
			u64 accessorOffset = static_cast<u64>(accessor["byteOffset"].AsInt(0));
			u32 naturalStride = componentSize * view.components;
			u32 stride = static_cast<u32>(bufferView["byteStride"].AsInt(naturalStride));
			if (stride == 0) {
				stride = naturalStride;
			}

			u64 last = viewOffset + accessorOffset + static_cast<u64>(stride) * (view.count - 1) + naturalStride;
			if (viewOffset + viewLength > buffer.size() || last > buffer.size()) {
				error = std::format("accessor {} reads past its buffer (truncated file?)", accessorIndex);
				return view;
			}

			view.data = buffer.data() + viewOffset + accessorOffset;
			view.stride = stride;
			view.valid = true;
			return view;
		}

		f32 ReadComponentAsFloat(const AccessorView& view, u32 element, u32 component) {
			const u8* p = view.data + static_cast<u64>(element) * view.stride + component * ComponentSize(view.componentType);
			switch (view.componentType) {
			case glTFFloat: {
				f32 value;
				std::memcpy(&value, p, 4);
				return value;
			}
			case glTFUbyte: {
				u8 value = *p;
				return view.normalized ? value / 255.0f : static_cast<f32>(value);
			}
			case glTFUshort: {
				u16 value;
				std::memcpy(&value, p, 2);
				return view.normalized ? value / 65535.0f : static_cast<f32>(value);
			}
			case glTFByte: {
				i8 value = static_cast<i8>(*p);
				return view.normalized ? std::max(value / 127.0f, -1.0f) : static_cast<f32>(value);
			}
			case glTFShort: {
				i16 value;
				std::memcpy(&value, p, 2);
				return view.normalized ? std::max(value / 32767.0f, -1.0f) : static_cast<f32>(value);
			}
			default:
				return 0.0f;
			}
		}

		u32 ReadComponentAsUint(const AccessorView& view, u32 element, u32 component) {
			const u8* p = view.data + static_cast<u64>(element) * view.stride + component * ComponentSize(view.componentType);
			switch (view.componentType) {
			case glTFUbyte:
				return *p;
			case glTFUshort: {
				u16 value;
				std::memcpy(&value, p, 2);
				return value;
			}
			case glTFUint: {
				u32 value;
				std::memcpy(&value, p, 4);
				return value;
			}
			default:
				return 0;
			}
		}

		// glTF right handed, Core left handed -> mirror x (pain in the ass)
		constexpr Vec3 MirrorX(const Vec3& v) noexcept {
			return Vec3(-v.x, v.y, v.z);
		}

		Mat4 MirrorXConjugate(const Mat4& matrix) noexcept {
			const Mat4 s = Mat4::TRS(Vec3::Zero(), Quat::Identity(), Vec3(-1.0f, 1.0f, 1.0f));
			return s * matrix * s;
		}

		Mat4 NodeLocalMatrix(const JSONValue& node) {
			if (node.HasKey("matrix")) {
				const JSONValue& m = node["matrix"];
				Mat4 matrix;
				for (size_t i = 0; i < 16 && i < m.Size(); ++i) {
					matrix.data[i] = static_cast<f32>(m[i].AsNumber());
				}
				return matrix;
			}
			Vec3 translation{};
			Quat rotation = Quat::Identity();
			Vec3 scale{1.0f, 1.0f, 1.0f};
			if (node.HasKey("translation")) {
				const JSONValue& t = node["translation"];
				translation = Vec3(static_cast<f32>(t[size_t{0}].AsNumber()), static_cast<f32>(t[size_t{1}].AsNumber()), static_cast<f32>(t[size_t{2}].AsNumber()));
			}
			if (node.HasKey("rotation")) {
				const JSONValue& r = node["rotation"];
				rotation = Quat(static_cast<f32>(r[size_t{0}].AsNumber()), static_cast<f32>(r[size_t{1}].AsNumber()), static_cast<f32>(r[size_t{2}].AsNumber()), static_cast<f32>(r[size_t{3}].AsNumber()));
			}
			if (node.HasKey("scale")) {
				const JSONValue& s = node["scale"];
				scale = Vec3(static_cast<f32>(s[size_t{0}].AsNumber()), static_cast<f32>(s[size_t{1}].AsNumber()), static_cast<f32>(s[size_t{2}].AsNumber()));
			}
			return Mat4::TRS(translation, rotation, scale);
		}

		void EmitPrimitiveTargets(const ParseContext& context, const JSONValue& primitive, const JSONValue& targetNames,
								  u32 baseVertex, u32 vertexCount, ModelMesh& mesh) {
			const JSONValue& targets = primitive["targets"];
			for (size_t t = 0; t < targets.Size(); ++t) {
				while (mesh.blendChannels.size() <= t) {
					ModelBlendChannel fresh;
					const size_t index = mesh.blendChannels.size();
					fresh.name = index < targetNames.Size() ? str(targetNames[index].AsString()) : std::format("shape{}", index);
					if (fresh.name.empty()) {
						fresh.name = std::format("shape{}", index);
					}
					ModelBlendShape only;
					only.targetWeight = 100.0f;
					fresh.shapes.push_back(std::move(only));
					mesh.blendChannels.push_back(std::move(fresh));
				}
				ModelBlendShape& shape = mesh.blendChannels[t].shapes[0];

				str ignored;
				const JSONValue& attributes = targets[t];
				AccessorView positions{};
				AccessorView normals{};
				if (attributes.HasKey("POSITION")) {
					positions = ResolveAccessor(context, attributes["POSITION"].AsInt(), ignored);
				}
				if (attributes.HasKey("NORMAL")) {
					normals = ResolveAccessor(context, attributes["NORMAL"].AsInt(), ignored);
				}
				if (!positions.valid && !normals.valid) {
					continue;
				}

				const bool hasNormals = normals.valid;
				for (u32 v = 0; v < vertexCount; ++v) {
					const Vec3 position = (positions.valid && v < positions.count)
											  ? MirrorX(Vec3(ReadComponentAsFloat(positions, v, 0), ReadComponentAsFloat(positions, v, 1), ReadComponentAsFloat(positions, v, 2)))
											  : Vec3{};
					const Vec3 normal = (hasNormals && v < normals.count)
											? MirrorX(Vec3(ReadComponentAsFloat(normals, v, 0), ReadComponentAsFloat(normals, v, 1), ReadComponentAsFloat(normals, v, 2)))
											: Vec3{};
					if (position == Vec3{} && normal == Vec3{}) {
						continue;
					}
					shape.offsetVertices.push_back(baseVertex + v);
					shape.positionOffsets.push_back(position);
					if (hasNormals) {
						shape.normalOffsets.push_back(normal);
					}
				}
				if (!shape.normalOffsets.empty()) {
					shape.normalOffsets.resize(shape.positionOffsets.size());
				}
			}
		}

		bool EmitPrimitive(const ParseContext& context, const JSONValue& primitive, const JSONValue& targetNames, ModelMesh& mesh, str& error) {
			if (primitive["mode"].AsInt(4) != 4) {
				SLT_LOG_WARN("Graphics", "glTF: non-triangle primitive mode {} skipped", primitive["mode"].AsInt());
				return true;
			}
			const JSONValue& attributes = primitive["attributes"];
			i64 positionAccessor = attributes["POSITION"].AsInt(-1);
			if (positionAccessor < 0) {
				error = "primitive lacks POSITION";
				return false;
			}

			AccessorView positions = ResolveAccessor(context, positionAccessor, error);
			if (!positions.valid) {
				return false;
			}
			AccessorView normals{};
			if (attributes.HasKey("NORMAL")) {
				normals = ResolveAccessor(context, attributes["NORMAL"].AsInt(), error);
				if (!normals.valid) {
					return false;
				}
			}
			AccessorView tangents{};
			if (attributes.HasKey("TANGENT")) {
				tangents = ResolveAccessor(context, attributes["TANGENT"].AsInt(), error);
				if (!tangents.valid) {
					return false;
				}
			}
			AccessorView uvs{};
			if (attributes.HasKey("TEXCOORD_0")) {
				uvs = ResolveAccessor(context, attributes["TEXCOORD_0"].AsInt(), error);
				if (!uvs.valid) {
					return false;
				}
			}
			AccessorView colors{};
			if (attributes.HasKey("COLOR_0")) {
				colors = ResolveAccessor(context, attributes["COLOR_0"].AsInt(), error);
				if (!colors.valid) {
					return false;
				}
			}

			AccessorView joints{};
			AccessorView weights{};
			if (attributes.HasKey("JOINTS_0")) {
				joints = ResolveAccessor(context, attributes["JOINTS_0"].AsInt(), error);
				if (!joints.valid) {
					return false;
				}
			}
			if (attributes.HasKey("WEIGHTS_0")) {
				weights = ResolveAccessor(context, attributes["WEIGHTS_0"].AsInt(), error);
				if (!weights.valid) {
					return false;
				}
			}
			if (joints.valid != weights.valid) {
				SLT_LOG_WARN("Graphics", "glTF: mesh '{}' has {} without {} (primitive imports unskinned)", mesh.name,
							 joints.valid ? "JOINTS_0" : "WEIGHTS_0", joints.valid ? "WEIGHTS_0" : "JOINTS_0");
				joints.valid = false;
				weights.valid = false;
			}
			if (attributes.HasKey("JOINTS_1")) {
				SLT_LOG_WARN("Graphics", "glTF: mesh '{}' has > 4 influences per vertex (only the 1st 4 are imported)", mesh.name);
			}

			u32 baseVertex = static_cast<u32>(mesh.vertices.size());
			u32 firstIndex = static_cast<u32>(mesh.indices.size());

			AABB primitiveBounds = AABB::Empty();

			for (u32 i = 0; i < positions.count; ++i) {
				MeshVertex vertex;
				vertex.position = Vec3(
					ReadComponentAsFloat(positions, i, 0),
					ReadComponentAsFloat(positions, i, 1),
					ReadComponentAsFloat(positions, i, 2));

				if (normals.valid && i < normals.count) {
					vertex.normal = Vec3(
						ReadComponentAsFloat(normals, i, 0),
						ReadComponentAsFloat(normals, i, 1),
						ReadComponentAsFloat(normals, i, 2));
				}
				if (tangents.valid && i < tangents.count) {
					vertex.tangent = Vec3(
						ReadComponentAsFloat(tangents, i, 0),
						ReadComponentAsFloat(tangents, i, 1),
						ReadComponentAsFloat(tangents, i, 2));
					vertex.tangentSign = ReadComponentAsFloat(tangents, i, 3);
				}
				if (uvs.valid && i < uvs.count) {
					vertex.u = ReadComponentAsFloat(uvs, i, 0);
					vertex.v = ReadComponentAsFloat(uvs, i, 1);
				}
				if (colors.valid && i < colors.count) {
					Color color(
						ReadComponentAsFloat(colors, i, 0),
						ReadComponentAsFloat(colors, i, 1),
						ReadComponentAsFloat(colors, i, 2),
						colors.components == 4 ? ReadComponentAsFloat(colors, i, 3) : 1.0f);
					vertex.color = color.ToColor32();
				}

				vertex.position = MirrorX(vertex.position);
				vertex.normal = MirrorX(vertex.normal);
				vertex.tangent = MirrorX(vertex.tangent);
				vertex.tangentSign = -vertex.tangentSign;

				mesh.bounds.Encapsulate(vertex.position);
				primitiveBounds.Encapsulate(vertex.position);
				mesh.vertices.push_back(vertex);
			}

			if (joints.valid && weights.valid) {
				mesh.influences.resize(baseVertex);
				for (u32 i = 0; i < positions.count; ++i) {
					ModelSkinInfluence influence;
					if (i < joints.count && i < weights.count) {
						const u32 lanes = std::min<u32>(4, std::min(joints.components, weights.components));
						for (u32 lane = 0; lane < lanes; ++lane) {
							influence.joints[lane] = static_cast<u16>(ReadComponentAsUint(joints, i, lane));
							influence.weights[lane] = ReadComponentAsFloat(weights, i, lane);
						}
					}
					NormalizeInfluence(influence);
					mesh.influences.push_back(influence);
				}
			}
			if (!mesh.influences.empty()) {
				mesh.influences.resize(mesh.vertices.size());
			}

			// after vertices, before indices (targets use baseVertex)
			EmitPrimitiveTargets(context, primitive, targetNames, baseVertex, positions.count, mesh);

			if (primitive.HasKey("indices")) {
				AccessorView indexView = ResolveAccessor(context, primitive["indices"].AsInt(), error);
				if (!indexView.valid) {
					return false;
				}
				for (u32 i = 0; i < indexView.count; ++i) {
					mesh.indices.push_back(baseVertex + ReadComponentAsUint(indexView, i, 0));
				}
			} else {
				for (u32 i = 0; i < positions.count; ++i) {
					mesh.indices.push_back(baseVertex + i);
				}
			}

			// mirroring flips facing -> reverse winding
			for (size_t i = firstIndex; i + 2 < mesh.indices.size(); i += 3) {
				std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
			}

			if (!normals.valid) {
				for (size_t i = firstIndex; i + 2 < mesh.indices.size(); i += 3) {
					MeshVertex& a = mesh.vertices[mesh.indices[i]];
					MeshVertex& b = mesh.vertices[mesh.indices[i + 1]];
					MeshVertex& c = mesh.vertices[mesh.indices[i + 2]];
					Vec3 normal = Vec3::Cross(b.position - a.position, c.position - a.position).Normalized();
					a.normal = normal;
					b.normal = normal;
					c.normal = normal;
				}
			}

			SubMesh submesh;
			submesh.firstIndex = firstIndex;
			submesh.indexCount = static_cast<u32>(mesh.indices.size()) - firstIndex;
			submesh.bounds = primitiveBounds;

			i64 materialIndex = primitive["material"].AsInt(-1);
			submesh.materialSlot = MaterialSlotFor(mesh, materialIndex >= 0 ? static_cast<i32>(materialIndex) : -1);
			mesh.submeshes.push_back(submesh);
			return true;
		}

		Vec4 MirrorTrackValue(ModelChannel channel, const Vec4& value) {
			switch (channel) {
			case ModelChannel::Translation:
				return Vec4(-value.x, value.y, value.z, 0.0f);
			case ModelChannel::Rotation:
				return Vec4(value.x, -value.y, -value.z, value.w);
			case ModelChannel::MorphWeight:
				return Vec4(value.x, 0.0f, 0.0f, 0.0f);
			case ModelChannel::Scale:
			default:
				return Vec4(value.x, value.y, value.z, 0.0f);
			}
		}

		bool ChannelFromPath(std::string_view path, ModelChannel& out) {
			if (path == "translation") {
				out = ModelChannel::Translation;
				return true;
			}
			if (path == "rotation") {
				out = ModelChannel::Rotation;
				return true;
			}
			if (path == "scale") {
				out = ModelChannel::Scale;
				return true;
			}
			if (path == "weights") {
				out = ModelChannel::MorphWeight;
				return true;
			}
			return false;
		}

		ModelInterpolation InterpolationFromName(std::string_view name) {
			if (name == "STEP") {
				return ModelInterpolation::Step;
			}
			if (name == "CUBICSPLINE") {
				return ModelInterpolation::CubicSpline;
			}
			return ModelInterpolation::Linear;
		}

		void EmitAnimations(const ParseContext& context, std::span<const i32> fileNodeToSlot, ModelParseResult& result) {
			const JSONValue& animations = (*context.root)["animations"];
			size_t droppedChannels = 0;
			size_t unreachableTargets = 0;
			for (size_t a = 0; a < animations.Size(); ++a) {
				const JSONValue& animation = animations[a];
				const JSONValue& samplers = animation["samplers"];
				const JSONValue& channels = animation["channels"];

				ModelTakeRange take;
				take.name = str(animation["name"].AsString());
				if (take.name.empty()) {
					take.name = std::format("animation{}", a);
				}
				bool timed = false;

				for (size_t c = 0; c < channels.Size(); ++c) {
					const JSONValue& channel = channels[c];
					const JSONValue& target = channel["target"];

					ModelChannel kind;
					if (!ChannelFromPath(target["path"].AsString(), kind)) {
						++droppedChannels;
						continue;
					}
					i64 targetNode = target["node"].AsInt(-1);
					i32 slot = -1;
					if (targetNode >= 0 && static_cast<size_t>(targetNode) < fileNodeToSlot.size()) {
						slot = fileNodeToSlot[static_cast<size_t>(targetNode)];
					}
					if (slot < 0) {
						++unreachableTargets;
						continue;
					}

					i64 samplerIndex = channel["sampler"].AsInt(-1);
					if (samplerIndex < 0 || static_cast<size_t>(samplerIndex) >= samplers.Size()) {
						++droppedChannels;
						continue;
					}
					const JSONValue& sampler = samplers[static_cast<size_t>(samplerIndex)];

					str error;
					AccessorView times = ResolveAccessor(context, sampler["input"].AsInt(-1), error);
					AccessorView values = ResolveAccessor(context, sampler["output"].AsInt(-1), error);
					if (!times.valid || !values.valid) {
						SLT_LOG_WARN("Graphics", "glTF: animation '{}' channel {} is unreadable ({}). Dropped", take.name, c, error);
						++droppedChannels;
						continue;
					}

					const ModelInterpolation interp = InterpolationFromName(sampler["interpolation"].AsString("LINEAR"));
					const u32 perKey = ModelValuesPerKey(interp);

					u32 curveCount = 1;
					if (kind == ModelChannel::MorphWeight) {
						const i32 meshIndex = result.nodes[static_cast<size_t>(slot)].mesh;
						const size_t channelCount = (meshIndex >= 0 && static_cast<size_t>(meshIndex) < result.meshes.size())
														? result.meshes[static_cast<size_t>(meshIndex)].blendChannels.size()
														: 0;
						if (channelCount == 0) {
							++droppedChannels;
							continue;
						}
						curveCount = static_cast<u32>(channelCount);
					}

					const u32 available = values.count / (perKey * curveCount);
					const u32 keys = times.count < available ? times.count : available;
					if (keys == 0) {
						++droppedChannels;
						continue;
					}

					for (u32 curve = 0; curve < curveCount; ++curve) {
						ModelTrack track;
						track.node = slot;
						track.channel = kind;
						track.interp = interp;
						track.blendChannel = kind == ModelChannel::MorphWeight ? static_cast<i32>(curve) : -1;
						track.times.reserve(keys);
						track.values.reserve(static_cast<size_t>(keys) * perKey);
						for (u32 k = 0; k < keys; ++k) {
							track.times.push_back(ReadComponentAsFloat(times, k, 0));
							for (u32 v = 0; v < perKey; ++v) {
								const u32 element = (k * perKey + v) * curveCount + curve;
								Vec4 value(ReadComponentAsFloat(values, element, 0),
										   values.components > 1 ? ReadComponentAsFloat(values, element, 1) : 0.0f,
										   values.components > 2 ? ReadComponentAsFloat(values, element, 2) : 0.0f,
										   values.components > 3 ? ReadComponentAsFloat(values, element, 3) : 0.0f);
								if (kind == ModelChannel::MorphWeight) {
									// glTF weights 0-1, ours 0-100
									value.x *= 100.0f;
								}
								track.values.push_back(MirrorTrackValue(kind, value));
							}
						}

						const f32 first = track.times.front();
						const f32 last = track.times.back();
						take.begin = timed ? std::min(take.begin, first) : first;
						take.end = timed ? std::max(take.end, last) : last;
						timed = true;

						take.tracks.push_back(static_cast<i32>(result.tracks.size()));
						result.tracks.push_back(std::move(track));
					}
				}

				result.takes.push_back(std::move(take));
			}

			if (unreachableTargets > 0) {
				SLT_LOG_WARN("Graphics", "glTF: {} animation channel(s) drive a node the imported scene doesn't reach (dropped)", unreachableTargets);
			}
			if (droppedChannels > 0) {
				SLT_LOG_WARN("Graphics", "glTF: {} animation channel(s) target a property this build doesn't animate, or a sampler it can't read (dropped)", droppedChannels);
			}
		}

		void WarnUnsupported(const JSONValue& root) {
			const char* unsupported[] = {"cameras", "extensionsRequired"};
			for (const char* key : unsupported) {
				if (root.HasKey(key) && root[key].Size() > 0) {
					SLT_LOG_WARN("Graphics", "glTF: '{}' present but out of the v1 import scope. Ignored", key);
				}
			}
		}

	} // anonymous namespace

	ModelParseResult ParseGLTF(const Path& sourcePhysicalPath, const ModelParseOptions& parseOptions) {
		ModelParseResult result;

		Buffer file = FileSystem::ReadFile(sourcePhysicalPath, MemTag::Asset);
		if (!file || file.size == 0) {
			result.error = "file unreadable or empty";
			return result;
		}

		std::string_view jsonText;
		Vector<u8> binChunk{GetMemoryResource(MemTag::Asset)};

		constexpr u32 GLB_MAGIC = 0x46546C67;
		if (file.size >= 12 && std::memcmp(file.data, &GLB_MAGIC, 4) == 0) {
			u32 version = 0;
			u32 totalLength = 0;
			std::memcpy(&version, file.data + 4, 4);
			std::memcpy(&totalLength, file.data + 8, 4);
			if (version != 2 || totalLength > file.size) {
				result.error = std::format("GLB header invalid (version {}, length {} of {})", version, totalLength, file.size);
				return result;
			}

			u64 cursor = 12;
			while (cursor + 8 <= totalLength) {
				u32 chunkLength = 0;
				u32 chunkType = 0;
				std::memcpy(&chunkLength, file.data + cursor, 4);
				std::memcpy(&chunkType, file.data + cursor + 4, 4);
				cursor += 8;
				if (cursor + chunkLength > totalLength) {
					result.error = "GLB chunk overruns the file (truncated?)";
					return result;
				}
				constexpr u32 CHUNK_JSON = 0x4E4F534A;
				constexpr u32 CHUNK_BIN = 0x004E4942;
				if (chunkType == CHUNK_JSON) {
					jsonText = std::string_view(reinterpret_cast<const char*>(file.data + cursor), chunkLength);
				} else if (chunkType == CHUNK_BIN) {
					binChunk.assign(file.data + cursor, file.data + cursor + chunkLength);
				}
				cursor += chunkLength;
			}
			if (jsonText.empty()) {
				result.error = "GLB has no JSON chunk";
				return result;
			}
		} else {
			jsonText = std::string_view(reinterpret_cast<const char*>(file.data), file.size);
		}

		LinearAllocator arena(jsonText.size() + 1024, MemTag::Asset);
		JSONValue root = JSONParse(jsonText, arena);
		if (!root.IsObject()) {
			result.error = "glTF JSON malformed";
			return result;
		}
		WarnUnsupported(root);

		str baseDir = std::filesystem::path(sourcePhysicalPath.ToString()).parent_path().generic_string();

		Vector<Vector<u8>> buffers{GetMemoryResource(MemTag::Asset)};
		const JSONValue& bufferArray = root["buffers"];
		for (size_t i = 0; i < bufferArray.Size(); ++i) {
			const JSONValue& buffer = bufferArray[i];
			Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
			std::string_view uri = buffer["uri"].AsString();
			if (uri.empty()) {
				payload = std::move(binChunk);
			} else if (uri.starts_with("data:")) {
				size_t comma = uri.find(',');
				if (comma == std::string_view::npos || !DecodeBase64(uri.substr(comma + 1), payload)) {
					result.error = std::format("buffer {} data: URI malformed", i);
					return result;
				}
			} else {
				Buffer external = FileSystem::ReadFile(Path(baseDir) / str(uri), MemTag::Asset);
				if (!external) {
					result.error = std::format("buffer file '{}' missing", str(uri));
					return result;
				}
				payload.assign(external.data, external.data + external.size);
			}

			u64 declared = static_cast<u64>(buffer["byteLength"].AsInt(0));
			if (payload.size() < declared) {
				result.error = std::format("buffer {} shorter than declared ({} < {})", i, payload.size(), declared);
				return result;
			}
			buffers.push_back(std::move(payload));
		}

		ParseContext context;
		context.root = &root;
		context.buffers = &buffers;
		context.baseDir = baseDir;

		const JSONValue& imageArray = root["images"];
		for (size_t i = 0; i < imageArray.Size(); ++i) {
			const JSONValue& image = imageArray[i];
			ModelImage entry;
			entry.name = SanitizeName(image["name"].AsString(), "image", static_cast<u32>(i));

			std::string_view mime = image["mimeType"].AsString();
			entry.extension = mime == "image/jpeg" ? ".jpg" : ".png";

			std::string_view uri = image["uri"].AsString();
			if (!uri.empty() && !uri.starts_with("data:")) {
				entry.externalPath = (Path(baseDir) / str(uri)).ToString();
				str ext = StringUtil::ToLower(std::filesystem::path(str(uri)).extension().string());
				if (!ext.empty()) {
					entry.extension = ext;
				}
			} else if (!uri.empty()) {
				size_t comma = uri.find(',');
				if (comma == std::string_view::npos || !DecodeBase64(uri.substr(comma + 1), entry.embeddedBytes)) {
					result.error = std::format("image {} data: URI malformed", i);
					return result;
				}
			} else if (image.HasKey("bufferView")) {
				i64 viewIndex = image["bufferView"].AsInt();
				const JSONValue& bufferViews = root["bufferViews"];
				if (viewIndex < 0 || static_cast<size_t>(viewIndex) >= bufferViews.Size()) {
					result.error = std::format("image {} bufferView out of range", i);
					return result;
				}
				const JSONValue& view = bufferViews[static_cast<size_t>(viewIndex)];
				i64 bufferIndex = view["buffer"].AsInt();
				u64 offset = static_cast<u64>(view["byteOffset"].AsInt(0));
				u64 length = static_cast<u64>(view["byteLength"].AsInt(0));
				if (bufferIndex < 0 || static_cast<size_t>(bufferIndex) >= buffers.size() ||
					offset + length > buffers[static_cast<size_t>(bufferIndex)].size()) {
					result.error = std::format("image {} bufferView overruns its buffer", i);
					return result;
				}
				const Vector<u8>& buffer = buffers[static_cast<size_t>(bufferIndex)];
				entry.embeddedBytes.assign(buffer.data() + offset, buffer.data() + offset + length);
			}
			result.images.push_back(std::move(entry));
		}

		const JSONValue& textureArray = root["textures"];
		auto imageOf = [&](const JSONValue& textureInfo) -> i32 {
			if (!textureInfo.IsObject()) {
				return -1;
			}
			const i64 textureIndex = textureInfo["index"].AsInt(-1);
			if (textureIndex < 0 || static_cast<size_t>(textureIndex) >= textureArray.Size()) {
				return -1;
			}
			const i64 source = textureArray[static_cast<size_t>(textureIndex)]["source"].AsInt(-1);
			if (source < 0 || static_cast<size_t>(source) >= result.images.size()) {
				return -1;
			}
			return static_cast<i32>(source);
		};

		const JSONValue& materialArray = root["materials"];
		for (size_t i = 0; i < materialArray.Size(); ++i) {
			const JSONValue& material = materialArray[i];
			ModelMaterial entry;
			entry.name = SanitizeName(material["name"].AsString(), "material", static_cast<u32>(i));

			const JSONValue& pbr = material["pbrMetallicRoughness"];
			if (pbr.IsObject()) {
				if (pbr.HasKey("baseColorFactor")) {
					const JSONValue& factor = pbr["baseColorFactor"];
					entry.baseColorFactor = Color(
						static_cast<f32>(factor[size_t{0}].AsNumber(1.0)),
						static_cast<f32>(factor[size_t{1}].AsNumber(1.0)),
						static_cast<f32>(factor[size_t{2}].AsNumber(1.0)),
						static_cast<f32>(factor[size_t{3}].AsNumber(1.0)));
				}
				entry.metallicFactor = static_cast<f32>(pbr["metallicFactor"].AsNumber(1.0));
				entry.roughnessFactor = static_cast<f32>(pbr["roughnessFactor"].AsNumber(1.0));
				entry.baseColorImage = imageOf(pbr["baseColorTexture"]);
				entry.metallicRoughnessImage = imageOf(pbr["metallicRoughnessTexture"]);
			}

			const JSONValue& normalInfo = material["normalTexture"];
			entry.normalImage = imageOf(normalInfo);
			entry.normalScale = static_cast<f32>(normalInfo["scale"].AsNumber(1.0));

			const JSONValue& occlusionInfo = material["occlusionTexture"];
			entry.occlusionImage = imageOf(occlusionInfo);
			entry.occlusionStrength = static_cast<f32>(occlusionInfo["strength"].AsNumber(1.0));

			entry.emissiveImage = imageOf(material["emissiveTexture"]);
			if (material.HasKey("emissiveFactor")) {
				const JSONValue& emissive = material["emissiveFactor"];
				entry.emissiveFactor = Vec3(
					static_cast<f32>(emissive[size_t{0}].AsNumber(0.0)),
					static_cast<f32>(emissive[size_t{1}].AsNumber(0.0)),
					static_cast<f32>(emissive[size_t{2}].AsNumber(0.0)));
			}

			result.materials.push_back(std::move(entry));
		}

		const JSONValue& meshes = root["meshes"];
		const JSONValue& nodes = root["nodes"];

		Vector<str> meshNames{GetMemoryResource(MemTag::Asset)};
		for (size_t i = 0; i < meshes.Size(); ++i) {
			ModelMesh entry;
			entry.name = UniquifyName(SanitizeName(meshes[i]["name"].AsString(), "mesh", static_cast<u32>(i)), meshNames);
			const JSONValue& primitives = meshes[i]["primitives"];
			const JSONValue& targetNames = meshes[i]["extras"]["targetNames"];
			for (size_t p = 0; p < primitives.Size(); ++p) {
				if (!EmitPrimitive(context, primitives[p], targetNames, entry, result.error)) {
					return result;
				}
			}
			result.meshes.push_back(std::move(entry));
		}

		struct PendingNode {
			i64 index;
			i32 parentSlot;
		};
		Vector<u8> visited{GetMemoryResource(MemTag::Asset)};
		visited.resize(nodes.Size(), 0);
		Vector<i32> fileNodeToSlot{GetMemoryResource(MemTag::Asset)};
		fileNodeToSlot.resize(nodes.Size(), -1);
		Vector<PendingNode> pending{GetMemoryResource(MemTag::Asset)};
		auto visitNode = [&](i64 nodeIndex, i32 parentSlot) -> void {
			if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= nodes.Size() || visited[static_cast<size_t>(nodeIndex)]) {
				return;
			}
			visited[static_cast<size_t>(nodeIndex)] = 1;
			const JSONValue& node = nodes[static_cast<size_t>(nodeIndex)];

			ModelNode entry;
			entry.name = str(node["name"].AsString());
			entry.parent = parentSlot;
			entry.localMatrix = MirrorXConjugate(NodeLocalMatrix(node));
			if (!Mat4::DecomposeTRS(entry.localMatrix, entry.position, entry.rotation, entry.scale) && node.HasKey("matrix")) {
				entry.shearedMatrix = true;
				SLT_LOG_WARN("Graphics", "glTF: node '{}' has a matrix a SceneObject can't represent (shear or a collapsed axis)", entry.name);
			}
			if (node.HasKey("mesh")) {
				i64 meshIndex = node["mesh"].AsInt(-1);
				if (meshIndex >= 0 && static_cast<size_t>(meshIndex) < result.meshes.size()) {
					entry.mesh = static_cast<i32>(meshIndex);
				}
			}
			entry.skin = static_cast<i32>(node["skin"].AsInt(-1));

			i32 slot = static_cast<i32>(result.nodes.size());
			fileNodeToSlot[static_cast<size_t>(nodeIndex)] = slot;
			result.nodes.push_back(std::move(entry));

			const JSONValue& children = node["children"];
			for (size_t i = children.Size(); i-- > 0;) {
				pending.push_back({children[i].AsInt(), slot});
			}
		};

		i64 sceneIndex = root["scene"].AsInt(0);
		const JSONValue& scenes = root["scenes"];
		if (scenes.Size() > 0 && static_cast<size_t>(sceneIndex) < scenes.Size()) {
			const JSONValue& sceneNodes = scenes[static_cast<size_t>(sceneIndex)]["nodes"];
			for (size_t i = sceneNodes.Size(); i-- > 0;) {
				pending.push_back({sceneNodes[i].AsInt(), -1});
			}
			while (!pending.empty()) {
				const PendingNode current = pending.back();
				pending.pop_back();
				visitNode(current.index, current.parentSlot);
			}
		} else {
			for (size_t i = 0; i < result.meshes.size(); ++i) {
				ModelNode entry;
				entry.name = result.meshes[i].name;
				entry.mesh = static_cast<i32>(i);
				result.nodes.push_back(std::move(entry));
			}
		}
		const JSONValue& skinArray = root["skins"];
		for (size_t i = 0; i < skinArray.Size(); ++i) {
			const JSONValue& skin = skinArray[i];
			const JSONValue& jointArray = skin["joints"];

			AccessorView inverseBind{};
			if (skin.HasKey("inverseBindMatrices")) {
				inverseBind = ResolveAccessor(context, skin["inverseBindMatrices"].AsInt(), result.error);
				if (!inverseBind.valid) {
					return result;
				}
			}

			ModelSkin entry;
			entry.joints.reserve(jointArray.Size());
			entry.inverseBind.reserve(jointArray.Size());
			size_t unresolved = 0;
			for (size_t j = 0; j < jointArray.Size(); ++j) {
				i64 jointNode = jointArray[j].AsInt(-1);
				i32 slot = -1;
				if (jointNode >= 0 && static_cast<size_t>(jointNode) < fileNodeToSlot.size()) {
					slot = fileNodeToSlot[static_cast<size_t>(jointNode)];
				}
				if (slot < 0) {
					++unresolved;
				}
				entry.joints.push_back(slot);

				Mat4 matrix = Mat4::Identity();
				if (inverseBind.valid && j < inverseBind.count) {
					for (u32 e = 0; e < 16; ++e) {
						matrix.data[e] = ReadComponentAsFloat(inverseBind, static_cast<u32>(j), e);
					}
				}
				entry.inverseBind.push_back(MirrorXConjugate(matrix));
			}
			if (unresolved > 0) {
				SLT_LOG_WARN("Graphics", "glTF: skin {} uses {} joint node(s) the imported scene doesn't reach (influences bind to nothing)", i, unresolved);
			}
			result.skins.push_back(std::move(entry));
		}
		for (ModelNode& node : result.nodes) {
			if (node.skin >= 0 && static_cast<size_t>(node.skin) >= result.skins.size()) {
				SLT_LOG_WARN("Graphics", "glTF: node '{}' binds skin {} (the file doesn't declare it)", node.name, node.skin);
				node.skin = -1;
			}
		}

		if (parseOptions.tracks) {
			EmitAnimations(context, std::span<const i32>(fileNodeToSlot.data(), fileNodeToSlot.size()), result);
		}

		if (result.nodes.empty() && result.meshes.empty()) {
			result.error = "the file has no nodes/geometry";
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
				SLT_LOG_WARN("Graphics", "glTF: this document has {} mesh(es), there's no nodes that reach one with triangles (importing the hierarchy only)", result.meshes.size());
			}
		}

		result.success = true;
		return result;
	}

} // namespace Shalltear
