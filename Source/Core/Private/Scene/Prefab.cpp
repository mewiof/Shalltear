#include "Shalltear/Core/Scene/Prefab.h"

#include "SceneDocument.h"
#include "SceneInternal.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <charconv>

namespace Shalltear {

	using namespace SceneInternal;
	namespace SI = SerializerInternal;

	u64 FoldRef(u64 seed, const Guid& file, u64 localId) {
		constexpr u64 PRIME = 0x00000100000001B3ULL;
		constexpr u64 OFFSET_BASIS = 0xCBF29CE484222325ULL;

		u64 hash = seed != 0 ? seed : OFFSET_BASIS;
		const u64 words[3] = {file.low, file.high, localId};
		for (u64 word : words) {
			for (u32 byte = 0; byte < 8; ++byte) {
				hash ^= (word >> (byte * 8)) & 0xFFu;
				hash *= PRIME;
			}
		}
		return hash != 0 ? hash : 1;
	}

	namespace SceneInternal {

		namespace {

			str HexOf(u64 value) {
				return std::format("{:016x}", value);
			}

			bool HexTo(std::string_view text, u64& out) {
				if (text.size() != 16) {
					return false;
				}
				auto result = std::from_chars(text.data(), text.data() + 16, out, 16);
				return result.ec == std::errc{} && result.ptr == text.data() + 16;
			}

			template <typename T>
			bool TryEncodeScalar(const Variant& value, JSONValue& out) {
				if (!value.Is<T>()) {
					return false;
				}
				out = JSONValue::MakeNumber(static_cast<f64>(value.As<T>()));
				return true;
			}

			template <typename T>
			bool TryEncodeArrayF(const Variant& value, JSONValue& out) {
				if (!value.Is<T>()) {
					return false;
				}
				const T& typed = value.As<T>();
				const f32* data = reinterpret_cast<const f32*>(&typed);
				out = JSONValue::MakeArray();
				for (size_t i = 0; i < sizeof(T) / sizeof(f32); ++i) {
					out.Push(JSONValue::MakeNumber(static_cast<f64>(data[i])));
				}
				return true;
			}

			template <typename T>
			bool TryEncodeArrayI(const Variant& value, JSONValue& out) {
				if (!value.Is<T>()) {
					return false;
				}
				const T& typed = value.As<T>();
				const i32* data = reinterpret_cast<const i32*>(&typed);
				out = JSONValue::MakeArray();
				for (size_t i = 0; i < sizeof(T) / sizeof(i32); ++i) {
					out.Push(JSONValue::MakeNumber(static_cast<f64>(data[i])));
				}
				return true;
			}

			bool EncodeVariantJSON(const Variant& value, JSONValue& out, std::deque<str>& pool) {
				if (value.Is<bool>()) {
					out = JSONValue::MakeBool(value.As<bool>());
					return true;
				}
				if (TryEncodeScalar<i8>(value, out) || TryEncodeScalar<i16>(value, out) ||
					TryEncodeScalar<i32>(value, out) || TryEncodeScalar<i64>(value, out) ||
					TryEncodeScalar<u8>(value, out) || TryEncodeScalar<u16>(value, out) ||
					TryEncodeScalar<u32>(value, out) || TryEncodeScalar<u64>(value, out) ||
					TryEncodeScalar<f32>(value, out) || TryEncodeScalar<f64>(value, out)) {
					return true;
				}
				if (value.Is<str>()) {
					pool.push_back(value.As<str>());
					out = JSONValue::MakeString(pool.back());
					return true;
				}
				if (value.Is<Guid>()) {
					pool.push_back(value.As<Guid>().ToString());
					out = JSONValue::MakeString(pool.back());
					return true;
				}
				if (TryEncodeArrayF<Vec2>(value, out) || TryEncodeArrayF<Vec3>(value, out) ||
					TryEncodeArrayF<Vec4>(value, out) || TryEncodeArrayF<Quat>(value, out) ||
					TryEncodeArrayF<Color>(value, out)) {
					return true;
				}
				if (TryEncodeArrayI<Vec2I>(value, out) || TryEncodeArrayI<Vec3I>(value, out) ||
					TryEncodeArrayI<Vec4I>(value, out) || TryEncodeArrayI<RectI>(value, out)) {
					return true;
				}
				if (value.Is<Color32>()) {
					const Color32& typed = value.As<Color32>();
					const u8* data = reinterpret_cast<const u8*>(&typed);
					out = JSONValue::MakeArray();
					for (size_t i = 0; i < 4; ++i) {
						out.Push(JSONValue::MakeNumber(static_cast<f64>(data[i])));
					}
					return true;
				}
				return false;
			}

