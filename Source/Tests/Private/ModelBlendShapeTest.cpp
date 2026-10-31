#include "TestsPCH.h"

#include "FBXParser.h"
#include "glTFParser.h"

#include <algorithm>
#include <filesystem>

using namespace Shalltear;

namespace {

	std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	Path CorpusFile(const char* name) {
		std::filesystem::path dir = RepoDir("External/Submodules/ufbx/data");
		return dir.empty() ? Path() : Path((dir / name).generic_string());
	}

	Path KhronosFile(const char* relative) {
		std::filesystem::path dir = RepoDir("../glTF-Sample-Assets/Models");
		return dir.empty() ? Path() : Path((dir / relative).generic_string());
	}

	constexpr ModelParseOptions WithTracks{true};

	const ModelBlendChannel* FindChannel(const ModelMesh& mesh, std::string_view name) {
		for (const ModelBlendChannel& channel : mesh.blendChannels) {
			if (channel.name == name) {
				return &channel;
			}
		}
		return nullptr;
	}

	const ModelMesh* FirstMeshWithShapes(const ModelParseResult& parsed) {
		for (const ModelMesh& mesh : parsed.meshes) {
			if (!mesh.blendChannels.empty()) {
				return &mesh;
			}
		}
		return nullptr;
	}

	void ExpectWellFormed(const ModelParseResult& parsed, std::string_view what) {
		for (const ModelMesh& mesh : parsed.meshes) {
			for (const ModelBlendChannel& channel : mesh.blendChannels) {
				ASSERT_FALSE(channel.name.empty()) << what << ' ' << mesh.name;
				ASSERT_FALSE(channel.shapes.empty()) << what << ": channel '" << channel.name;
				for (size_t s = 0; s < channel.shapes.size(); ++s) {
					const ModelBlendShape& shape = channel.shapes[s];
					ASSERT_EQ(shape.positionOffsets.size(), shape.offsetVertices.size()) << what << ": '" << channel.name << "' target " << s;
					ASSERT_TRUE(shape.normalOffsets.empty() || shape.normalOffsets.size() == shape.offsetVertices.size());

					Vector<u32> seen{GetMemoryResource(MemTag::Asset)};
					for (const u32 vertex : shape.offsetVertices) {
						ASSERT_LT(static_cast<size_t>(vertex), mesh.vertices.size())
							<< what << ": '" << channel.name << "' target " << s << " offsets vertex " << vertex
							<< " of " << mesh.vertices.size();
						seen.push_back(vertex);
					}
					std::sort(seen.begin(), seen.end());
					ASSERT_TRUE(std::adjacent_find(seen.begin(), seen.end()) == seen.end()) << what << ": '" << channel.name << "' target " << s;

					if (s > 0) {
						ASSERT_GE(shape.targetWeight, channel.shapes[s - 1].targetWeight) << what << ": '" << channel.name << "' target " << s;
					}
				}
			}
		}
	}

} // namespace

TEST(ModelBlendShapeTest, OffsetsAreClosedOverSharedPositions) {
	const Path file = CorpusFile("maya_blend_shape_cube_7700_binary.fbx");
	ASSERT_FALSE(file.ToString().empty());

	const ModelParseResult parsed = ParseFBX(file, ModelParseOptions{});
	ASSERT_TRUE(parsed.success) << parsed.error;
	ExpectWellFormed(parsed, "maya_blend_shape_cube");

	const ModelMesh* mesh = FirstMeshWithShapes(parsed);
	ASSERT_NE(mesh, nullptr);

	size_t expanded = 0;
	for (const ModelBlendChannel& channel : mesh->blendChannels) {
		for (const ModelBlendShape& shape : channel.shapes) {
			Vector<bool> offset(mesh->vertices.size(), false, GetMemoryResource(MemTag::Asset));
			for (const u32 vertex : shape.offsetVertices) {
				offset[vertex] = true;
			}
			for (size_t v = 0; v < mesh->vertices.size(); ++v) {
				if (!offset[v]) {
					continue;
				}
				for (size_t other = 0; other < mesh->vertices.size(); ++other) {
					if (other == v || mesh->vertices[other].position != mesh->vertices[v].position) {
						continue;
					}
					EXPECT_TRUE(offset[other]);
					++expanded;
				}
			}
		}
	}

	EXPECT_GT(expanded, 0u);
}

TEST(ModelBlendShapeTest, MeshWithNoDeformersCarriesNoChannels) {
	const Path file = CorpusFile("maya_cube_7500_binary.fbx");
	ASSERT_FALSE(file.ToString().empty());

	const ModelParseResult parsed = ParseFBX(file, ModelParseOptions{});
	ASSERT_TRUE(parsed.success) << parsed.error;
	for (const ModelMesh& mesh : parsed.meshes) {
		EXPECT_TRUE(mesh.blendChannels.empty()) << "mesh '" << mesh.name;
	}
}

