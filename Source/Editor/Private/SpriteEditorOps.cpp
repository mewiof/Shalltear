#include "Shalltear/Editor/SpriteEditorOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/TextureCodecs.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include "Shalltear/Editor/EditorAssetOps.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>

namespace Shalltear::SpriteEditorOps {

	namespace {

		bool CellHasInk(const u8* rgba, Vec2I imageSize, const RectI& cell) {
			for (i32 y = cell.Top(); y < cell.Bottom(); ++y) {
				const u8* row = rgba + (static_cast<size_t>(y) * imageSize.x) * 4;
				for (i32 x = cell.Left(); x < cell.Right(); ++x) {
					if (row[static_cast<size_t>(x) * 4 + 3] != 0) {
						return true;
					}
				}
			}
			return false;
		}

		void SliceGrid(Vec2I cell, Vec2I offset, Vec2I padding, Vec2I imageSize, const u8* rgba, Vector<RectI>& out) {
			if (cell.x <= 0 || cell.y <= 0) {
				return;
			}
			for (i32 y = std::max(0, offset.y); y + cell.y <= imageSize.y; y += cell.y + std::max(0, padding.y)) {
				for (i32 x = std::max(0, offset.x); x + cell.x <= imageSize.x; x += cell.x + std::max(0, padding.x)) {
					const RectI rect(x, y, cell.x, cell.y);
					if (!rgba || CellHasInk(rgba, imageSize, rect)) {
						out.push_back(rect);
					}
				}
			}
		}

		void SliceIslands(Vec2I imageSize, const u8* rgba, Vector<RectI>& out) {
			if (!rgba) {
				return;
			}
			const size_t count = static_cast<size_t>(imageSize.x) * imageSize.y;
			Vector<u8> seen{GetMemoryResource(MemTag::Editor)};
			seen.resize(count, 0);
			Vector<i32> stack{GetMemoryResource(MemTag::Editor)};
			for (i32 y = 0; y < imageSize.y; ++y) {
				for (i32 x = 0; x < imageSize.x; ++x) {
					const size_t start = static_cast<size_t>(y) * imageSize.x + x;
					if (seen[start] || rgba[start * 4 + 3] == 0) {
						continue;
					}
					i32 minX = x;
					i32 minY = y;
					i32 maxX = x;
					i32 maxY = y;
					seen[start] = 1;
					stack.push_back(static_cast<i32>(start));
					while (!stack.empty()) {
						const i32 at = stack.back();
						stack.pop_back();
						const i32 px = at % imageSize.x;
						const i32 py = at / imageSize.x;
						minX = std::min(minX, px);
						minY = std::min(minY, py);
						maxX = std::max(maxX, px);
						maxY = std::max(maxY, py);
						for (i32 dy = -1; dy <= 1; ++dy) {
							for (i32 dx = -1; dx <= 1; ++dx) {
								const i32 nx = px + dx;
								const i32 ny = py + dy;
								if (nx < 0 || ny < 0 || nx >= imageSize.x || ny >= imageSize.y) {
									continue;
								}
								const size_t next = static_cast<size_t>(ny) * imageSize.x + nx;
								if (!seen[next] && rgba[next * 4 + 3] != 0) {
									seen[next] = 1;
									stack.push_back(static_cast<i32>(next));
								}
							}
						}
					}
					out.push_back(RectI(minX, minY, maxX - minX + 1, maxY - minY + 1));
				}
			}

			for (bool merged = true; merged;) {
				merged = false;
				for (size_t i = 0; i < out.size() && !merged; ++i) {
					for (size_t j = i + 1; j < out.size(); ++j) {
						if (!out[i].Intersects(out[j])) {
							continue;
						}
						const i32 left = std::min(out[i].Left(), out[j].Left());
						const i32 top = std::min(out[i].Top(), out[j].Top());
						const i32 right = std::max(out[i].Right(), out[j].Right());
						const i32 bottom = std::max(out[i].Bottom(), out[j].Bottom());
						out[i] = RectI(left, top, right - left, bottom - top);
						out.erase(out.begin() + static_cast<ptrdiff_t>(j));
						merged = true;
						break;
					}
				}
			}
			std::sort(out.begin(), out.end(), [](const RectI& a, const RectI& b) {
				return a.Top() != b.Top() ? a.Top() < b.Top() : a.Left() < b.Left();
			});
		}