			template <typename T>
			bool TryDecodeScalar(u64 typeHash, const JSONValue& in, const char* name, Variant& out) {
				if (typeHash != StringHash(name).GetValue()) {
					return false;
				}
				out = static_cast<T>(in.AsNumber());
				return true;
			}

			template <typename T, typename Comp>
			bool TryDecodeArray(u64 typeHash, const JSONValue& in, const char* name, Variant& out) {
				if (typeHash != StringHash(name).GetValue()) {
					return false;
				}
				constexpr size_t COUNT = sizeof(T) / sizeof(Comp);
				if (!in.IsArray() || in.Size() != COUNT) {
					return false;
				}
				T typed{};
				Comp* data = reinterpret_cast<Comp*>(&typed);
				for (size_t i = 0; i < COUNT; ++i) {
					data[i] = static_cast<Comp>(in[i].AsNumber());
				}
				out = typed;
				return true;
			}

			bool DecodeVariantJSON(u64 typeHash, const JSONValue& in, Variant& out) {
				if (typeHash == StringHash("bool").GetValue()) {
					out = in.AsBool();
					return true;
				}
				if (TryDecodeScalar<i8>(typeHash, in, "i8", out) || TryDecodeScalar<i16>(typeHash, in, "i16", out) ||
					TryDecodeScalar<i32>(typeHash, in, "i32", out) || TryDecodeScalar<i64>(typeHash, in, "i64", out) ||
					TryDecodeScalar<u8>(typeHash, in, "u8", out) || TryDecodeScalar<u16>(typeHash, in, "u16", out) ||
					TryDecodeScalar<u32>(typeHash, in, "u32", out) || TryDecodeScalar<u64>(typeHash, in, "u64", out) ||
					TryDecodeScalar<f32>(typeHash, in, "f32", out) || TryDecodeScalar<f64>(typeHash, in, "f64", out)) {
					return true;
				}
				if (typeHash == StringHash("str").GetValue()) {
					out = str(in.AsString());
					return true;
				}
				if (typeHash == StringHash("Guid").GetValue()) {
					out = Guid::FromString(in.AsString());
					return true;
				}
				if (TryDecodeArray<Vec2, f32>(typeHash, in, "Vec2", out) || TryDecodeArray<Vec3, f32>(typeHash, in, "Vec3", out) ||
					TryDecodeArray<Vec4, f32>(typeHash, in, "Vec4", out) || TryDecodeArray<Quat, f32>(typeHash, in, "Quat", out) ||
					TryDecodeArray<Color, f32>(typeHash, in, "Color", out)) {
					return true;
				}
				if (TryDecodeArray<Vec2I, i32>(typeHash, in, "Vec2I", out) || TryDecodeArray<Vec3I, i32>(typeHash, in, "Vec3I", out) ||
					TryDecodeArray<Vec4I, i32>(typeHash, in, "Vec4I", out) || TryDecodeArray<RectI, i32>(typeHash, in, "RectI", out)) {
					return true;
				}
				if (TryDecodeArray<Color32, u8>(typeHash, in, "Color32", out)) {
					return true;
				}
				return false;
			}

			template <typename T>
			bool TryEncodeBinary(const Variant& value, SI::BinaryWriter& writer) {
				if (!value.Is<T>()) {
					return false;
				}
				writer.WriteU64(value.GetTypeId().GetValue());
				writer.WriteU32(static_cast<u32>(sizeof(T)));
				writer.WriteBytes(&value.As<T>(), sizeof(T));
				return true;
			}

