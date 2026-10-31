#pragma once

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Types.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>

namespace Shalltear {

	// 128 bytes, field order never changes
	struct ImportArtifactHeader {
		u8 magic[8];
		u16 versionMajor;
		u16 versionMinor;
		u32 headerSize;
		u64 key;
		u64 sourceSize;
		u64 sourceMtime;
		u64 typeNameHash;
		u64 paySize;
		u32 importerVersion;
		u32 recordVersion;
		u32 inputCount;
		u32 inputsSize;
		u32 outputCount;
		u32 outputsSize;
		u32 strsSize;
		u32 descSize;
		u8 codec;
		u8 flags;
		u16 reserved;
		u32 crc32c;
		u64 settingsHash;
		u32 impSize;
		u32 reserved2;
		u32 prodCount;
		u32 prodsSize;
		u64 prodBlobSize;
	};

	constexpr u8 IMPORT_ARTIFACT_MAGIC[8] = {'S', 'L', 'A', 'R', 0x0D, 0x0A, 0x1A, 0x0A};

	constexpr u16 IMPORT_ARTIFACT_VERSION_MAJOR = 1;
	constexpr u16 IMPORT_ARTIFACT_VERSION_MINOR = 0;
	constexpr u32 IMPORT_ARTIFACT_HEADER_SIZE = 128;

	constexpr u8 IMPORT_ARTIFACT_FLAG_HAS_MAIN = 1 << 0;

	constexpr u8 IMPORT_ARTIFACT_FLAGS_KNOWN = IMPORT_ARTIFACT_FLAG_HAS_MAIN;

	constexpr u32 IMPORT_ARTIFACT_ALIGN = 4;

	// packed (CookWriter appends without padding)
	constexpr u32 IMPORT_ARTIFACT_INPUT_STRIDE = sizeof(u32) + 3 * sizeof(u64);

	constexpr u32 IMPORT_ARTIFACT_OUTPUT_STRIDE = sizeof(u32);

	constexpr u32 IMPORT_ARTIFACT_PRODUCT_STRIDE = sizeof(u32) + sizeof(u64) + sizeof(u32) + sizeof(u64) + sizeof(u32);

	constexpr u32 IMPORT_ARTIFACT_INPUT_SIZE_OFFSET = sizeof(u32);
	constexpr u32 IMPORT_ARTIFACT_INPUT_MTIME_OFFSET = sizeof(u32) + sizeof(u64);

	static_assert(sizeof(ImportArtifactHeader) == IMPORT_ARTIFACT_HEADER_SIZE);
	static_assert(alignof(ImportArtifactHeader) == 8);
	static_assert(std::is_trivially_copyable_v<ImportArtifactHeader>);
	static_assert(offsetof(ImportArtifactHeader, magic) == 0);
	static_assert(offsetof(ImportArtifactHeader, versionMajor) == 8);
	static_assert(offsetof(ImportArtifactHeader, versionMinor) == 10);
	static_assert(offsetof(ImportArtifactHeader, headerSize) == 12);
	static_assert(offsetof(ImportArtifactHeader, key) == 16);
	static_assert(offsetof(ImportArtifactHeader, sourceSize) == 24);
	static_assert(offsetof(ImportArtifactHeader, sourceMtime) == 32);
	static_assert(offsetof(ImportArtifactHeader, typeNameHash) == 40);
	static_assert(offsetof(ImportArtifactHeader, paySize) == 48);
	static_assert(offsetof(ImportArtifactHeader, importerVersion) == 56);
	static_assert(offsetof(ImportArtifactHeader, recordVersion) == 60);
	static_assert(offsetof(ImportArtifactHeader, inputCount) == 64);
	static_assert(offsetof(ImportArtifactHeader, inputsSize) == 68);
	static_assert(offsetof(ImportArtifactHeader, outputCount) == 72);
	static_assert(offsetof(ImportArtifactHeader, outputsSize) == 76);
	static_assert(offsetof(ImportArtifactHeader, strsSize) == 80);
	static_assert(offsetof(ImportArtifactHeader, descSize) == 84);
	static_assert(offsetof(ImportArtifactHeader, codec) == 88);
	static_assert(offsetof(ImportArtifactHeader, flags) == 89);
	static_assert(offsetof(ImportArtifactHeader, reserved) == 90);
	static_assert(offsetof(ImportArtifactHeader, crc32c) == 92);
	static_assert(offsetof(ImportArtifactHeader, settingsHash) == 96);
	static_assert(offsetof(ImportArtifactHeader, impSize) == 104);
	static_assert(offsetof(ImportArtifactHeader, reserved2) == 108);
	static_assert(offsetof(ImportArtifactHeader, prodCount) == 112);
	static_assert(offsetof(ImportArtifactHeader, prodsSize) == 116);
	static_assert(offsetof(ImportArtifactHeader, prodBlobSize) == 120);
	static_assert(std::has_unique_object_representations_v<ImportArtifactHeader>);