		i64 OverlapArea(const RectI& a, const RectI& b) {
			const i32 w = std::min(a.Right(), b.Right()) - std::max(a.Left(), b.Left());
			const i32 h = std::min(a.Bottom(), b.Bottom()) - std::max(a.Top(), b.Top());
			return w > 0 && h > 0 ? static_cast<i64>(w) * h : 0;
		}

		bool HasEdge(u8 edges, GrabEdge edge) { return (edges & static_cast<u8>(edge)) != 0; }

	} // anonymous namespace

	void Slice(const SliceDesc& desc, Vec2I imageSize, const u8* rgba, Vector<RectI>& out) {
		out.clear();
		if (imageSize.x <= 0 || imageSize.y <= 0) {
			return;
		}
		switch (desc.mode) {
		case SliceMode::CellSize:
			SliceGrid(desc.cell, desc.offset, desc.padding, imageSize, rgba, out);
			break;
		case SliceMode::CellCount: {
			if (desc.cell.x <= 0 || desc.cell.y <= 0) {
				return;
			}
			const Vec2I usable(imageSize.x - std::max(0, desc.offset.x) - std::max(0, desc.padding.x) * (desc.cell.x - 1),
							   imageSize.y - std::max(0, desc.offset.y) - std::max(0, desc.padding.y) * (desc.cell.y - 1));
			SliceGrid(Vec2I(usable.x / desc.cell.x, usable.y / desc.cell.y), desc.offset, desc.padding, imageSize, rgba, out);
			break;
		}
		case SliceMode::Automatic:
			SliceIslands(imageSize, rgba, out);
			break;
		}
	}

	str MintId() {
		const Guid guid = AssetDatabase::GenerateGuid();
		return std::format("{:016x}", guid.low ^ guid.high);
	}

	str UniqueName(std::span<const TextureSprite> sprites, std::string_view base) {
		auto taken = [&sprites](std::string_view candidate) {
			return std::any_of(sprites.begin(), sprites.end(), [candidate](const TextureSprite& sprite) { return sprite.name == candidate; });
		};
		for (u32 suffix = 0;; ++suffix) {
			str candidate = std::format("{}_{}", base, suffix);
			if (!taken(candidate)) {
				return candidate;
			}
		}
	}

	void MergeSlices(Vector<TextureSprite>& sprites, std::span<const RectI> rects, std::string_view baseName) {
		Vector<u8> claimed{GetMemoryResource(MemTag::Editor)};
		claimed.resize(sprites.size(), 0);
		Vector<TextureSprite> added{GetMemoryResource(MemTag::Editor)};
		for (const RectI& rect : rects) {
			i64 best = 0;
			size_t bestIndex = sprites.size();
			for (size_t i = 0; i < sprites.size(); ++i) {
				const i64 area = claimed[i] ? 0 : OverlapArea(sprites[i].rect, rect);
				if (area > best) {
					best = area;
					bestIndex = i;
				}
			}
			if (bestIndex < sprites.size()) {
				claimed[bestIndex] = 1;
				sprites[bestIndex].rect = rect;
				sprites[bestIndex].border = ClampBorder(sprites[bestIndex].border, rect.size);
				continue;
			}
			TextureSprite sprite;
			sprite.id = MintId();
			sprite.rect = rect;
			added.push_back(std::move(sprite));
		}
		for (TextureSprite& sprite : added) {
			sprite.name = UniqueName(sprites, baseName);
			sprites.push_back(std::move(sprite));
		}
	}

	bool DecodeTexels(const TextureAsset& texture, Vector<u8>& rgba) {
		rgba.clear();
		const Vec2I size = texture.size;
		if (size.x <= 0 || size.y <= 0 || texture.GetDimension() != RHI::TextureDimension::Texture2D) {
			return false;
		}
		const size_t bytes = static_cast<size_t>(size.x) * size.y * 4;
		if (texture.format == RHI::Format::RGBA8Unorm || texture.format == RHI::Format::RGBA8SRGB) {
			if (texture.pixels.size() < bytes) {
				return false;
			}
			rgba.assign(texture.pixels.begin(), texture.pixels.begin() + static_cast<ptrdiff_t>(bytes));
			return true;
		}
		const TextureCodec* codec = TextureCodecs::Find(texture.format);
		if (!codec || !codec->decode) {
			return false;
		}
		const size_t levelBytes = static_cast<size_t>(RHI::FormatLevelBytes(texture.format, size));
		if (texture.pixels.size() < levelBytes) {
			return false;
		}
		rgba.resize(bytes);
		if (!codec->decode(texture.format, {texture.pixels.data(), levelBytes}, size, {rgba.data(), rgba.size()})) {
			rgba.clear();
			return false;
		}
		return true;
	}