			bool EncodeVariantBinary(const Variant& value, SI::BinaryWriter& writer) {
				if (value.Is<str>()) {
					const str& text = value.As<str>();
					writer.WriteU64(value.GetTypeId().GetValue());
					writer.WriteU32(static_cast<u32>(text.size()));
					writer.WriteBytes(text.data(), text.size());
					return true;
				}
				return TryEncodeBinary<bool>(value, writer) || TryEncodeBinary<i8>(value, writer) ||
					   TryEncodeBinary<i16>(value, writer) || TryEncodeBinary<i32>(value, writer) ||
					   TryEncodeBinary<i64>(value, writer) || TryEncodeBinary<u8>(value, writer) ||
					   TryEncodeBinary<u16>(value, writer) || TryEncodeBinary<u32>(value, writer) ||
					   TryEncodeBinary<u64>(value, writer) || TryEncodeBinary<f32>(value, writer) ||
					   TryEncodeBinary<f64>(value, writer) || TryEncodeBinary<Guid>(value, writer) ||
					   TryEncodeBinary<Vec2>(value, writer) || TryEncodeBinary<Vec3>(value, writer) ||
					   TryEncodeBinary<Vec4>(value, writer) || TryEncodeBinary<Quat>(value, writer) ||
					   TryEncodeBinary<Color>(value, writer) || TryEncodeBinary<Color32>(value, writer) ||
					   TryEncodeBinary<Vec2I>(value, writer) || TryEncodeBinary<Vec3I>(value, writer) ||
					   TryEncodeBinary<Vec4I>(value, writer) || TryEncodeBinary<RectI>(value, writer);
			}

			template <typename T>
			bool TryDecodeBinary(u64 typeHash, const char* name, const u8* payload, u32 length, Variant& out) {
				if (typeHash != StringHash(name).GetValue() || length != sizeof(T)) {
					return false;
				}
				T typed{};
				std::memcpy(&typed, payload, sizeof(T));
				out = typed;
				return true;
			}

			bool DecodeVariantBinary(SI::BinaryReader& reader, Variant& out) {
				u64 typeHash = 0;
				u32 length = 0;
				if (!reader.ReadU64(typeHash) || !reader.ReadU32(length) || length > reader.Remaining()) {
					return false;
				}
				const u8* payload = reader.CurrentPtr();
				bool decoded =
					TryDecodeBinary<bool>(typeHash, "bool", payload, length, out) ||
					TryDecodeBinary<i8>(typeHash, "i8", payload, length, out) ||
					TryDecodeBinary<i16>(typeHash, "i16", payload, length, out) ||
					TryDecodeBinary<i32>(typeHash, "i32", payload, length, out) ||
					TryDecodeBinary<i64>(typeHash, "i64", payload, length, out) ||
					TryDecodeBinary<u8>(typeHash, "u8", payload, length, out) ||
					TryDecodeBinary<u16>(typeHash, "u16", payload, length, out) ||
					TryDecodeBinary<u32>(typeHash, "u32", payload, length, out) ||
					TryDecodeBinary<u64>(typeHash, "u64", payload, length, out) ||
					TryDecodeBinary<f32>(typeHash, "f32", payload, length, out) ||
					TryDecodeBinary<f64>(typeHash, "f64", payload, length, out) ||
					TryDecodeBinary<Guid>(typeHash, "Guid", payload, length, out) ||
					TryDecodeBinary<Vec2>(typeHash, "Vec2", payload, length, out) ||
					TryDecodeBinary<Vec3>(typeHash, "Vec3", payload, length, out) ||
					TryDecodeBinary<Vec4>(typeHash, "Vec4", payload, length, out) ||
					TryDecodeBinary<Quat>(typeHash, "Quat", payload, length, out) ||
					TryDecodeBinary<Color>(typeHash, "Color", payload, length, out) ||
					TryDecodeBinary<Color32>(typeHash, "Color32", payload, length, out) ||
					TryDecodeBinary<Vec2I>(typeHash, "Vec2I", payload, length, out) ||
					TryDecodeBinary<Vec3I>(typeHash, "Vec3I", payload, length, out) ||
					TryDecodeBinary<Vec4I>(typeHash, "Vec4I", payload, length, out) ||
					TryDecodeBinary<RectI>(typeHash, "RectI", payload, length, out);
				if (!decoded && typeHash == StringHash("str").GetValue()) {
					out = str(reinterpret_cast<const char*>(payload), length);
					decoded = true;
				}
				return reader.Skip(length) && decoded;
			}

			JSONValue WriteFileRefJSON(const PrefabFileRef& ref, std::deque<str>& pool) {
				JSONValue value = JSONValue::MakeObject();
				pool.push_back(ref.file.ToString());
				value.AddMember("file", JSONValue::MakeString(pool.back()));
				value.AddMember("id", JSONValue::MakeNumber(static_cast<f64>(ref.localId)));
				return value;
			}

