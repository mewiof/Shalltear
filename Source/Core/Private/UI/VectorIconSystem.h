#pragma once

#include "UI/UIInternal.h"
#include "UI/VectorRasterizer.h"

#include "Shalltear/Core/UI/UIAtlas.h"
#include "Shalltear/Core/UI/VectorIcon.h"

#include <algorithm>
#include <cmath>

namespace Shalltear::UI {

	class VectorIconSystem final : public IVectorIconSystem {
	public:
		struct CreateInfo {
			IAtlasBackend* backend = nullptr;
			Vec2I atlasSize = {1024, 1024};
			i32 padding = 2;
			u64 retentionFrames = 120;
		};

		explicit VectorIconSystem(const CreateInfo& createInfo)
			: _config(createInfo) {
			SLT_CORE_ASSERT_MSG(_config.backend != nullptr, "VectorIconSystem requires an atlas backend");
			_atlasSize = {std::max(64, _config.atlasSize.x), std::max(64, _config.atlasSize.y)};
			ResetShelves();
		}
		~VectorIconSystem() override = default;
		VectorIconSystem(const VectorIconSystem&) = delete;
		VectorIconSystem& operator=(const VectorIconSystem&) = delete;

		Vec2 MeasureIcon(Handle<VectorAsset> asset, f32 sizePx) override {
			const VectorAsset* image = asset.Get();
			if (!image || image->IsEmpty()) {
				return {};
			}
			return image->FitInto(std::min(sizePx, MAX_ICON_PX));
		}

		bool GetIcon(const IconQuery& query, RasterizedIcon& out) override {
			out = RasterizedIcon{};
			const VectorAsset* image = query.asset.Get();
			if (!image || image->IsEmpty() || query.sizePx < 1.0f) {
				return false;
			}
			const f32 clamped = std::min(query.sizePx, MAX_ICON_PX);
			const u32 sizePx = static_cast<u32>(std::max<long>(1, std::lround(clamped)));

			const IconKey key{query.asset.GetInstanceId(), sizePx, HashPalette(query.palette)};
			auto it = _entries.find(key);
			if (it != _entries.end() && it->second.packed) {
				it->second.lastUsedFrame = _frame;
				Publish(it->second, out);
				return true;
			}

			IconEntry entry;
			if (!_rasterizer.Rasterize(*image, static_cast<f32>(sizePx), query.palette, entry.pixels, entry.size)) {
				return false;
			}
			++_stats.rasterizations;
			entry.lastUsedFrame = _frame;
			EnsureAtlas();

			if (!Upload(entry)) {
				bool recovered = false;
				if (_lastCompactFrame != _frame) {
					Compact();
					recovered = Upload(entry);
				}
				if (!recovered) {
					if (!_atlasExhaustedLogged) {
						_atlasExhaustedLogged = true;
						SLT_LOG_WARN("UI", "the icon atlas is full at {}x{} with {} live entries (more icons won't draw)",
									 _atlasSize.x, _atlasSize.y, _entries.size());
					}
					return false;
				}
			}

			auto [inserted, ok] = _entries.insert_or_assign(key, std::move(entry));
			_stats.entries = static_cast<u32>(_entries.size());
			Publish(inserted->second, out);
			return true;
		}

		void EndFrame() override {
			++_frame;
		}

		[[nodiscard]] Stats GetStats() const override { return _stats; }

		bool GetAtlasPage(AtlasPage& out) const override {
			if (!_atlas.IsValid()) {
				return false;
			}
			out.texture = _atlas;
			out.size = _atlasSize;
			out.format = AtlasFormat::RGBA8;
			out.entries = _stats.entries;
			out.full = _atlasExhaustedLogged;
			return true;
		}
	private:
		struct IconKey {
			u64 assetId = 0;
			u32 sizePx = 0;
			u64 paletteHash = 0;
			bool operator==(const IconKey&) const = default;
		};
		struct IconKeyHash {
			size_t operator()(const IconKey& key) const {
				u64 hash = key.assetId * 0x9E3779B97F4A7C15ull;
				hash ^= static_cast<u64>(key.sizePx) * 0x100000001B3ull;
				hash ^= key.paletteHash + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
				return static_cast<size_t>(hash);
			}
		};

		struct IconEntry {
			Vector<u8> pixels{GetMemoryResource(MemTag::UI)};
			Vec2I size{};
			Rect uv{};
			u64 lastUsedFrame = 0;
			bool packed = false;
		};