	bool ApplySprites(const Path& texturePath, std::span<const TextureSprite> sprites) {
		const Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(texturePath));
		const str text = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
		LinearAllocator arena(text.size() + 64, MemTag::Editor);
		const JSONValue existing = text.empty() ? JSONValue() : JSONParse(text, arena);

		std::deque<str> pool;
		JSONValue list = JSONValue::MakeArray();
		for (const TextureSprite& sprite : sprites) {
			JSONValue entry = JSONValue::MakeObject();
			if (!sprite.id.empty()) {
				entry.AddMember("id", JSONValue::MakeString(pool.emplace_back(sprite.id)));
			}
			entry.AddMember("name", JSONValue::MakeString(pool.emplace_back(sprite.name)));
			JSONValue rect = JSONValue::MakeArray();
			for (i32 value : {sprite.rect.position.x, sprite.rect.position.y, sprite.rect.size.x, sprite.rect.size.y}) {
				rect.Push(JSONValue::MakeNumber(value));
			}
			entry.AddMember("rect", std::move(rect));
			if (sprite.border != SpriteBorder{}) {
				JSONValue border = JSONValue::MakeArray();
				for (i32 value : {sprite.border.left, sprite.border.top, sprite.border.right, sprite.border.bottom}) {
					border.Push(JSONValue::MakeNumber(value));
				}
				entry.AddMember("border", std::move(border));
			}
			list.Push(std::move(entry));
		}

		JSONValue rebuilt = JSONValue::MakeObject();
		const JSONValue& importer = existing["importer"];
		const JSONValue* previous = importer.IsObject() ? importer.Find("settings") : nullptr;
		if (previous && previous->IsObject()) {
			for (size_t i = 0; i < previous->MemberCount(); ++i) {
				if (previous->KeyAt(i) != "sprites") {
					rebuilt.AddMember(previous->KeyAt(i), previous->ChildAt(i));
				}
			}
		}
		if (!sprites.empty()) {
			rebuilt.AddMember("sprites", std::move(list));
		}