			bool ParseFileRefJSON(const JSONValue& value, PrefabFileRef& out) {
				if (!value.IsObject()) {
					return false;
				}
				out.file = Guid::FromString(value["file"].AsString());
				out.localId = static_cast<u64>(value["id"].AsNumber());
				return out.localId != 0;
			}

			JSONValue WritePathJSON(const PrefabTargetPath& path, std::deque<str>& pool) {
				JSONValue value = JSONValue::MakeObject();
				if (!path.hops.empty()) {
					JSONValue hops = JSONValue::MakeArray();
					for (const PrefabFileRef& hop : path.hops) {
						hops.Push(WriteFileRefJSON(hop, pool));
					}
					value.AddMember("hops", std::move(hops));
				}
				pool.push_back(path.leaf.file.ToString());
				value.AddMember("file", JSONValue::MakeString(pool.back()));
				value.AddMember("id", JSONValue::MakeNumber(static_cast<f64>(path.leaf.localId)));
				return value;
			}

			bool ParsePathJSON(const JSONValue& value, PrefabTargetPath& out) {
				if (!value.IsObject()) {
					return false;
				}
				const JSONValue& hops = value["hops"];
				for (size_t i = 0; i < hops.Size(); ++i) {
					PrefabFileRef hop;
					if (!ParseFileRefJSON(hops[i], hop)) {
						return false;
					}
					out.hops.push_back(hop);
				}
				out.leaf.file = Guid::FromString(value["file"].AsString());
				out.leaf.localId = static_cast<u64>(value["id"].AsNumber());
				return out.leaf.localId != 0;
			}

			void WritePathBinary(SI::BinaryWriter& writer, const PrefabTargetPath& path) {
				writer.WriteU8(static_cast<u8>(path.hops.size()));
				for (const PrefabFileRef& hop : path.hops) {
					writer.WriteU64(hop.file.low);
					writer.WriteU64(hop.file.high);
					writer.WriteU64(hop.localId);
				}
				writer.WriteU64(path.leaf.file.low);
				writer.WriteU64(path.leaf.file.high);
				writer.WriteU64(path.leaf.localId);
			}

			bool ParsePathBinary(SI::BinaryReader& reader, PrefabTargetPath& out) {
				u8 hopCount = 0;
				if (!reader.ReadU8(hopCount)) {
					return false;
				}
				for (u8 i = 0; i < hopCount; ++i) {
					PrefabFileRef hop;
					if (!reader.ReadU64(hop.file.low) || !reader.ReadU64(hop.file.high) || !reader.ReadU64(hop.localId)) {
						return false;
					}
					out.hops.push_back(hop);
				}
				return reader.ReadU64(out.leaf.file.low) && reader.ReadU64(out.leaf.file.high) && reader.ReadU64(out.leaf.localId);
			}

		} // anonymous namespace