	namespace ImportArtifactDetail {

		constexpr size_t AlignUp(size_t value) {
			return (value + IMPORT_ARTIFACT_ALIGN - 1) / IMPORT_ARTIFACT_ALIGN * IMPORT_ARTIFACT_ALIGN;
		}

		inline bool AddChecked(u64& total, u64 value) {
			if (value > UINT64_MAX - total) {
				return false;
			}
			total += value;
			return true;
		}

	} // namespace ImportArtifactDetail

	inline u64 ImportArtifactHashSource(const u8* data, size_t size) {
		if (!data || size == 0) {
			return PakContentHash(nullptr, 0);
		}
		return PakContentHash(data, size);
	}

	inline u64 ImportArtifactHashSettings(const JSONValue& settings) {
		str compact = JSONSerialize(settings, false);
		return PakContentHash(compact.data(), compact.size());
	}

	inline u64 ImportArtifactComputeKey(u64 sourceHash, u64 settingsHash, u32 importerVersion) {
		u64 hash = PakContentHash(&sourceHash, sizeof(sourceHash));
		hash = PakContentHash(&settingsHash, sizeof(settingsHash), hash);
		hash = PakContentHash(&importerVersion, sizeof(importerVersion), hash);
		return hash;
	}

	struct ImportArtifactWriteDesc {
		u64 key = 0;
		u64 sourceSize = 0;
		u64 sourceMtime = 0;
		u64 typeNameHash = 0;
		u64 settingsHash = 0;
		u32 importerVersion = 0;

		const Vector<str>* strings = nullptr;
		const u8* inputs = nullptr;
		size_t inputsSize = 0;
		u32 inputCount = 0;
		const u8* outputs = nullptr;
		size_t outputsSize = 0;
		u32 outputCount = 0;
		const u8* desc = nullptr;
		size_t descSize = 0;
		const u8* payload = nullptr;
		size_t payloadSize = 0;
		const u8* importState = nullptr;
		size_t importStateSize = 0;
		bool hasMain = true;
		const u8* products = nullptr;
		size_t productsSize = 0;
		u32 productCount = 0;
		const u8* productBlob = nullptr;
		size_t productBlobSize = 0;
	};

