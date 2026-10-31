#include "TestsPCH.h"

#include "FBXParser.h"
#include "glTFParser.h"
#include "ModelImport.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"

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

	Path FixtureFile(const char* name) {
		std::filesystem::path dir = RepoDir("Source/Tests/Fixtures/Graphics");
		return dir.empty() ? Path() : Path((dir / name).generic_string());
	}

	Path CorpusFile(const char* name) {
		std::filesystem::path dir = RepoDir("External/Submodules/ufbx/data");
		return dir.empty() ? Path() : Path((dir / name).generic_string());
	}

	Mat4 WorldMatrix(const ModelParseResult& parsed, i32 node) {
		if (node < 0 || static_cast<size_t>(node) >= parsed.nodes.size()) {
			return Mat4::Identity();
		}
		Mat4 matrix = parsed.nodes[static_cast<size_t>(node)].localMatrix;
		i32 parent = parsed.nodes[static_cast<size_t>(node)].parent;
		for (int guard = 0; parent >= 0 && guard < 256; ++guard) {
			matrix = parsed.nodes[static_cast<size_t>(parent)].localMatrix * matrix;
			parent = parsed.nodes[static_cast<size_t>(parent)].parent;
		}
		return matrix;
	}

	i32 FindSkinnedNode(const ModelParseResult& parsed) {
		for (size_t i = 0; i < parsed.nodes.size(); ++i) {
			if (parsed.nodes[i].skin >= 0 && parsed.nodes[i].mesh >= 0) {
				return static_cast<i32>(i);
			}
		}
		return -1;
	}

	f32 WorstBindResidual(const ModelParseResult& parsed, i32 skinnedNode) {
		const ModelSkin& skin = parsed.skins[static_cast<size_t>(parsed.nodes[static_cast<size_t>(skinnedNode)].skin)];
		const Mat4 meshWorld = WorldMatrix(parsed, skinnedNode);
		f32 worst = 0.0f;
		for (size_t j = 0; j < skin.joints.size(); ++j) {
			if (skin.joints[j] < 0) {
				continue;
			}
			const Mat4 composed = WorldMatrix(parsed, skin.joints[j]) * skin.inverseBind[j];
			for (size_t e = 0; e < 16; ++e) {
				worst = Math::Max(worst, Math::Abs(composed.data[e] - meshWorld.data[e]));
			}
		}
		return worst;
	}

	f32 WeightSum(const ModelSkinInfluence& influence) {
		return influence.weights[0] + influence.weights[1] + influence.weights[2] + influence.weights[3];
	}

} // anonymous namespace

TEST(ModelSkinTest, SimpleSkinKeepsTwoJoints) {
	ModelParseResult parsed = ParseGLTF(FixtureFile("SimpleSkin.gltf"));
	ASSERT_TRUE(parsed.success) << parsed.error;

	ASSERT_EQ(parsed.skins.size(), 1u);
	const ModelSkin& skin = parsed.skins[0];
	EXPECT_EQ(skin.joints.size(), 2u);
	EXPECT_EQ(skin.inverseBind.size(), skin.joints.size());

	for (i32 joint : skin.joints) {
		EXPECT_GE(joint, 0);
		EXPECT_LT(static_cast<size_t>(joint), parsed.nodes.size());
	}
	EXPECT_EQ(parsed.nodes[static_cast<size_t>(skin.joints[1])].parent, skin.joints[0]);
	EXPECT_NEAR(parsed.nodes[static_cast<size_t>(skin.joints[1])].position.y, 1.0f, 1e-5f);

	const i32 skinned = FindSkinnedNode(parsed);
	ASSERT_GE(skinned, 0);
	EXPECT_EQ(parsed.nodes[static_cast<size_t>(skinned)].skin, 0);
	EXPECT_EQ(parsed.nodes[static_cast<size_t>(skinned)].mesh, 0);
}

TEST(ModelSkinTest, InverseBindMatricesAreOnesFileStates) {
	ModelParseResult parsed = ParseGLTF(FixtureFile("SimpleSkin.gltf"));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.skins.size(), 1u);
	ASSERT_EQ(parsed.skins[0].inverseBind.size(), 2u);

	EXPECT_TRUE(Mat4::Approximately(parsed.skins[0].inverseBind[0], Mat4::Identity(), 1e-5f));

	Mat4 expected = Mat4::Identity();
	expected(1, 3) = -1.0f;
	EXPECT_TRUE(Mat4::Approximately(parsed.skins[0].inverseBind[1], expected, 1e-5f));
}