		JSONValue WriteOverrideLayerJSON(const OverrideLayer& layer, SceneDocument& document) {
			std::deque<str>& pool = document.pool;
			JSONValue value = JSONValue::MakeObject();

			if (!layer.properties.empty()) {
				JSONValue properties = JSONValue::MakeArray();
				for (const PropertyOverrideOp& op : layer.properties) {
					JSONValue opValue = JSONValue::MakeObject();
					opValue.AddMember("target", WritePathJSON(op.target, pool));
					JSONValue fieldPath = JSONValue::MakeArray();
					for (StringHash segment : op.fieldPath) {
						pool.push_back(HexOf(segment.GetValue()));
						fieldPath.Push(JSONValue::MakeString(pool.back()));
					}
					opValue.AddMember("path", std::move(fieldPath));
					if (op.value.isHandle) {
						JSONValue handleValue = JSONValue::MakeObject();
						pool.push_back(op.value.handleGuid.ToString());
						handleValue.AddMember("guid", JSONValue::MakeString(pool.back()));
						pool.push_back(HexOf(op.value.handlePointee.GetValue()));
						handleValue.AddMember("pointee", JSONValue::MakeString(pool.back()));
						opValue.AddMember("handle", std::move(handleValue));
					} else {
						JSONValue encoded;
						if (!EncodeVariantJSON(op.value.direct, encoded, pool)) {
							SLT_LOG_WARN("Scene", "override value has an unsupported type. Op dropped");
							continue;
						}
						pool.push_back(HexOf(op.value.direct.GetTypeId().GetValue()));
						opValue.AddMember("t", JSONValue::MakeString(pool.back()));
						opValue.AddMember("v", std::move(encoded));
					}
					properties.Push(std::move(opValue));
				}
				value.AddMember("properties", std::move(properties));
			}

			if (!layer.addedObjects.empty()) {
				JSONValue added = JSONValue::MakeArray();
				for (const AddedObjectOp& op : layer.addedObjects) {
					JSONValue opValue = JSONValue::MakeObject();
					opValue.AddMember("parent", WritePathJSON(op.parent, pool));
					opValue.AddMember("index", JSONValue::MakeNumber(static_cast<f64>(op.insertIndex)));
					opValue.AddMember("root", JSONValue::MakeNumber(static_cast<f64>(op.rootRowId)));
					added.Push(std::move(opValue));
				}
				value.AddMember("addedObjects", std::move(added));
			}

			if (!layer.removedObjects.empty()) {
				JSONValue removed = JSONValue::MakeArray();
				for (const RemovedObjectOp& op : layer.removedObjects) {
					JSONValue opValue = JSONValue::MakeObject();
					opValue.AddMember("target", WritePathJSON(op.target, pool));
					removed.Push(std::move(opValue));
				}
				value.AddMember("removedObjects", std::move(removed));
			}

			if (!layer.addedComponents.empty()) {
				JSONValue added = JSONValue::MakeArray();
				for (const AddedComponentOp& op : layer.addedComponents) {
					const SceneDocumentComponentRow* componentRow = nullptr;
					for (const SceneDocumentComponentRow& loose : document.looseComponents) {
						if (loose.id == op.componentRowId) {
							componentRow = &loose;
							break;
						}
					}
					if (!componentRow) {
						SLT_LOG_WARN("Scene", "added-component op references missing row {}. Op dropped", op.componentRowId);
						continue;
					}
					JSONValue opValue = JSONValue::MakeObject();
					opValue.AddMember("owner", WritePathJSON(op.owner, pool));
					JSONValue rowValue = JSONValue::MakeObject();
					rowValue.AddMember("id", JSONValue::MakeNumber(static_cast<f64>(componentRow->id)));
					pool.push_back(HexOf(componentRow->type.GetValue()));
					rowValue.AddMember("type", JSONValue::MakeString(pool.back()));
					rowValue.AddMember("fields", componentRow->fields.json);
					opValue.AddMember("component", std::move(rowValue));
					added.Push(std::move(opValue));
				}
				value.AddMember("addedComponents", std::move(added));
			}

			if (!layer.removedComponents.empty()) {
				JSONValue removed = JSONValue::MakeArray();
				for (const RemovedComponentOp& op : layer.removedComponents) {
					JSONValue opValue = JSONValue::MakeObject();
					opValue.AddMember("target", WritePathJSON(op.target, pool));
					removed.Push(std::move(opValue));
				}
				value.AddMember("removedComponents", std::move(removed));
			}

			return value;
		}