		if (!AssetDatabase::WriteImporterSettings(texturePath, rebuilt)) {
			return false;
		}
		if (!EditorAssetOps::Reimport(texturePath)) {
			SLT_LOG_WARN("Editor", "'{}': sprites saved, the texture couldn't be re-imported. Close what uses it and re-import",
						 texturePath.ToString());
			return false;
		}
		return true;
	}

	f32 CanvasView::Scale() const {
		if (imageSize.x <= 0 || imageSize.y <= 0 || box.w <= 0.0f || box.h <= 0.0f) {
			return 1.0f;
		}
		return std::min(box.w / static_cast<f32>(imageSize.x), box.h / static_cast<f32>(imageSize.y)) * zoom;
	}

	Vec2 CanvasView::ToTexel(Vec2 point) const {
		const f32 scale = Scale();
		const Vec2 origin(box.x + box.w * 0.5f + pan.x - static_cast<f32>(imageSize.x) * scale * 0.5f,
						  box.y + box.h * 0.5f + pan.y - static_cast<f32>(imageSize.y) * scale * 0.5f);
		return {(point.x - origin.x) / scale, (point.y - origin.y) / scale};
	}

	Vec2 CanvasView::ToPoint(Vec2 texel) const {
		const f32 scale = Scale();
		const Vec2 origin(box.x + box.w * 0.5f + pan.x - static_cast<f32>(imageSize.x) * scale * 0.5f,
						  box.y + box.h * 0.5f + pan.y - static_cast<f32>(imageSize.y) * scale * 0.5f);
		return {origin.x + texel.x * scale, origin.y + texel.y * scale};
	}

	UI::Rect CanvasView::ToRect(const RectI& texels) const {
		const Vec2 a = ToPoint(Vec2(static_cast<f32>(texels.Left()), static_cast<f32>(texels.Top())));
		const Vec2 b = ToPoint(Vec2(static_cast<f32>(texels.Right()), static_cast<f32>(texels.Bottom())));
		return {a.x, a.y, b.x - a.x, b.y - a.y};
	}

	void ZoomAt(CanvasView& view, Vec2 point, f32 notches) {
		const Vec2 anchor = view.ToTexel(point);
		view.zoom = std::clamp(view.zoom * std::pow(1.2f, notches), 0.25f, 64.0f);
		const Vec2 moved = view.ToPoint(anchor);
		view.pan.x += point.x - moved.x;
		view.pan.y += point.y - moved.y;
	}

	Grab Pick(const CanvasView& view, std::span<const TextureSprite> sprites, i32 selected, Vec2 point, f32 handlePx) {
		if (selected >= 0 && static_cast<size_t>(selected) < sprites.size()) {
			const UI::Rect r = view.ToRect(sprites[static_cast<size_t>(selected)].rect);
			const bool alongX = point.x >= r.x - handlePx && point.x <= r.x + r.w + handlePx;
			const bool alongY = point.y >= r.y - handlePx && point.y <= r.y + r.h + handlePx;
			u8 edges = 0;
			if (alongY && std::abs(point.x - r.x) <= handlePx) {
				edges |= static_cast<u8>(GrabEdge::Left);
			} else if (alongY && std::abs(point.x - (r.x + r.w)) <= handlePx) {
				edges |= static_cast<u8>(GrabEdge::Right);
			}
			if (alongX && std::abs(point.y - r.y) <= handlePx) {
				edges |= static_cast<u8>(GrabEdge::Top);
			} else if (alongX && std::abs(point.y - (r.y + r.h)) <= handlePx) {
				edges |= static_cast<u8>(GrabEdge::Bottom);
			}
			if (edges != 0) {
				return {GrabKind::Resize, selected, edges};
			}
		}
		for (size_t i = sprites.size(); i-- > 0;) {
			if (view.ToRect(sprites[i].rect).Contains(point)) {
				return {GrabKind::Move, static_cast<i32>(i), 0};
			}
		}
		return {GrabKind::Create, -1, 0};
	}

	RectI Drag(const RectI& start, const Grab& grab, Vec2I delta, Vec2I imageSize) {
		if (grab.kind == GrabKind::Move) {
			const i32 x = std::clamp(start.Left() + delta.x, 0, std::max(0, imageSize.x - start.size.x));
			const i32 y = std::clamp(start.Top() + delta.y, 0, std::max(0, imageSize.y - start.size.y));
			return RectI(x, y, start.size.x, start.size.y);
		}
		if (grab.kind != GrabKind::Resize) {
			return start;
		}
		i32 left = start.Left();
		i32 top = start.Top();
		i32 right = start.Right();
		i32 bottom = start.Bottom();
		if (HasEdge(grab.edges, GrabEdge::Left)) {
			left = std::clamp(left + delta.x, 0, right - 1);
		}
		if (HasEdge(grab.edges, GrabEdge::Right)) {
			right = std::clamp(right + delta.x, left + 1, imageSize.x);
		}
		if (HasEdge(grab.edges, GrabEdge::Top)) {
			top = std::clamp(top + delta.y, 0, bottom - 1);
		}
		if (HasEdge(grab.edges, GrabEdge::Bottom)) {
			bottom = std::clamp(bottom + delta.y, top + 1, imageSize.y);
		}
		return RectI(left, top, right - left, bottom - top);
	}

	RectI SpanRect(Vec2 fromTexel, Vec2 toTexel, Vec2I imageSize) {
		const i32 left = std::clamp(static_cast<i32>(std::floor(std::min(fromTexel.x, toTexel.x))), 0, imageSize.x);
		const i32 top = std::clamp(static_cast<i32>(std::floor(std::min(fromTexel.y, toTexel.y))), 0, imageSize.y);
		const i32 right = std::clamp(static_cast<i32>(std::ceil(std::max(fromTexel.x, toTexel.x))), 0, imageSize.x);
		const i32 bottom = std::clamp(static_cast<i32>(std::ceil(std::max(fromTexel.y, toTexel.y))), 0, imageSize.y);
		return RectI(left, top, right - left, bottom - top);
	}

	SpriteBorder ClampBorder(const SpriteBorder& border, Vec2I rectSize) {
		SpriteBorder out;
		out.left = std::clamp(border.left, 0, std::max(0, rectSize.x));
		out.right = std::clamp(border.right, 0, std::max(0, rectSize.x - out.left));
		out.top = std::clamp(border.top, 0, std::max(0, rectSize.y));
		out.bottom = std::clamp(border.bottom, 0, std::max(0, rectSize.y - out.top));
		return out;
	}

} // namespace Shalltear::SpriteEditorOps