	inline void ImportArtifactWrite(Vector<u8>& out, const ImportArtifactWriteDesc& desc) {
		size_t base = out.size();
		out.resize(base + IMPORT_ARTIFACT_HEADER_SIZE, 0);

		bool hasInputs = desc.inputsSize > 0 && desc.inputs != nullptr;
		size_t inputsPadded = ImportArtifactDetail::AlignUp(hasInputs ? desc.inputsSize : 0);
		size_t at = out.size();
		out.resize(at + inputsPadded, 0);
		if (hasInputs) {
			std::memcpy(out.data() + at, desc.inputs, desc.inputsSize);
		}

		bool hasOutputs = desc.outputsSize > 0 && desc.outputs != nullptr;
		size_t outputsPadded = ImportArtifactDetail::AlignUp(hasOutputs ? desc.outputsSize : 0);
		at = out.size();
		out.resize(at + outputsPadded, 0);
		if (hasOutputs) {
			std::memcpy(out.data() + at, desc.outputs, desc.outputsSize);
		}

		Vector<u8> strsBlob{GetMemoryResource(MemTag::Asset)};
		if (desc.strings && !desc.strings->empty()) {
			PakWriteStringBlob(strsBlob, *desc.strings);
		}
		size_t strsPadded = ImportArtifactDetail::AlignUp(strsBlob.size());

		at = out.size();
		out.resize(at + strsPadded, 0);
		if (!strsBlob.empty()) {
			std::memcpy(out.data() + at, strsBlob.data(), strsBlob.size());
		}

		bool hasDesc = desc.descSize > 0 && desc.desc != nullptr;
		bool hasPayload = desc.payloadSize > 0 && desc.payload != nullptr;
		if (hasDesc) {
			at = out.size();
			out.resize(at + desc.descSize);
			std::memcpy(out.data() + at, desc.desc, desc.descSize);
		}
		if (hasPayload) {
			at = out.size();
			out.resize(at + desc.payloadSize);
			std::memcpy(out.data() + at, desc.payload, desc.payloadSize);
		}
		bool hasImportState = desc.importStateSize > 0 && desc.importState != nullptr;
		if (hasImportState) {
			at = out.size();
			out.resize(at + desc.importStateSize);
			std::memcpy(out.data() + at, desc.importState, desc.importStateSize);
		}

		bool hasProducts = desc.productCount > 0 && desc.products != nullptr && desc.productsSize > 0;
		size_t productsPadded = ImportArtifactDetail::AlignUp(hasProducts ? desc.productsSize : 0);
		at = out.size();
		out.resize(at + productsPadded, 0);
		if (hasProducts) {
			std::memcpy(out.data() + at, desc.products, desc.productsSize);
		}
		bool hasProductBlob = hasProducts && desc.productBlobSize > 0 && desc.productBlob != nullptr;
		if (hasProductBlob) {
			at = out.size();
			out.resize(at + desc.productBlobSize);
			std::memcpy(out.data() + at, desc.productBlob, desc.productBlobSize);
		}

		ImportArtifactHeader header{};
		std::memcpy(header.magic, IMPORT_ARTIFACT_MAGIC, sizeof(header.magic));
		header.versionMajor = IMPORT_ARTIFACT_VERSION_MAJOR;
		header.versionMinor = IMPORT_ARTIFACT_VERSION_MINOR;
		header.headerSize = IMPORT_ARTIFACT_HEADER_SIZE;
		header.key = desc.key;
		header.sourceSize = desc.sourceSize;
		header.sourceMtime = desc.sourceMtime;
		header.typeNameHash = desc.typeNameHash;
		header.paySize = hasPayload ? static_cast<u64>(desc.payloadSize) : 0;
		header.settingsHash = desc.settingsHash;
		header.importerVersion = desc.importerVersion;
		header.recordVersion = PAK_RECORD_VERSION;
		header.inputCount = hasInputs ? desc.inputCount : 0;
		header.inputsSize = static_cast<u32>(inputsPadded);
		header.outputCount = hasOutputs ? desc.outputCount : 0;
		header.outputsSize = static_cast<u32>(outputsPadded);
		header.strsSize = static_cast<u32>(strsPadded);
		header.descSize = hasDesc ? static_cast<u32>(desc.descSize) : 0;
		header.impSize = hasImportState ? static_cast<u32>(desc.importStateSize) : 0;
		header.codec = static_cast<u8>(PakCodec::None);
		header.flags = desc.hasMain ? IMPORT_ARTIFACT_FLAG_HAS_MAIN : 0;
		header.reserved = 0;
		header.reserved2 = 0;
		header.prodCount = hasProducts ? desc.productCount : 0;
		header.prodsSize = static_cast<u32>(productsPadded);
		header.prodBlobSize = hasProductBlob ? static_cast<u64>(desc.productBlobSize) : 0;

		size_t bodyAt = base + IMPORT_ARTIFACT_HEADER_SIZE;
		header.crc32c = PakCrc32c(out.data() + bodyAt, out.size() - bodyAt);

		std::memcpy(out.data() + base, &header, sizeof(header));
	}