		bool ParseOverrideLayerJSON(const JSONValue& json, OverrideLayer& out, SceneDocument& document) {
			if (json.IsNull()) {
				return true;
			}
			if (!json.IsObject()) {
				return false;
			}

			const JSONValue& properties = json["properties"];
			for (size_t i = 0; i < properties.Size(); ++i) {
				const JSONValue& opValue = properties[i];
				PropertyOverrideOp op;
				if (!ParsePathJSON(opValue["target"], op.target)) {
					SLT_LOG_WARN("Scene", "override layer: malformed property target skipped");
					continue;
				}
				const JSONValue& fieldPath = opValue["path"];
				bool pathOk = fieldPath.IsArray() && fieldPath.Size() > 0;
				for (size_t s = 0; pathOk && s < fieldPath.Size(); ++s) {
					u64 segment = 0;
					pathOk = HexTo(fieldPath[s].AsString(), segment);
					op.fieldPath.push_back(StringHash(segment));
				}
				if (!pathOk) {
					SLT_LOG_WARN("Scene", "override layer: malformed field path skipped");
					continue;
				}
				const JSONValue& handleValue = opValue["handle"];
				if (handleValue.IsObject()) {
					op.value.isHandle = true;
					op.value.handleGuid = Guid::FromString(handleValue["guid"].AsString());
					u64 pointee = 0;
					HexTo(handleValue["pointee"].AsString(), pointee);
					op.value.handlePointee = StringHash(pointee);
				} else {
					u64 typeHash = 0;
					if (!HexTo(opValue["t"].AsString(), typeHash) || !DecodeVariantJSON(typeHash, opValue["v"], op.value.direct)) {
						SLT_LOG_WARN("Scene", "override layer: undecodable value skipped");
						continue;
					}
				}
				out.properties.push_back(std::move(op));
			}

			const JSONValue& addedObjects = json["addedObjects"];
			for (size_t i = 0; i < addedObjects.Size(); ++i) {
				const JSONValue& opValue = addedObjects[i];
				AddedObjectOp op;
				if (!ParsePathJSON(opValue["parent"], op.parent)) {
					continue;
				}
				op.insertIndex = static_cast<u32>(opValue["index"].AsInt());
				op.rootRowId = static_cast<u64>(opValue["root"].AsNumber());
				if (op.rootRowId != 0) {
					out.addedObjects.push_back(std::move(op));
				}
			}

			const JSONValue& removedObjects = json["removedObjects"];
			for (size_t i = 0; i < removedObjects.Size(); ++i) {
				RemovedObjectOp op;
				if (ParsePathJSON(removedObjects[i]["target"], op.target)) {
					out.removedObjects.push_back(std::move(op));
				}
			}

			const JSONValue& addedComponents = json["addedComponents"];
			for (size_t i = 0; i < addedComponents.Size(); ++i) {
				const JSONValue& opValue = addedComponents[i];
				AddedComponentOp op;
				if (!ParsePathJSON(opValue["owner"], op.owner)) {
					continue;
				}
				const JSONValue& rowValue = opValue["component"];
				SceneDocumentComponentRow componentRow;
				u64 typeHash = 0;
				componentRow.id = static_cast<u64>(rowValue["id"].AsNumber());
				if (componentRow.id == 0 || !HexTo(rowValue["type"].AsString(), typeHash)) {
					continue;
				}
				componentRow.type = StringHash(typeHash);
				componentRow.fields.json = rowValue["fields"];
				op.componentRowId = componentRow.id;
				document.looseComponents.push_back(std::move(componentRow));
				out.addedComponents.push_back(std::move(op));
			}

			const JSONValue& removedComponents = json["removedComponents"];
			for (size_t i = 0; i < removedComponents.Size(); ++i) {
				RemovedComponentOp op;
				if (ParsePathJSON(removedComponents[i]["target"], op.target)) {
					out.removedComponents.push_back(std::move(op));
				}
			}
			return true;
		}

		bool WriteOverrideLayerBinary(SI::BinaryWriter& writer, const OverrideLayer& layer, const SceneDocument& document) {
			size_t blockPatch = writer.BeginPatch();

			writer.WriteU32(static_cast<u32>(layer.properties.size()));
			for (const PropertyOverrideOp& op : layer.properties) {
				WritePathBinary(writer, op.target);
				writer.WriteU8(static_cast<u8>(op.fieldPath.size()));
				for (StringHash segment : op.fieldPath) {
					writer.WriteU64(segment.GetValue());
				}
				writer.WriteU8(op.value.isHandle ? 1 : 0);
				if (op.value.isHandle) {
					writer.WriteU64(op.value.handleGuid.low);
					writer.WriteU64(op.value.handleGuid.high);
					writer.WriteU64(op.value.handlePointee.GetValue());
				} else if (!EncodeVariantBinary(op.value.direct, writer)) {
					writer.WriteU64(StringHash("str").GetValue());
					writer.WriteU32(0);
				}
			}

			writer.WriteU32(static_cast<u32>(layer.addedObjects.size()));
			for (const AddedObjectOp& op : layer.addedObjects) {
				WritePathBinary(writer, op.parent);
				writer.WriteU32(op.insertIndex);
				writer.WriteU64(op.rootRowId);
			}

			writer.WriteU32(static_cast<u32>(layer.removedObjects.size()));
			for (const RemovedObjectOp& op : layer.removedObjects) {
				WritePathBinary(writer, op.target);
			}

			std::vector<u8> scratch;
			std::span<const u8> record;

			writer.WriteU32(static_cast<u32>(layer.addedComponents.size()));
			for (const AddedComponentOp& op : layer.addedComponents) {
				WritePathBinary(writer, op.owner);
				const SceneDocumentComponentRow* componentRow = nullptr;
				for (const SceneDocumentComponentRow& loose : document.looseComponents) {
					if (loose.id == op.componentRowId) {
						componentRow = &loose;
						break;
					}
				}
				writer.WriteU64(op.componentRowId);
				writer.WriteU64(componentRow ? componentRow->type.GetValue() : 0);
				if (!componentRow) {
					writer.WriteU32(0);
					continue;
				}
				if (!EncodeBlobBinary(document, componentRow->fields, componentRow->type, BlobRow::Component,
									  scratch, record)) {
					return false;
				}
				writer.WriteU32(static_cast<u32>(record.size()));
				writer.WriteBytes(record.data(), record.size());
			}

			writer.WriteU32(static_cast<u32>(layer.removedComponents.size()));
			for (const RemovedComponentOp& op : layer.removedComponents) {
				WritePathBinary(writer, op.target);
			}

			writer.EndPatch(blockPatch);
			return true;
		}

