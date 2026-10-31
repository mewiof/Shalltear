#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/VectorIcon.h"

#include <string_view>

namespace Shalltear {

	enum class VectorVerb : u8 {
		Move = 0,
		Line,
		Cubic,
		Close
	};

	inline constexpr u32 INVALID_VECTOR_INDEX = 0xFFFFFFFFu;

	struct VectorShape {
		u32 firstVerb = 0;
		u32 verbCount = 0;
		u32 firstPoint = 0;
		u32 pointCount = 0;
		u32 slot = 0;
		f32 opacity = 1.0f;
		u32 mask = INVALID_VECTOR_INDEX;
	};

	struct VectorMask {
		u32 firstShape = 0;
		u32 shapeCount = 0;
	};

	struct VectorMaskShape {
		u32 firstVerb = 0;
		u32 verbCount = 0;
		u32 firstPoint = 0;
		u32 pointCount = 0;
		Color color = Color::White();
	};

	class SHALLTEAR_CORE_API VectorAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(VectorAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		VectorAsset() = default;
		~VectorAsset() override;

		Vec2 viewSize{0.0f, 0.0f};

		Vector<VectorVerb> verbs{GetMemoryResource(MemTag::Asset)};
		Vector<Vec2> points{GetMemoryResource(MemTag::Asset)};
		Vector<VectorShape> shapes{GetMemoryResource(MemTag::Asset)};
		Vector<VectorMask> masks{GetMemoryResource(MemTag::Asset)};
		Vector<VectorMaskShape> maskShapes{GetMemoryResource(MemTag::Asset)};
		Vector<VectorVerb> maskVerbs{GetMemoryResource(MemTag::Asset)};
		Vector<Vec2> maskPoints{GetMemoryResource(MemTag::Asset)};

		Vector<Color> slotDefaults{GetMemoryResource(MemTag::Asset)};

		[[nodiscard]] Vec2 FitInto(f32 sizePx) const;

		[[nodiscard]] bool IsEmpty() const { return shapes.empty() || viewSize.x <= 0.0f || viewSize.y <= 0.0f; }

		bool Import(const Path& sourcePath) override;
		StringHash GetAssetTypeId() const override { return VectorAsset::GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		bool ParseSvg(std::string_view svgText, str& error);
	private:
		[[nodiscard]] bool ValidateRanges() const;
	};

	class SHALLTEAR_CORE_API VectorAssetImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("VectorAsset"); }
		Asset* Import(const Path& sourcePath) override;
	};

} // namespace Shalltear