TEST(ModelSkinTest, EveryVertexCarriesWeightsFileStates) {
	ModelParseResult parsed = ParseGLTF(FixtureFile("SimpleSkin.gltf"));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.meshes.size(), 1u);

	const ModelMesh& mesh = parsed.meshes[0];
	ASSERT_EQ(mesh.vertices.size(), 10u);
	ASSERT_EQ(mesh.influences.size(), mesh.vertices.size());

	const f32 expected[5][2] = {{1.0f, 0.0f}, {0.75f, 0.25f}, {0.5f, 0.5f}, {0.25f, 0.75f}, {0.0f, 1.0f}};
	for (size_t v = 0; v < 10; ++v) {
		const ModelSkinInfluence& influence = mesh.influences[v];
		EXPECT_NEAR(influence.weights[0], expected[v / 2][0], 1e-5f) << "vertex " << v;
		EXPECT_NEAR(influence.weights[1], expected[v / 2][1], 1e-5f) << "vertex " << v;
		EXPECT_NEAR(influence.weights[2], 0.0f, 1e-5f) << "vertex " << v;
		EXPECT_NEAR(influence.weights[3], 0.0f, 1e-5f) << "vertex " << v;
		if (influence.weights[1] > 0.0f) {
			EXPECT_EQ(influence.joints[1], 1) << "vertex " << v;
		}
	}
	EXPECT_NEAR(mesh.vertices[0].position.y, 0.0f, 1e-5f);
	EXPECT_NEAR(mesh.vertices[8].position.y, 2.0f, 1e-5f);
}

TEST(ModelSkinTest, InfluenceIndexesSkinsJointListNotNodes) {
	ModelParseResult parsed = ParseGLTF(FixtureFile("SimpleSkin.gltf"));
	ASSERT_TRUE(parsed.success) << parsed.error;
	const i32 skinned = FindSkinnedNode(parsed);
	ASSERT_GE(skinned, 0);
	const ModelSkin& skin = parsed.skins[static_cast<size_t>(parsed.nodes[static_cast<size_t>(skinned)].skin)];
	const ModelMesh& mesh = parsed.meshes[static_cast<size_t>(parsed.nodes[static_cast<size_t>(skinned)].mesh)];

	ASSERT_LT(skin.joints.size(), parsed.nodes.size());
	for (const ModelSkinInfluence& influence : mesh.influences) {
		for (int lane = 0; lane < 4; ++lane) {
			EXPECT_LT(influence.joints[lane], skin.joints.size());
		}
	}
}

TEST(ModelSkinTest, BindPoseInvertsJointsWorldTransform) {
	ModelParseResult parsed = ParseGLTF(FixtureFile("Fox.glb"));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.skins.size(), 1u);
	EXPECT_EQ(parsed.skins[0].joints.size(), 24u);

	const i32 skinned = FindSkinnedNode(parsed);
	ASSERT_GE(skinned, 0);
	for (i32 joint : parsed.skins[0].joints) {
		ASSERT_GE(joint, 0);
	}

	EXPECT_LT(WorstBindResidual(parsed, skinned), 1e-3f);
}

TEST(ModelSkinTest, BindPosesCrossedIntoEnginesFrame) {
	ModelParseResult parsed = ParseGLTF(FixtureFile("Fox.glb"));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_FALSE(parsed.skins.empty());

	const ModelSkin& skin = parsed.skins[0];
	i32 hip = -1;
	for (size_t j = 0; j < skin.joints.size(); ++j) {
		if (skin.joints[j] >= 0 && parsed.nodes[static_cast<size_t>(skin.joints[j])].name == "b_Hip_01") {
			hip = static_cast<i32>(j);
			break;
		}
	}
	ASSERT_GE(hip, 0);
	const Mat4& bind = skin.inverseBind[static_cast<size_t>(hip)];
	EXPECT_NEAR(bind(0, 3), 30.636034f, 1e-3f);
	EXPECT_NEAR(bind(1, 3), -40.256638f, 1e-3f);
	EXPECT_NEAR(bind(2, 3), 0.000044f, 1e-3f);
}