		bool ParseOverrideLayerBinary(SI::BinaryReader& reader, OverrideLayer& out, SceneDocument& document) {
			u32 blockLength = 0;
			if (!reader.ReadU32(blockLength) || blockLength > reader.Remaining()) {
				return false;
			}
			size_t blockStart = reader.Tell();

			bool ok = true;
			u32 propertyCount = 0;
			ok = ok && reader.ReadU32(propertyCount);
			for (u32 i = 0; ok && i < propertyCount; ++i) {
				PropertyOverrideOp op;
				ok = ParsePathBinary(reader, op.target);
				u8 segmentCount = 0;
				ok = ok && reader.ReadU8(segmentCount);
				for (u8 s = 0; ok && s < segmentCount; ++s) {
					u64 segment = 0;
					ok = reader.ReadU64(segment);
					op.fieldPath.push_back(StringHash(segment));
				}
				u8 isHandle = 0;
				ok = ok && reader.ReadU8(isHandle);
				if (ok && isHandle) {
					op.value.isHandle = true;
					u64 pointee = 0;
					ok = reader.ReadU64(op.value.handleGuid.low) && reader.ReadU64(op.value.handleGuid.high) && reader.ReadU64(pointee);
					op.value.handlePointee = StringHash(pointee);
				} else if (ok) {
					ok = DecodeVariantBinary(reader, op.value.direct);
				}
				if (ok) {
					out.properties.push_back(std::move(op));
				}
			}

			u32 addedObjectCount = 0;
			ok = ok && reader.ReadU32(addedObjectCount);
			for (u32 i = 0; ok && i < addedObjectCount; ++i) {
				AddedObjectOp op;
				ok = ParsePathBinary(reader, op.parent) && reader.ReadU32(op.insertIndex) && reader.ReadU64(op.rootRowId);
				if (ok && op.rootRowId != 0) {
					out.addedObjects.push_back(std::move(op));
				}
			}

			u32 removedObjectCount = 0;
			ok = ok && reader.ReadU32(removedObjectCount);
			for (u32 i = 0; ok && i < removedObjectCount; ++i) {
				RemovedObjectOp op;
				ok = ParsePathBinary(reader, op.target);
				if (ok) {
					out.removedObjects.push_back(std::move(op));
				}
			}

			u32 addedComponentCount = 0;
			ok = ok && reader.ReadU32(addedComponentCount);
			for (u32 i = 0; ok && i < addedComponentCount; ++i) {
				AddedComponentOp op;
				SceneDocumentComponentRow componentRow;
				u64 typeHash = 0;
				u32 blobSize = 0;
				ok = ParsePathBinary(reader, op.owner) && reader.ReadU64(componentRow.id) && reader.ReadU64(typeHash) && reader.ReadU32(blobSize);
				ok = ok && blobSize <= reader.Remaining();
				if (ok) {
					componentRow.type = StringHash(typeHash);
					componentRow.fields.binary.assign(reader.CurrentPtr(), reader.CurrentPtr() + blobSize);
					NormalizeOpaqueBlob(componentRow.fields, true);
					ok = reader.Skip(blobSize);
				}
				if (ok && componentRow.id != 0) {
					op.componentRowId = componentRow.id;
					document.looseComponents.push_back(std::move(componentRow));
					out.addedComponents.push_back(std::move(op));
				}
			}

			u32 removedComponentCount = 0;
			ok = ok && reader.ReadU32(removedComponentCount);
			for (u32 i = 0; ok && i < removedComponentCount; ++i) {
				RemovedComponentOp op;
				ok = ParsePathBinary(reader, op.target);
				if (ok) {
					out.removedComponents.push_back(std::move(op));
				}
			}

			return reader.Seek(blockStart + blockLength) && ok;
		}

	} // namespace SceneInternal