		void EnsureAtlas() {
			if (_atlasAttempted) {
				return;
			}
			_atlasAttempted = true;
			_atlas = _config.backend->CreateAtlas(_atlasSize, AtlasFormat::RGBA8);
			_stats.atlasBytes = _atlas.IsValid()
									? static_cast<u32>(_atlasSize.x) * static_cast<u32>(_atlasSize.y) * 4u
									: 0u;
		}

		void Publish(const IconEntry& entry, RasterizedIcon& out) const {
			out.atlas = _atlas;
			out.uv = entry.uv;
			out.sizePx = Vec2{static_cast<f32>(entry.size.x), static_cast<f32>(entry.size.y)};
		}

		[[nodiscard]] static u64 HashPalette(const IconPalette& palette) {
			u64 hash = 0xCBF29CE484222325ull ^ palette.overrideMask;
			for (u32 slot = 0; slot < MAX_ICON_SLOTS; ++slot) {
				if (!palette.Overrides(slot)) {
					continue;
				}
				const Color32 packed = palette.slots[slot].ToColor32();
				const u64 value = (static_cast<u64>(packed.r) << 24) | (static_cast<u64>(packed.g) << 16) |
								  (static_cast<u64>(packed.b) << 8) | static_cast<u64>(packed.a);
				hash = (hash ^ (value + slot)) * 0x100000001B3ull;
			}
			return hash;
		}

		bool PackAlloc(i32 width, i32 height, i32& outX, i32& outY) {
			const i32 padding = _config.padding;
			if (width + padding * 2 > _atlasSize.x) {
				return false;
			}
			if (_shelfX + width + padding > _atlasSize.x) {
				_shelfX = padding;
				_shelfY += _shelfHeight + padding;
				_shelfHeight = 0;
			}
			if (_shelfY + height + padding > _atlasSize.y) {
				return false;
			}
			outX = _shelfX;
			outY = _shelfY;
			_shelfX += width + padding;
			_shelfHeight = std::max(_shelfHeight, height);
			return true;
		}

		void ResetShelves() {
			_shelfX = _config.padding;
			_shelfY = _config.padding;
			_shelfHeight = 0;
		}

		bool Upload(IconEntry& entry) {
			i32 x = 0;
			i32 y = 0;
			if (!PackAlloc(entry.size.x, entry.size.y, x, y)) {
				entry.packed = false;
				return false;
			}
			if (_atlas.IsValid()) {
				_config.backend->UpdateAtlas(_atlas, RectI{x, y, entry.size.x, entry.size.y},
											 {entry.pixels.data(), entry.pixels.size()});
			}
			const f32 invW = 1.0f / static_cast<f32>(_atlasSize.x);
			const f32 invH = 1.0f / static_cast<f32>(_atlasSize.y);
			entry.uv = {static_cast<f32>(x) * invW, static_cast<f32>(y) * invH,
						static_cast<f32>(entry.size.x) * invW, static_cast<f32>(entry.size.y) * invH};
			entry.packed = true;
			return true;
		}

		void Compact() {
			++_stats.compactions;
			_lastCompactFrame = _frame;

			const u64 cutoff = _frame > _config.retentionFrames ? _frame - _config.retentionFrames : 0;
			for (auto it = _entries.begin(); it != _entries.end();) {
				if (it->second.lastUsedFrame < cutoff) {
					it = _entries.erase(it);
				} else {
					it->second.packed = false;
					++it;
				}
			}

			Vector<IconEntry*> order{GetMemoryResource(MemTag::UI)};
			order.reserve(_entries.size());
			for (auto& [key, entry] : _entries) {
				order.push_back(&entry);
			}
			std::sort(order.begin(), order.end(), [](const IconEntry* a, const IconEntry* b) {
				return a->size.y > b->size.y;
			});

			ResetShelves();
			for (IconEntry* entry : order) {
				Upload(*entry);
			}
			_stats.entries = static_cast<u32>(_entries.size());
		}

		CreateInfo _config;
		RHI::TextureHandle _atlas;
		Vec2I _atlasSize{};
		i32 _shelfX = 1;
		i32 _shelfY = 1;
		i32 _shelfHeight = 0;
		bool _atlasAttempted = false;
		bool _atlasExhaustedLogged = false;

		HashMap<IconKey, IconEntry, IconKeyHash> _entries{GetMemoryResource(MemTag::UI)};
		u64 _frame = 1;
		u64 _lastCompactFrame = 0;
		Stats _stats;

		VectorRasterizer _rasterizer;
	};

} // namespace Shalltear::UI