TEST(ModelSkinTest, FbxSkinBindsAtBindPose) {
	ModelParseResult parsed = ParseFBX(CorpusFile("maya_game_sausage_7500_binary.fbx"));
	ASSERT_TRUE(parsed.success) << parsed.error;

	ASSERT_EQ(parsed.skins.size(), 1u);
	EXPECT_EQ(parsed.skins[0].joints.size(), 3u);
	for (i32 joint : parsed.skins[0].joints) {
		EXPECT_GE(joint, 0);
	}

	const i32 skinned = FindSkinnedNode(parsed);
	ASSERT_GE(skinned, 0);
	EXPECT_LT(WorstBindResidual(parsed, skinned), 1e-4f);

	const ModelMesh& mesh = parsed.meshes[static_cast<size_t>(parsed.nodes[static_cast<size_t>(skinned)].mesh)];
	EXPECT_EQ(mesh.influences.size(), mesh.vertices.size());
}

TEST(ModelSkinTest, WeldKeepsEachVertexWithWeights) {
	ModelParseResult parsed = ParseFBX(CorpusFile("maya_game_sausage_7500_binary.fbx"));
	ASSERT_TRUE(parsed.success) << parsed.error;
	const i32 skinned = FindSkinnedNode(parsed);
	ASSERT_GE(skinned, 0);

	const ModelNode& node = parsed.nodes[static_cast<size_t>(skinned)];
	const ModelMesh& mesh = parsed.meshes[static_cast<size_t>(node.mesh)];
	const ModelSkin& skin = parsed.skins[static_cast<size_t>(node.skin)];
	const Mat4 meshWorld = WorldMatrix(parsed, skinned);
	ASSERT_EQ(mesh.influences.size(), mesh.vertices.size());

	f32 worst = 0.0f;
	size_t bound = 0;
	for (size_t v = 0; v < mesh.vertices.size(); ++v) {
		const ModelSkinInfluence& influence = mesh.influences[v];
		if (WeightSum(influence) <= 0.0f) {
			continue;
		}
		Vec3 skinned3 = Vec3::Zero();
		for (int lane = 0; lane < 4; ++lane) {
			if (influence.weights[lane] <= 0.0f) {
				continue;
			}
			const i32 joint = skin.joints[influence.joints[lane]];
			const Mat4 matrix = WorldMatrix(parsed, joint) * skin.inverseBind[influence.joints[lane]];
			skinned3 = skinned3 + matrix.TransformPoint(mesh.vertices[v].position) * influence.weights[lane];
		}
		const Vec3 want = meshWorld.TransformPoint(mesh.vertices[v].position);
		worst = Math::Max(worst, (skinned3 - want).Length());
		++bound;
	}
	EXPECT_GT(bound, 20u);
	EXPECT_LT(worst, 1e-4f);

	Vector<u16> distinct{GetMemoryResource(MemTag::Asset)};
	for (const ModelSkinInfluence& influence : mesh.influences) {
		for (int lane = 0; lane < 4; ++lane) {
			if (influence.weights[lane] > 0.0f &&
				std::find(distinct.begin(), distinct.end(), influence.joints[lane]) == distinct.end()) {
				distinct.push_back(influence.joints[lane]);
			}
		}
	}
	EXPECT_EQ(distinct.size(), 3u);
}

TEST(ModelSkinTest, RiggedCharacterHas45Bones) {
	ModelParseResult parsed = ParseFBX(CorpusFile("maya_kenney_character_7700_binary.fbx"));
	ASSERT_TRUE(parsed.success) << parsed.error;

	ASSERT_EQ(parsed.skins.size(), 1u);
	EXPECT_EQ(parsed.skins[0].joints.size(), 45u);
	EXPECT_EQ(parsed.skins[0].inverseBind.size(), 45u);
	for (i32 joint : parsed.skins[0].joints) {
		EXPECT_GE(joint, 0);
	}

	const i32 skinned = FindSkinnedNode(parsed);
	ASSERT_GE(skinned, 0);
	const ModelMesh& mesh = parsed.meshes[static_cast<size_t>(parsed.nodes[static_cast<size_t>(skinned)].mesh)];
	EXPECT_EQ(mesh.influences.size(), mesh.vertices.size());

	for (const ModelSkinInfluence& influence : mesh.influences) {
		EXPECT_NEAR(WeightSum(influence), 1.0f, 1e-4f);
	}
}