	PrefabAsset::PrefabAsset() = default;

	PrefabAsset::~PrefabAsset() {
		if (_fragment) {
			Delete(_fragment);
			_fragment = nullptr;
		}
	}

	Guid PrefabAsset::GetBasePrefab() const {
		return _fragment ? _fragment->basePrefab : Guid{};
	}

	const OverrideLayer& PrefabAsset::GetOverrides() const {
		static const OverrideLayer _empty{{}, {}, {}, {}, {}};
		return _fragment ? _fragment->prefabOverrides : _empty;
	}

	bool PrefabAsset::Import(const Path& sourcePath) {
		Buffer bytes = FileSystem::ReadFile(sourcePath, MemTag::Scene);
		if (!bytes) {
			SLT_LOG_ERROR("Scene", "PrefabAsset: can't read '{}'", sourcePath.ToString());
			return false;
		}
		SceneDocument* document = SceneInternal::ParseSceneDocument(std::move(bytes));
		if (!document) {
			SLT_LOG_ERROR("Scene", "PrefabAsset: malformed document '{}'", sourcePath.ToString());
			return false;
		}
		document->isPrefab = true;
		if (_fragment) {
			Delete(_fragment);
		}
		_fragment = document;
		return true;
	}

	bool PrefabAsset::Export(const Path& destPath) const {
		if (!_fragment) {
			return false;
		}
		if (_fragment->binary) {
			Buffer bytes = SceneInternal::WriteSceneDocumentBinary(*_fragment);
			return bytes && FileSystem::WriteFile(destPath, bytes);
		}
		str text = SceneInternal::WriteSceneDocumentText(*_fragment);
		return !text.empty() && FileSystem::WriteTextFile(destPath, text);
	}

	bool PrefabAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		(void)descriptor;
		return SceneInternal::CookDocumentAsset(_fragment, virtualPath, payload);
	}

	bool PrefabAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		(void)descriptor;
		SceneDocument* document = SceneInternal::LoadCookedDocument(payload, virtualPath);
		if (!document) {
			return false;
		}
		document->isPrefab = true;
		if (_fragment) {
			Delete(_fragment);
		}
		_fragment = document;
		return true;
	}

	bool PrefabAsset::CookImportState(CookWriter& importState) const {
		(void)importState;
		return false;
	}

	void PrefabAsset::OnLoad() {
		if (!_fragment || !assetId.IsValid()) {
			return;
		}
		auto canonicalizeRef = [this](PrefabFileRef& ref) {
			if (!ref.file.IsValid()) {
				ref.file = assetId;
			}
		};
		auto canonicalizePath = [&](PrefabTargetPath& path) {
			for (PrefabFileRef& hop : path.hops) {
				canonicalizeRef(hop);
			}
			canonicalizeRef(path.leaf);
		};
		auto canonicalizeLayer = [&](OverrideLayer& layer) {
			for (PropertyOverrideOp& op : layer.properties) {
				canonicalizePath(op.target);
			}
			for (AddedObjectOp& op : layer.addedObjects) {
				canonicalizePath(op.parent);
			}
			for (RemovedObjectOp& op : layer.removedObjects) {
				canonicalizePath(op.target);
			}
			for (AddedComponentOp& op : layer.addedComponents) {
				canonicalizePath(op.owner);
			}
			for (RemovedComponentOp& op : layer.removedComponents) {
				canonicalizePath(op.target);
			}
		};
		canonicalizeLayer(_fragment->prefabOverrides);
		for (SceneDocumentRow& row : _fragment->rows) {
			if (row.kind == SceneDocumentRow::Kind::Instance) {
				canonicalizeLayer(row.overrides);
			}
		}
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::PrefabAsset)
