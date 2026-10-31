#include "Shalltear/Core/UI/VectorAsset.h"

#include "Serialization/XML.h"
#include "UI/SvgParser.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>

namespace Shalltear {

	VectorAsset::~VectorAsset() = default;

	Vec2 VectorAsset::FitInto(f32 sizePx) const {
		if (viewSize.x <= 0.0f || viewSize.y <= 0.0f || sizePx <= 0.0f) {
			return {};
		}
		const f32 scale = sizePx / std::max(viewSize.x, viewSize.y);
		return {viewSize.x * scale, viewSize.y * scale};
	}

	bool VectorAsset::ParseSvg(std::string_view svgText, str& error) {
		LinearAllocator arena(svgText.size() + 64, MemTag::Asset);
		XML::XMLError xmlError;
		XML::XMLNode root = XML::Parse(svgText, arena, xmlError);
		if (xmlError.Failed()) {
			error = xmlError.Describe();
			return false;
		}

		VectorAsset parsed;
		Svg::Reader reader;
		if (!reader.Build(root, parsed, error)) {
			return false;
		}

		viewSize = parsed.viewSize;
		verbs = std::move(parsed.verbs);
		points = std::move(parsed.points);
		shapes = std::move(parsed.shapes);
		masks = std::move(parsed.masks);
		maskShapes = std::move(parsed.maskShapes);
		maskVerbs = std::move(parsed.maskVerbs);
		maskPoints = std::move(parsed.maskPoints);
		slotDefaults = std::move(parsed.slotDefaults);
		return true;
	}

	bool VectorAsset::Import(const Path& sourcePath) {
		Buffer file = FileSystem::ReadFile(sourcePath, MemTag::Asset);
		if (!file || file.size == 0) {
			SLT_LOG_ERROR("UI", "VectorAsset: '{}' unreadable", sourcePath.ToString());
			return false;
		}
		str error;
		std::string_view text(reinterpret_cast<const char*>(file.data), file.size);
		if (!ParseSvg(text, error)) {
			SLT_LOG_ERROR("UI", "VectorAsset: '{}' rejected: {}", sourcePath.ToString(), error);
			return false;
		}
		return true;
	}

	namespace {

		struct VectorCookedHeader {
			u32 version = 1;
			f32 viewWidth = 0.0f;
			f32 viewHeight = 0.0f;
			u32 verbCount = 0;
			u32 pointCount = 0;
			u32 shapeCount = 0;
			u32 maskCount = 0;
			u32 maskShapeCount = 0;
			u32 maskVerbCount = 0;
			u32 maskPointCount = 0;
			u32 slotCount = 0;
		};

		constexpr u32 MAX_COOKED_ELEMENTS = 1u << 22;

	} // anonymous namespace

	bool VectorAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		VectorCookedHeader header;
		header.viewWidth = viewSize.x;
		header.viewHeight = viewSize.y;
		header.verbCount = static_cast<u32>(verbs.size());
		header.pointCount = static_cast<u32>(points.size());
		header.shapeCount = static_cast<u32>(shapes.size());
		header.maskCount = static_cast<u32>(masks.size());
		header.maskShapeCount = static_cast<u32>(maskShapes.size());
		header.maskVerbCount = static_cast<u32>(maskVerbs.size());
		header.maskPointCount = static_cast<u32>(maskPoints.size());
		header.slotCount = static_cast<u32>(slotDefaults.size());
		descriptor.Write(header);

		payload.WriteBytes(verbs.data(), verbs.size() * sizeof(VectorVerb));
		payload.AlignTo(4);
		payload.WriteBytes(points.data(), points.size() * sizeof(Vec2));
		payload.WriteBytes(shapes.data(), shapes.size() * sizeof(VectorShape));
		payload.WriteBytes(masks.data(), masks.size() * sizeof(VectorMask));
		payload.WriteBytes(maskShapes.data(), maskShapes.size() * sizeof(VectorMaskShape));
		payload.WriteBytes(maskVerbs.data(), maskVerbs.size() * sizeof(VectorVerb));
		payload.AlignTo(4);
		payload.WriteBytes(maskPoints.data(), maskPoints.size() * sizeof(Vec2));
		payload.WriteBytes(slotDefaults.data(), slotDefaults.size() * sizeof(Color));
		return true;
	}

	bool VectorAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		VectorCookedHeader header;
		if (!descriptor.Read(header) || header.version != 1) {
			return false;
		}
		const u32 counts[] = {header.verbCount, header.pointCount, header.shapeCount, header.maskCount,
							  header.maskShapeCount, header.maskVerbCount, header.maskPointCount};
		for (u32 count : counts) {
			if (count > MAX_COOKED_ELEMENTS) {
				return false;
			}
		}
		if (header.slotCount > UI::MAX_ICON_SLOTS) {
			return false;
		}

		viewSize = {header.viewWidth, header.viewHeight};
		if (!payload.ReadInto(verbs, header.verbCount)) {
			return false;
		}
		payload.Skip((4 - (header.verbCount % 4)) % 4);
		if (!payload.ReadInto(points, header.pointCount) ||
			!payload.ReadInto(shapes, header.shapeCount) ||
			!payload.ReadInto(masks, header.maskCount) ||
			!payload.ReadInto(maskShapes, header.maskShapeCount) ||
			!payload.ReadInto(maskVerbs, header.maskVerbCount)) {
			return false;
		}
		payload.Skip((4 - (header.maskVerbCount % 4)) % 4);
		if (!payload.ReadInto(maskPoints, header.maskPointCount) ||
			!payload.ReadInto(slotDefaults, header.slotCount)) {
			return false;
		}

		return ValidateRanges();
	}

	bool VectorAsset::ValidateRanges() const {
		auto fits = [](u32 first, u32 count, size_t total) {
			return static_cast<size_t>(first) + count <= total;
		};
		for (const VectorShape& shape : shapes) {
			if (!fits(shape.firstVerb, shape.verbCount, verbs.size()) ||
				!fits(shape.firstPoint, shape.pointCount, points.size()) ||
				shape.slot >= slotDefaults.size() ||
				(shape.mask != INVALID_VECTOR_INDEX && shape.mask >= masks.size())) {
				return false;
			}
		}
		for (const VectorMask& mask : masks) {
			if (!fits(mask.firstShape, mask.shapeCount, maskShapes.size())) {
				return false;
			}
		}
		for (const VectorMaskShape& shape : maskShapes) {
			if (!fits(shape.firstVerb, shape.verbCount, maskVerbs.size()) ||
				!fits(shape.firstPoint, shape.pointCount, maskPoints.size())) {
				return false;
			}
		}
		return true;
	}

	Asset* VectorAssetImporter::Import(const Path& sourcePath) {
		VectorAsset* asset = New<VectorAsset>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::VectorAsset)