	// view into caller's buffer (must outlive it)
	struct ImportArtifactView {
		ImportArtifactHeader header{};
		PakStringTable strings;
		const u8* inputs = nullptr;
		size_t inputsSize = 0;
		const u8* outputs = nullptr;
		size_t outputsSize = 0;
		const u8* desc = nullptr;
		size_t descSize = 0;
		const u8* payload = nullptr;
		size_t payloadSize = 0;
		const u8* importState = nullptr;
		size_t importStateSize = 0;
		const u8* products = nullptr;
		size_t productsSize = 0;
		const u8* productBlob = nullptr;
		size_t productBlobSize = 0;

		bool HasMain() const { return (header.flags & IMPORT_ARTIFACT_FLAG_HAS_MAIN) != 0; }

		CookReader Descriptor() const { return CookReader(desc, descSize, strings); }
		CookReader Payload() const { return CookReader(payload, payloadSize, strings); }
		CookReader Inputs() const { return CookReader(inputs, inputsSize, strings); }
		CookReader Outputs() const { return CookReader(outputs, outputsSize, strings); }
		CookReader ImportState() const { return CookReader(importState, importStateSize, strings); }
		CookReader Products() const { return CookReader(products, productsSize, strings); }
	};

	struct ImportArtifactProductView {
		std::string_view key;
		u64 typeNameHash = 0;
		const u8* desc = nullptr;
		size_t descSize = 0;
		const u8* payload = nullptr;
		size_t payloadSize = 0;
		const u8* importState = nullptr;
		size_t importStateSize = 0;
	};

	// validated in order, no offset trusted before CRC check
	inline bool ImportArtifactRead(const u8* data, size_t size, ImportArtifactView& out) {
		out = ImportArtifactView{};
		if (!data || size < IMPORT_ARTIFACT_HEADER_SIZE) {
			return false;
		}

		ImportArtifactHeader header{};
		std::memcpy(&header, data, sizeof(header));

		if (std::memcmp(header.magic, IMPORT_ARTIFACT_MAGIC, sizeof(header.magic)) != 0) {
			return false;
		}
		if (header.versionMajor != IMPORT_ARTIFACT_VERSION_MAJOR) {
			return false;
		}
		if (header.headerSize != IMPORT_ARTIFACT_HEADER_SIZE) {
			return false;
		}
		if (header.codec != static_cast<u8>(PakCodec::None)) {
			return false;
		}
		if ((header.flags & ~IMPORT_ARTIFACT_FLAGS_KNOWN) != 0) {
			return false;
		}
		if (header.inputsSize % IMPORT_ARTIFACT_ALIGN != 0 || header.outputsSize % IMPORT_ARTIFACT_ALIGN != 0 ||
			header.strsSize % IMPORT_ARTIFACT_ALIGN != 0 || header.prodsSize % IMPORT_ARTIFACT_ALIGN != 0) {
			return false;
		}
		if (static_cast<u64>(header.inputCount) * IMPORT_ARTIFACT_INPUT_STRIDE > header.inputsSize ||
			static_cast<u64>(header.outputCount) * IMPORT_ARTIFACT_OUTPUT_STRIDE > header.outputsSize ||
			static_cast<u64>(header.prodCount) * IMPORT_ARTIFACT_PRODUCT_STRIDE > header.prodsSize) {
			return false;
		}

		u64 total = IMPORT_ARTIFACT_HEADER_SIZE;
		using ImportArtifactDetail::AddChecked;
		if (!AddChecked(total, header.inputsSize) || !AddChecked(total, header.outputsSize) ||
			!AddChecked(total, header.strsSize) || !AddChecked(total, header.descSize) ||
			!AddChecked(total, header.paySize) || !AddChecked(total, header.impSize) ||
			!AddChecked(total, header.prodsSize) || !AddChecked(total, header.prodBlobSize)) {
			return false;
		}
		if (total != static_cast<u64>(size)) {
			return false;
		}

		if (PakCrc32c(data + IMPORT_ARTIFACT_HEADER_SIZE, size - IMPORT_ARTIFACT_HEADER_SIZE) != header.crc32c) {
			return false;
		}

		const u8* cursor = data + IMPORT_ARTIFACT_HEADER_SIZE;
		const u8* inputsAt = cursor;
		cursor += header.inputsSize;
		const u8* outputsAt = cursor;
		cursor += header.outputsSize;

		if (header.strsSize > 0) {
			if (!PakReadStringBlob(cursor, header.strsSize, out.strings)) {
				return false;
			}
			cursor += header.strsSize;
		}

		out.header = header;
		out.inputs = inputsAt;
		out.inputsSize = header.inputsSize;
		out.outputs = outputsAt;
		out.outputsSize = header.outputsSize;
		out.desc = cursor;
		out.descSize = header.descSize;
		cursor += header.descSize;
		out.payload = cursor;
		out.payloadSize = static_cast<size_t>(header.paySize);
		cursor += header.paySize;
		out.importState = cursor;
		out.importStateSize = header.impSize;
		cursor += header.impSize;
		out.products = cursor;
		out.productsSize = header.prodsSize;
		cursor += header.prodsSize;
		out.productBlob = cursor;
		out.productBlobSize = static_cast<size_t>(header.prodBlobSize);
		return true;
	}

