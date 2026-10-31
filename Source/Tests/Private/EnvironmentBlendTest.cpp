#include "TestsPCH.h"

#include "Graphics/EnvironmentBlend.h"

#include <array>

using namespace Shalltear;
using Shalltear::GraphicsInternal::ComputeEnvironmentWeights;
using Shalltear::GraphicsInternal::EnvironmentLayerInput;
using Shalltear::GraphicsInternal::NO_REFLECTION_LAYER;
using Shalltear::GraphicsInternal::SelectReflectionCubes;

namespace {

	EnvironmentLayerInput Layer(f32 priority, f32 contribution, u32 registration) {
		EnvironmentLayerInput layer;
		layer.priority = priority;
		layer.contribution = contribution;
		layer.registration = registration;
		return layer;
	}

} // anonymous namespace

TEST(EnvironmentBlendTest, TopLayerFirstRestShareRemainder) {
	const std::array<EnvironmentLayerInput, 2> layers = {Layer(0.0f, 1.0f, 0), Layer(1.0f, 0.4f, 1)};
	std::array<f32, 2> weights{};
	std::array<u32, 2> order{};
	const f32 remainder = ComputeEnvironmentWeights(layers, weights, order);
	EXPECT_FLOAT_EQ(weights[1], 0.4f);
	EXPECT_FLOAT_EQ(weights[0], 1.0f * (1.0f - 0.4f));
	EXPECT_FLOAT_EQ(remainder, 0.0f);
	EXPECT_EQ(order[0], 0u);
	EXPECT_EQ(order[1], 1u);
}

TEST(EnvironmentBlendTest, PriorityAndNotRegistrationDecidesTheOrder) {
	const std::array<EnvironmentLayerInput, 2> layers = {Layer(5.0f, 0.4f, 0), Layer(-1.0f, 1.0f, 1)};
	std::array<f32, 2> weights{};
	std::array<u32, 2> order{};
	ComputeEnvironmentWeights(layers, weights, order);
	EXPECT_FLOAT_EQ(weights[0], 0.4f);
	EXPECT_FLOAT_EQ(weights[1], 0.6f);
	EXPECT_EQ(order[1], 0u);
}

TEST(EnvironmentBlendTest, TiePutsFirstRegisteredOnTop) {
	const std::array<EnvironmentLayerInput, 2> layers = {Layer(0.0f, 1.0f, 0), Layer(0.0f, 1.0f, 1)};
	std::array<f32, 2> weights{};
	std::array<u32, 2> order{};
	ComputeEnvironmentWeights(layers, weights, order);
	EXPECT_FLOAT_EQ(weights[0], 1.0f);
	EXPECT_FLOAT_EQ(weights[1], 0.0f);
}

TEST(EnvironmentBlendTest, UnclaimedShareIsRemainder) {
	{
		const std::array<EnvironmentLayerInput, 1> layers = {Layer(0.0f, 0.3f, 0)};
		std::array<f32, 1> weights{};
		std::array<u32, 1> order{};
		EXPECT_FLOAT_EQ(ComputeEnvironmentWeights(layers, weights, order), 0.7f);
		EXPECT_FLOAT_EQ(weights[0], 0.3f);
	}
	const std::array<EnvironmentLayerInput, 4> layers = {Layer(2.0f, 0.25f, 0), Layer(0.0f, 0.5f, 1), Layer(1.0f, 0.8f, 2),
														 Layer(3.0f, 0.1f, 3)};
	std::array<f32, 4> weights{};
	std::array<u32, 4> order{};
	const f32 remainder = ComputeEnvironmentWeights(layers, weights, order);
	EXPECT_FLOAT_EQ(weights[3], 0.1f);
	EXPECT_FLOAT_EQ(weights[0], 0.25f * 0.9f);
	EXPECT_FLOAT_EQ(weights[2], 0.8f * 0.9f * 0.75f);
	EXPECT_FLOAT_EQ(weights[1], 0.5f * 0.9f * 0.75f * 0.2f);
	EXPECT_NEAR(weights[0] + weights[1] + weights[2] + weights[3] + remainder, 1.0f, 1e-6f);
}

TEST(EnvironmentBlendTest, TwoHeaviestCubesReflect) {
	const std::array<f32, 4> weights = {0.2f, 0.5f, 0.1f, 0.3f};
	const std::array<bool, 4> hasCube = {true, true, false, true};
	u32 index[2];
	f32 weight[2];
	SelectReflectionCubes(std::span<const f32>(weights), std::span<const bool>(hasCube.data(), hasCube.size()), index, weight);
	EXPECT_EQ(index[0], 1u);
	EXPECT_EQ(index[1], 3u);
	const f32 cubes = 0.2f + 0.5f + 0.3f;
	EXPECT_FLOAT_EQ(weight[0], 0.5f * cubes / 0.8f);
	EXPECT_FLOAT_EQ(weight[1], 0.3f * cubes / 0.8f);

	const std::array<f32, 2> one = {0.6f, 0.4f};
	const std::array<bool, 2> oneCube = {true, false};
	SelectReflectionCubes(std::span<const f32>(one), std::span<const bool>(oneCube.data(), oneCube.size()), index, weight);
	EXPECT_EQ(index[0], 0u);
	EXPECT_FLOAT_EQ(weight[0], 0.6f);
	EXPECT_EQ(index[1], NO_REFLECTION_LAYER);
	EXPECT_FLOAT_EQ(weight[1], 0.0f);
}
