#pragma once

// single assignment, instruction i writes register i

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class CookReader;
	class CookWriter;
	struct MeshVertex;

	namespace VisualEffectsImpl {

		enum class EffectOp : u8 {
			Constant = 0,
			Input,
			Negate,
			Absolute,
			Sign,
			Floor,
			Ceiling,
			Round,
			Truncate,
			Fraction,
			Saturate,
			SquareRoot,
			ReciprocalSquareRoot,
			Reciprocal,
			Exponential,
			Exponential2,
			Logarithm,
			Logarithm2,
			Logarithm10,
			Sine,
			Cosine,
			Tangent,
			Arcsine,
			Arccosine,
			Arctangent,
			HyperbolicSine,
			HyperbolicCosine,
			HyperbolicTangent,
			Add,
			Subtract,
			Multiply,
			Divide,
			Power,
			Minimum,
			Maximum,
			Modulo,
			Arctangent2,
			Step,
			Lerp,
			Clamp,
			Smoothstep,
			Dot,
			Cross,
			Length,
			Distance,
			Normalize,
			Reflect,
			Swizzle,
			Construct,
			Less,
			LessEqual,
			Equal,
			Select,
			LogicalAnd,
			LogicalOr,
			LogicalNot,
			All,
			Any,
			IsNaN,
			IsInfinite,
			Random,
			ValueNoise,
			GradientNoise,
			Voronoi,
			FractalNoise,
			CurlNoise,
			MeshIndex,
			MeshVertex,
			Count
		};

		inline constexpr u32 EFFECT_MESH_CHANNEL_BITS = 3;

		struct EffectInstruction {
			EffectOp op = EffectOp::Constant;
			u8 width = 1;
			u8 operandCount = 0;
			u8 reserved = 0;
			u32 payload = 0;
			u32 operands[4] = {};
		};
		static_assert(sizeof(EffectInstruction) == 24, "a cooked program stores instructions as bytes");

		struct EffectInput {
			str name;
			u32 width = 1;
			// derived and never cooked
			u32 slot = 0;
		};

		// derived and never cooked
		struct EffectMeshInput {
			str name;
			u32 mesh = 0;
		};

		struct EffectMeshView {
			const MeshVertex* vertices = nullptr;
			u32 vertexCount = 0;
			const u32* indices = nullptr;
			u32 indexCount = 0;
		};

		struct EffectValue {
			f32 v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		};

		struct EffectProgram {
			Vector<EffectInstruction> code{GetMemoryResource(MemTag::Asset)};
			Vector<f32> constants{GetMemoryResource(MemTag::Asset)};
			Vector<EffectInput> inputs{GetMemoryResource(MemTag::Asset)};
			Vector<u32> roots{GetMemoryResource(MemTag::Asset)};
			Vector<EffectMeshInput> meshes{GetMemoryResource(MemTag::Asset)};

			bool IsEmpty() const { return code.empty(); }
		};

		namespace EffectPrograms {

			bool Assemble(std::string_view text, EffectProgram& out, str& error);

			// run before executing a cooked program
			bool Validate(const EffectProgram& program, str& error);

			void Cook(const EffectProgram& program, CookWriter& out);
			bool LoadCooked(CookReader& in, EffectProgram& out);

		} // namespace EffectPrograms

		class EffectMachine {
		public:
			bool Run(const EffectProgram& program, std::span<const EffectValue> slots, std::span<const EffectMeshView> meshes = {});
			const EffectValue& Root(const EffectProgram& program, u32 index) const { return _registers[program.roots[index]]; }
		private:
			Vector<EffectValue> _registers{GetMemoryResource(MemTag::Graphics)};
		};

	} // namespace VisualEffectsImpl

} // namespace Shalltear