	inline bool ImportArtifactFindProduct(const ImportArtifactView& view, std::string_view key, ImportArtifactProductView& out) {
		out = ImportArtifactProductView{};
		if (key.empty() || view.header.prodCount == 0) {
			return false;
		}

		CookReader table = view.Products();
		u64 at = 0;
		for (u32 i = 0; i < view.header.prodCount; ++i) {
			u32 keyIndex = 0;
			u64 typeNameHash = 0;
			u32 descSize = 0;
			u64 paySize = 0;
			u32 impSize = 0;
			if (!table.Read(keyIndex) || !table.Read(typeNameHash) || !table.Read(descSize) || !table.Read(paySize) ||
				!table.Read(impSize)) {
				return false;
			}

			u64 span = at;
			using ImportArtifactDetail::AddChecked;
			if (!AddChecked(span, descSize) || !AddChecked(span, paySize) || !AddChecked(span, impSize) ||
				span > view.productBlobSize) {
				return false;
			}

			if (view.strings.Get(keyIndex) == key) {
				out.key = view.strings.Get(keyIndex);
				out.typeNameHash = typeNameHash;
				out.desc = view.productBlob + at;
				out.descSize = descSize;
				out.payload = out.desc + descSize;
				out.payloadSize = static_cast<size_t>(paySize);
				out.importState = out.payload + paySize;
				out.importStateSize = impSize;
				return true;
			}
			at = span;
		}
		return false;
	}

	inline Path ImportArtifactDirectory() {
		Path projectCache = FileSystem::Resolve(Path("project://Library/Artifacts"));
		if (projectCache.ToString().find("://") == str::npos) {
			return projectCache;
		}
		return Path("Library/Artifacts");
	}

	inline Path ImportArtifactPath(const Guid& guid) {
		str name = guid.ToString();
		return ImportArtifactDirectory() / name.substr(0, 2) / std::format("{}.slart", name);
	}

	inline bool ImportArtifactIsDerivedData(std::string_view recordedPath) {
		size_t scheme = recordedPath.find("://");
		std::string_view rest = scheme == std::string_view::npos ? recordedPath : recordedPath.substr(scheme + 3);
		constexpr std::string_view Library = "Library/";
		if (rest.size() < Library.size()) {
			return false;
		}
		for (size_t i = 0; i < Library.size(); ++i) {
			char c = rest[i];
			if (c >= 'A' && c <= 'Z') {
				c = static_cast<char>(c - 'A' + 'a');
			}
			char want = Library[i];
			if (want >= 'A' && want <= 'Z') {
				want = static_cast<char>(want - 'A' + 'a');
			}
			if (c != want) {
				return false;
			}
		}
		return true;
	}

	constexpr u64 IMPORT_ARTIFACT_COST_THRESHOLD_US = 1000;

	inline bool ImportArtifactIsWorthWriting(u64 importMicros, u64 thresholdMicros) {
		return importMicros >= thresholdMicros;
	}

	constexpr u64 IMPORT_ARTIFACT_BUDGET_BYTES = 8ull * 1024 * 1024 * 1024;

	constexpr u64 IMPORT_ARTIFACT_USE_BUCKET_SECONDS = 3600;