TEST(ModelSkinTest, FormatsAgreeOnBindPose) {
	ModelParseResult glTF = ParseGLTF(FixtureFile("Fox.glb"));
	ASSERT_TRUE(glTF.success) << glTF.error;
	ModelParseResult fbx = ParseFBX(CorpusFile("maya_game_sausage_7500_binary.fbx"));
	ASSERT_TRUE(fbx.success) << fbx.error;

	const i32 glTFNode = FindSkinnedNode(glTF);
	const i32 fbxNode = FindSkinnedNode(fbx);
	ASSERT_GE(glTFNode, 0);
	ASSERT_GE(fbxNode, 0);

	EXPECT_LT(WorstBindResidual(glTF, glTFNode), 1e-3f);
	EXPECT_LT(WorstBindResidual(fbx, fbxNode), 1e-3f);

	for (const ModelParseResult* parsed : {&glTF, &fbx}) {
		for (const ModelMesh& mesh : parsed->meshes) {
			for (const ModelSkinInfluence& influence : mesh.influences) {
				const f32 sum = WeightSum(influence);
				EXPECT_TRUE(sum == 0.0f || Math::Approximately(sum, 1.0f, 1e-4f)) << sum;
			}
		}
	}
}

TEST(ModelSkinTest, UnskinnedModelHasNoSkin) {
	for (const char* fixture : {"cube.glb", "hierarchy.gltf", "BoomBoxWithAxes.gltf"}) {
		ModelParseResult parsed = ParseGLTF(FixtureFile(fixture));
		ASSERT_TRUE(parsed.success) << fixture << ": " << parsed.error;
		EXPECT_TRUE(parsed.skins.empty()) << fixture;
		for (const ModelMesh& mesh : parsed.meshes) {
			EXPECT_TRUE(mesh.influences.empty()) << fixture << ": mesh '" << mesh.name << "'";
		}
		for (const ModelNode& node : parsed.nodes) {
			EXPECT_EQ(node.skin, -1) << fixture << ": node '" << node.name << "'";
		}
	}

	ModelParseResult fbx = ParseFBX(CorpusFile("maya_textured_cube_7500_binary.fbx"));
	ASSERT_TRUE(fbx.success) << fbx.error;
	EXPECT_TRUE(fbx.skins.empty());
	for (const ModelMesh& mesh : fbx.meshes) {
		EXPECT_TRUE(mesh.influences.empty());
	}
}

TEST(ModelSkinTest, TheCorpusSkinsAreStructurallySound) {
	std::filesystem::path corpus = RepoDir("External/Submodules/ufbx/data");
	ASSERT_FALSE(corpus.empty());

	int filesWithSkin = 0;
	int skinnedMeshes = 0;
	int joints = 0;
	int unresolvedJoints = 0;
	int unboundVertices = 0;
	int boundVertices = 0;
	int multiInfluence = 0;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(corpus, ec)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".fbx") {
			continue;
		}
		str physical;
		try {
			physical = entry.path().generic_string();
		} catch (const std::exception&) {
			continue;
		}
		ModelParseResult parsed = ParseFBX(Path(physical));
		if (!parsed.success) {
			continue;
		}
		if (!parsed.skins.empty()) {
			++filesWithSkin;
		}
		for (const ModelSkin& skin : parsed.skins) {
			ASSERT_EQ(skin.joints.size(), skin.inverseBind.size()) << physical;
			joints += static_cast<int>(skin.joints.size());
			for (i32 joint : skin.joints) {
				if (joint < 0) {
					++unresolvedJoints;
					continue;
				}
				ASSERT_LT(static_cast<size_t>(joint), parsed.nodes.size()) << physical;
			}
		}
		for (const ModelNode& node : parsed.nodes) {
			ASSERT_LT(node.skin, static_cast<i32>(parsed.skins.size())) << physical;
		}
		for (const ModelMesh& mesh : parsed.meshes) {
			if (mesh.influences.empty()) {
				continue;
			}
			++skinnedMeshes;
			ASSERT_EQ(mesh.influences.size(), mesh.vertices.size()) << physical << " mesh '" << mesh.name << "'";
			for (const ModelSkinInfluence& influence : mesh.influences) {
				const f32 sum = WeightSum(influence);
				if (sum == 0.0f) {
					++unboundVertices;
					continue;
				}
				++boundVertices;
				if (influence.weights[1] > 0.0f) {
					++multiInfluence;
				}
				ASSERT_NEAR(sum, 1.0f, 1e-4f) << physical;
			}
		}
	}

	EXPECT_EQ(filesWithSkin, 60);
	EXPECT_EQ(skinnedMeshes, 62);
	EXPECT_EQ(joints, 274);
	EXPECT_EQ(unresolvedJoints, 0);
	EXPECT_EQ(boundVertices, 13840);
	EXPECT_EQ(multiInfluence, 8389);
	EXPECT_EQ(unboundVertices, 32);
}
