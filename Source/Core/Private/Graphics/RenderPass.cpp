#include "Shalltear/Core/Graphics/RenderPass.h"

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

namespace Shalltear {

	RenderPass::~RenderPass() = default;

	u8 PassPlan::AddTarget(RHI::Format format, u16 divisor) {
		SLT_CORE_ASSERT_MSG(_targetCount < MAX_TARGETS, "a pass plan declared more than MAX_TARGETS targets");
		if (_targetCount >= MAX_TARGETS) {
			SLT_LOG_ERROR("Graphics", "a pass plan declared more than {} targets (rest dropped)", MAX_TARGETS);
			return PASS_NO_READ;
		}
		PassTarget& target = _targets[_targetCount];
		target.format = format;
		target.divisor = divisor > 0 ? divisor : 1;
		return static_cast<u8>(_targetCount++);
	}

	bool PassPlan::AddStep(u8 target, u8 readA, u8 readB) {
		SLT_CORE_ASSERT_MSG(_stepCount < MAX_STEPS, "a pass plan declared more than MAX_STEPS steps");
		if (_stepCount >= MAX_STEPS) {
			return false;
		}
		if (target != PASS_SCENE_TARGET && target >= _targetCount) {
			SLT_LOG_ERROR("Graphics", "a pass step writes undeclared target {} (dropped)", target);
			return false;
		}
		const auto readable = [&](u8 index) {
			return index == PASS_NO_READ || (index < _targetCount && (_written & (1u << index)) != 0);
		};
		if (!readable(readA) || !readable(readB)) {
			SLT_LOG_ERROR("Graphics", "a pass step reads target {} or {} before any step writes it (dropped)",
						  readA, readB);
			return false;
		}
		if (target != PASS_SCENE_TARGET && (target == readA || target == readB)) {
			SLT_LOG_ERROR("Graphics", "a pass step samples target {} it renders into (dropped)", target);
			return false;
		}
		PassStep& step = _steps[_stepCount++];
		step.target = target;
		step.reads[0] = readA;
		step.reads[1] = readB;
		if (target != PASS_SCENE_TARGET) {
			_written |= 1u << target;
		}
		return true;
	}

	namespace {

		u32 SoleProducedBit(PassProduces kind) {
			const u8 bits = static_cast<u8>(kind);
			if (bits == 0 || (bits & (bits - 1)) != 0) {
				return 8;
			}
			u32 index = 0;
			for (u8 probe = bits; (probe & 1u) == 0; probe >>= 1) {
				++index;
			}
			return index;
		}

	} // namespace

	bool PassPlan::Produce(PassProduces kind, u8 target) {
		const u32 bit = SoleProducedBit(kind);
		if (bit >= 8) {
			SLT_LOG_ERROR("Graphics", "a pass plan produced {} kinds at once (should be 1)",
						  static_cast<u32>(kind));
			return false;
		}
		if (target >= _targetCount) {
			SLT_LOG_ERROR("Graphics", "a pass plan produces undeclared target {} (dropped)",
						  target);
			return false;
		}
		if (HasFlag(_produced, kind)) {
			SLT_LOG_ERROR("Graphics", "a pass plan produces kind {} twice (2nd dropped)", static_cast<u32>(kind));
			return false;
		}
		_produced |= kind;
		_producedTargets[bit] = target;
		return true;
	}

	bool PassPlan::DeclareVolume(RHI::Format format, Vec3I size, std::span<const u8> contents, u64 revision) {
		if (_hasVolume) {
			SLT_LOG_ERROR("Graphics", "a pass plan declared a 2nd volume (only 1 reaches a shader)");
			return false;
		}
		if (size.x <= 0 || size.y <= 0 || size.z <= 0 || format == RHI::Format::Unknown) {
			SLT_LOG_ERROR("Graphics", "a pass plan declared a {}x{}x{} volume (extents should be positive)", size.x, size.y, size.z);
			return false;
		}
		if (contents.empty()) {
			SLT_LOG_ERROR("Graphics", "a pass plan declared a volume with no contents");
			return false;
		}
		_hasVolume = true;
		_volume.format = format;
		_volume.size = size;
		_volume.contents = contents;
		_volume.revision = revision;
		return true;
	}

	u8 PassPlan::GetProducedTarget(PassProduces kind) const {
		const u32 bit = SoleProducedBit(kind);
		return bit < 8 ? _producedTargets[bit] : PASS_NO_READ;
	}

	bool RenderPass::Import(const Path& sourcePath) {
		if (!FileSystem::Exists(sourcePath)) {
			SLT_LOG_ERROR("Asset", "RenderPass::Import: source '{}' doesn't exist", sourcePath.ToString());
			return false;
		}
		return TextSerializer::Deserialize(this, GetType(), FileSystem::ReadTextFile(sourcePath, MemTag::Asset));
	}

	bool RenderPass::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, TextSerializer::Serialize(this, GetType()));
	}

	Asset* RenderPassImporter::Import(const Path& sourcePath) {
		str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		if (text.empty()) {
			SLT_LOG_ERROR("Asset", "RenderPassImporter: '{}' is unreadable or empty", sourcePath.ToString());
			return nullptr;
		}
		LinearAllocator arena(text.size() + 64, MemTag::Asset);
		JSONValue root = JSONParse(text, arena);
		if (!root.IsObject()) {
			SLT_LOG_ERROR("Asset", "RenderPassImporter: '{}' isn't a JSON object", sourcePath.ToString());
			return nullptr;
		}

		std::string_view typeName = root["_type"].AsString();
		if (typeName.empty()) {
			SLT_LOG_ERROR("Asset", "RenderPassImporter: '{}' has no '_type'",
						  sourcePath.ToString());
			return nullptr;
		}

		const TypeInfo* type = TypeRegistry::FindType(StringHash(typeName));
		if (!type) {
			SLT_LOG_ERROR("Asset", "RenderPassImporter: '{}' names pass kind '{}', which this build doesnt know. "
								   "A module defining it may not be loaded",
						  sourcePath.ToString(), str(typeName));
			return nullptr;
		}
		if (!TypeRegistry::IsDerivedFrom(type, RenderPass::GetReflectType())) {
			SLT_LOG_ERROR("Asset", "RenderPassImporter: '{}' has type '{}' (not a RenderPass)",
						  sourcePath.ToString(), str(typeName));
			return nullptr;
		}

		Object* object = TypeRegistry::CreateObjectInstance(type, MemTag::Asset);
		if (!object) {
			SLT_LOG_ERROR("Asset", "RenderPassImporter: '{}' names '{}', which is not constructible. A pass kind "
								   "needs SLT_REFLECT_CREATABLE()",
						  sourcePath.ToString(), str(typeName));
			return nullptr;
		}

		auto* pass = static_cast<RenderPass*>(object);
		if (!pass->Import(sourcePath)) {
			TypeRegistry::DestroyObjectInstance(pass);
			return nullptr;
		}
		return pass;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::RenderPass)