	struct ImportArtifactEntry {
		Guid guid;
		Path path;
		u64 bytes = 0;
		i64 lastUse = 0;
	};

	struct ImportArtifactSweepResult {
		u32 orphans = 0;
		u32 evicted = 0;
		u64 bytesBefore = 0;
		u64 bytesAfter = 0;
	};

	inline void ImportArtifactMarkUsed(const Path& artifactPath) {
		namespace fs = std::filesystem;
		std::error_code ec;
		fs::path physical(artifactPath.ToString());
		fs::file_time_type recorded = fs::last_write_time(physical, ec);
		if (ec) {
			return;
		}
		fs::file_time_type now = fs::file_time_type::clock::now();
		if (now - recorded < std::chrono::seconds(static_cast<i64>(IMPORT_ARTIFACT_USE_BUCKET_SECONDS))) {
			return;
		}
		fs::last_write_time(physical, now, ec);
	}

	inline void ImportArtifactEnumerate(Vector<ImportArtifactEntry>& out) {
		namespace fs = std::filesystem;
		out.clear();

		std::error_code ec;
		fs::path root(ImportArtifactDirectory().ToString());
		if (!fs::is_directory(root, ec) || ec) {
			return;
		}
		for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
			if (ec) {
				break;
			}
			std::error_code fileEc;
			if (!it->is_regular_file(fileEc) || fileEc) {
				continue;
			}
			const fs::path& file = it->path();
			if (file.extension() != ".slart") {
				continue;
			}

			ImportArtifactEntry entry;
			entry.guid = Guid::FromString(file.stem().string());
			entry.path = Path(file.generic_string());
			entry.bytes = static_cast<u64>(fs::file_size(file, fileEc));
			if (fileEc) {
				entry.bytes = 0;
			}
			fs::file_time_type used = fs::last_write_time(file, fileEc);
			entry.lastUse = fileEc ? 0 : static_cast<i64>(used.time_since_epoch().count());
			out.push_back(entry);
		}
	}

	inline u32 ImportArtifactPlanEviction(Vector<ImportArtifactEntry>& entries, u64 budgetBytes) {
		u64 total = 0;
		for (const ImportArtifactEntry& entry : entries) {
			total += entry.bytes;
		}
		if (total <= budgetBytes) {
			return 0;
		}

		std::sort(entries.begin(), entries.end(), [](const ImportArtifactEntry& a, const ImportArtifactEntry& b) {
			if (a.lastUse != b.lastUse) {
				return a.lastUse < b.lastUse;
			}
			return a.guid < b.guid;
		});

		u32 evict = 0;
		for (const ImportArtifactEntry& entry : entries) {
			if (total <= budgetBytes) {
				break;
			}
			total -= entry.bytes;
			++evict;
		}
		return evict;
	}

	template <typename IsIndexedFn>
	inline ImportArtifactSweepResult ImportArtifactSweep(u64 budgetBytes, IsIndexedFn&& isIndexed) {
		namespace fs = std::filesystem;

		ImportArtifactSweepResult result;
		Vector<ImportArtifactEntry> entries{GetMemoryResource(MemTag::Asset)};
		ImportArtifactEnumerate(entries);

		Vector<ImportArtifactEntry> kept{GetMemoryResource(MemTag::Asset)};
		kept.reserve(entries.size());
		for (const ImportArtifactEntry& entry : entries) {
			result.bytesBefore += entry.bytes;
			if (entry.guid.IsValid() && isIndexed(entry.guid)) {
				kept.push_back(entry);
				continue;
			}
			std::error_code ec;
			fs::remove(fs::path(entry.path.ToString()), ec);
			if (!ec) {
				++result.orphans;
			} else {
				kept.push_back(entry);
			}
		}

		u32 evict = ImportArtifactPlanEviction(kept, budgetBytes);
		for (u32 i = 0; i < evict; ++i) {
			std::error_code ec;
			fs::remove(fs::path(kept[i].path.ToString()), ec);
			if (!ec) {
				++result.evicted;
				kept[i].bytes = 0;
			}
		}

		for (const ImportArtifactEntry& entry : kept) {
			result.bytesAfter += entry.bytes;
		}
		return result;
	}

} // namespace Shalltear