TEST(ModelBlendShapeTest, InBetweenTargetsScaleToPercent) {
	const Path file = CorpusFile("maya_blend_inbetween_7500_binary.fbx");
	ASSERT_FALSE(file.ToString().empty());

	const ModelParseResult parsed = ParseFBX(file, ModelParseOptions{});
	ASSERT_TRUE(parsed.success) << parsed.error;
	ExpectWellFormed(parsed, "maya_blend_inbetween");

	const ModelMesh* mesh = FirstMeshWithShapes(parsed);
	ASSERT_NE(mesh, nullptr);

	const ModelBlendChannel* lift = FindChannel(*mesh, "KeyBlend.LiftTop");
	ASSERT_NE(lift, nullptr);
	ASSERT_EQ(lift->shapes.size(), 2u);
	EXPECT_NEAR(lift->shapes[0].targetWeight, 50.0f, 0.01f);
	EXPECT_NEAR(lift->shapes[1].targetWeight, 100.0f, 0.01f);

	const ModelBlendChannel* side = FindChannel(*mesh, "KeyBlend.MoveToSide");
	ASSERT_NE(side, nullptr);
	ASSERT_EQ(side->shapes.size(), 3u);
	EXPECT_NEAR(side->shapes[0].targetWeight, -49.9f, 0.2f);
	EXPECT_NEAR(side->shapes[1].targetWeight, -24.9f, 0.2f);
	EXPECT_NEAR(side->shapes[2].targetWeight, 100.0f, 0.01f);
}

TEST(ModelBlendShapeTest, MorphWeightTrackAddressesChannelInPercent) {
	const Path file = CorpusFile("blender_293_barbarian_7400_binary.fbx");
	ASSERT_FALSE(file.ToString().empty());

	const ModelParseResult parsed = ParseFBX(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	ExpectWellFormed(parsed, "blender_293_barbarian");

	size_t morphTracks = 0;
	bool sawLeft = false;
	for (const ModelTrack& track : parsed.tracks) {
		if (track.channel != ModelChannel::MorphWeight) {
			EXPECT_EQ(track.blendChannel, -1);
			continue;
		}
		++morphTracks;
		ASSERT_GE(track.node, 0);
		const i32 meshIndex = parsed.nodes[static_cast<size_t>(track.node)].mesh;
		ASSERT_GE(meshIndex, 0);
		const ModelMesh& mesh = parsed.meshes[static_cast<size_t>(meshIndex)];
		ASSERT_GE(track.blendChannel, 0);
		ASSERT_LT(static_cast<size_t>(track.blendChannel), mesh.blendChannels.size());

		if (mesh.blendChannels[static_cast<size_t>(track.blendChannel)].name == "Left") {
			sawLeft = true;
			EXPECT_NEAR(track.values.front().x, 22.4f, 0.1f);
		}
	}
	EXPECT_EQ(morphTracks, 3u);
	EXPECT_TRUE(sawLeft);
}

TEST(ModelBlendShapeTest, AGLTFWeightsChannelSplitsPerSlider) {
	const Path file = KhronosFile("AnimatedMorphCube/glTF/AnimatedMorphCube.gltf");
	if (file.ToString().empty()) {
		GTEST_SKIP() << "Khronos corpus not cloned beside the repo";
	}

	const ModelParseResult parsed = ParseGLTF(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	ExpectWellFormed(parsed, "AnimatedMorphCube");

	const ModelMesh* mesh = FirstMeshWithShapes(parsed);
	ASSERT_NE(mesh, nullptr);
	ASSERT_EQ(mesh->blendChannels.size(), 2u);
	for (const ModelBlendChannel& channel : mesh->blendChannels) {
		ASSERT_EQ(channel.shapes.size(), 1u);
		EXPECT_NEAR(channel.shapes[0].targetWeight, 100.0f, 0.01f);
		EXPECT_FALSE(channel.shapes[0].normalOffsets.empty());
	}

	Vector<i32> driven{GetMemoryResource(MemTag::Asset)};
	Vector<const ModelTrack*> morphTracks{GetMemoryResource(MemTag::Asset)};
	f32 peak = 0.0f;
	for (const ModelTrack& track : parsed.tracks) {
		if (track.channel != ModelChannel::MorphWeight) {
			continue;
		}
		driven.push_back(track.blendChannel);
		morphTracks.push_back(&track);
		for (const Vec4& value : track.values) {
			peak = Math::Max(peak, value.x);
		}
	}
	std::sort(driven.begin(), driven.end());
	ASSERT_EQ(driven.size(), 2u);
	EXPECT_EQ(driven[0], 0);
	EXPECT_EQ(driven[1], 1);

	ASSERT_EQ(morphTracks.size(), 2u);
	bool differ = false;
	for (size_t k = 0; k < morphTracks[0]->values.size() && k < morphTracks[1]->values.size(); ++k) {
		if (morphTracks[0]->values[k].x != morphTracks[1]->values[k].x) {
			differ = true;
			break;
		}
	}
	EXPECT_TRUE(differ);

	EXPECT_NEAR(peak, 100.0f, 0.5f);
}

TEST(ModelBlendShapeTest, OneChannelSpansTwoPrimitives) {
	const Path file = KhronosFile("MorphPrimitivesTest/glTF/MorphPrimitivesTest.gltf");
	if (file.ToString().empty()) {
		GTEST_SKIP() << "Khronos corpus not cloned beside the repo";
	}

	const ModelParseResult parsed = ParseGLTF(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	ExpectWellFormed(parsed, "MorphPrimitivesTest");

	const ModelMesh* mesh = FirstMeshWithShapes(parsed);
	ASSERT_NE(mesh, nullptr);
	ASSERT_GE(mesh->submeshes.size(), 2u);
	ASSERT_EQ(mesh->blendChannels.size(), 1u);

	const ModelBlendShape& shape = mesh->blendChannels[0].shapes[0];
	u32 lowest = static_cast<u32>(mesh->vertices.size());
	u32 highest = 0;
	for (const u32 vertex : shape.offsetVertices) {
		lowest = Math::Min(lowest, vertex);
		highest = Math::Max(highest, vertex);
	}
	EXPECT_GT(highest, lowest);
}
