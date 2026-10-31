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

	const ModelNode* FindNode(const ModelParseResult& parsed, std::string_view name) {
		for (const ModelNode& node : parsed.nodes) {
			if (node.name == name) {
				return &node;
			}
		}
		return nullptr;
	}

	Vec3 WorldTranslation(const ModelParseResult& parsed, const ModelNode& node) {
		Mat4 matrix = node.localMatrix;
		i32 parent = node.parent;
		for (int guard = 0; parent >= 0 && guard < 64; ++guard) {
			const ModelNode& up = parsed.nodes[static_cast<size_t>(parent)];
			matrix = up.localMatrix * matrix;
			parent = up.parent;
		}
		return matrix.TransformPoint(Vec3::Zero());
	}

	void CountWindingAgreement(const ModelMesh& mesh, const Mat4& toModel, int& agree, int& disagree) {
		agree = 0;
		disagree = 0;
		for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
			const MeshVertex& a = mesh.vertices[mesh.indices[i]];
			const MeshVertex& b = mesh.vertices[mesh.indices[i + 1]];
			const MeshVertex& c = mesh.vertices[mesh.indices[i + 2]];
			Vec3 pa = toModel.TransformPoint(a.position);
			Vec3 pb = toModel.TransformPoint(b.position);
			Vec3 pc = toModel.TransformPoint(c.position);
			Vec3 geometric = Vec3::Cross(pb - pa, pc - pa);
			Vec3 normal = toModel.TransformVector(a.normal);
			if (Vec3::Dot(geometric, normal) >= 0.0f) {
				++agree;
			} else {
				++disagree;
			}
		}
	}

	Mat4 ModelMatrixForMesh(const ModelParseResult& parsed, i32 meshIndex) {
		for (const ModelNode& node : parsed.nodes) {
			if (node.mesh != meshIndex) {
				continue;
			}
			Mat4 matrix = node.localMatrix;
			i32 parent = node.parent;
			for (int guard = 0; parent >= 0 && guard < 64; ++guard) {
				const ModelNode& up = parsed.nodes[static_cast<size_t>(parent)];
				matrix = up.localMatrix * matrix;
				parent = up.parent;
			}
			return matrix;
		}
		return Mat4::Identity();
	}

} // anonymous namespace

TEST(ModelHandednessTest, AxisLabelsLandOnEngineSide) {
	ModelParseResult parsed = ParseGLTF(FixtureFile("BoomBoxWithAxes.gltf"));
	ASSERT_TRUE(parsed.success) << parsed.error;

	const ModelNode* x = FindNode(parsed, "X_axis");
	const ModelNode* y = FindNode(parsed, "Y_axis");
	const ModelNode* z = FindNode(parsed, "Z_axis");
	ASSERT_NE(x, nullptr);
	ASSERT_NE(y, nullptr);
	ASSERT_NE(z, nullptr);

	Vec3 xr = WorldTranslation(parsed, *x);
	Vec3 yr = WorldTranslation(parsed, *y);
	Vec3 zr = WorldTranslation(parsed, *z);

	EXPECT_LT(xr.x, -0.005f);
	EXPECT_GT(yr.y, 0.02f);
	EXPECT_GT(zr.z, 0.005f);

	EXPECT_NEAR(yr.z, 0.0f, 1e-4f);
	EXPECT_NEAR(xr.z, 0.0f, 1e-4f);
}

TEST(ModelHandednessTest, WindingFollowsMirrorNotStayingAsAuthored) {
	ModelParseResult parsed = ParseGLTF(FixtureFile("BoomBoxWithAxes.gltf"));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_FALSE(parsed.meshes.empty());

	const ModelMesh& body = parsed.meshes[0];
	ASSERT_GT(body.indices.size(), 1000u);

	int agree = 0;
	int disagree = 0;
	CountWindingAgreement(body, ModelMatrixForMesh(parsed, 0), agree, disagree);
	EXPECT_GT(agree, disagree * 4);
}

TEST(ModelHandednessTest, TheBitangentSignFlipsWithTheMirror) {
	ModelParseResult parsed = ParseGLTF(FixtureFile("BoomBoxWithAxes.gltf"));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_FALSE(parsed.meshes.empty());

	const ModelMesh& body = parsed.meshes[0];
	ASSERT_FALSE(body.vertices.empty());

	int negative = 0;
	int positive = 0;
	for (const MeshVertex& vertex : body.vertices) {
		(vertex.tangentSign < 0.0f ? negative : positive) += 1;
	}
	EXPECT_EQ(positive, 0);
	EXPECT_EQ(negative, static_cast<int>(body.vertices.size()));
}

TEST(ModelHandednessTest, AxisConventionsImportIdentically) {
	std::filesystem::path dir = RepoDir("External/Submodules/ufbx/data/blender_axes");
	ASSERT_FALSE(dir.empty());

	Vector<Vec3> placements{GetMemoryResource(MemTag::Asset)};
	int files = 0;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
		if (entry.path().extension() != ".fbx") {
			continue;
		}
		ModelParseResult parsed = ParseFBX(Path(entry.path().generic_string()));
		ASSERT_TRUE(parsed.success) << entry.path().filename().string() << ": " << parsed.error;
		ASSERT_FALSE(parsed.meshes.empty());
		ASSERT_FALSE(parsed.nodes.empty());
		++files;

		Vec3 world = WorldTranslation(parsed, parsed.nodes[0]);
		bool seen = false;
		for (const Vec3& previous : placements) {
			if ((previous - world).Length() < 1e-3f) {
				seen = true;
				break;
			}
		}
		if (!seen) {
			placements.push_back(world);
		}
	}

	EXPECT_EQ(files, 24);
	ASSERT_EQ(placements.size(), 1u) << files << ' ' << placements.size() << " different places";

	EXPECT_NEAR(placements[0].x, 1.0f, 1e-3f);
	EXPECT_NEAR(placements[0].y, 3.0f, 1e-3f);
	EXPECT_NEAR(placements[0].z, 2.0f, 1e-3f);
}

TEST(ModelHandednessTest, AZUpFileStandsUp) {
	std::filesystem::path corpus = RepoDir("External/Submodules/ufbx/data");
	ASSERT_FALSE(corpus.empty());

	ModelParseResult parsed = ParseFBX(Path((corpus / "max_geometry_transform_types_6100_binary.fbx").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_FALSE(parsed.meshes.empty());

	AABB world = AABB::Empty();
	for (const ModelNode& node : parsed.nodes) {
		if (node.mesh < 0) {
			continue;
		}
		Mat4 matrix = node.localMatrix;
		i32 parent = node.parent;
		for (int guard = 0; parent >= 0 && guard < 64; ++guard) {
			const ModelNode& up = parsed.nodes[static_cast<size_t>(parent)];
			matrix = up.localMatrix * matrix;
			parent = up.parent;
		}
		for (const MeshVertex& vertex : parsed.meshes[static_cast<size_t>(node.mesh)].vertices) {
			world.Encapsulate(matrix.TransformPoint(vertex.position));
		}
	}

	Vec3 size = world.max - world.min;
	EXPECT_NEAR(size.y, 0.254f, 1e-3f);
	EXPECT_NEAR(size.x, 0.508f, 1e-3f);
	EXPECT_NEAR(size.z, 0.508f, 1e-3f);
}

TEST(ModelHandednessTest, BothFormatsProduceTheSameChirality) {
	ModelParseResult glTF = ParseGLTF(FixtureFile("BoomBoxWithAxes.gltf"));
	ASSERT_TRUE(glTF.success) << glTF.error;
	ASSERT_FALSE(glTF.meshes.empty());

	std::filesystem::path corpus = RepoDir("External/Submodules/ufbx/data");
	ASSERT_FALSE(corpus.empty());
	ModelParseResult fbx = ParseFBX(Path((corpus / "maya_kenney_character_7700_binary.fbx").generic_string()));
	ASSERT_TRUE(fbx.success) << fbx.error;
	ASSERT_FALSE(fbx.meshes.empty());

	int glTFAgree = 0;
	int glTFDisagree = 0;
	CountWindingAgreement(glTF.meshes[0], ModelMatrixForMesh(glTF, 0), glTFAgree, glTFDisagree);

	int fbxAgree = 0;
	int fbxDisagree = 0;
	const ModelMesh* skin = nullptr;
	i32 skinIndex = -1;
	for (size_t i = 0; i < fbx.meshes.size(); ++i) {
		if (fbx.meshes[i].indices.size() > 1000) {
			skin = &fbx.meshes[i];
			skinIndex = static_cast<i32>(i);
			break;
		}
	}
	ASSERT_NE(skin, nullptr);
	CountWindingAgreement(*skin, ModelMatrixForMesh(fbx, skinIndex), fbxAgree, fbxDisagree);

	EXPECT_GT(glTFAgree, glTFDisagree * 4);
	EXPECT_GT(fbxAgree, fbxDisagree * 4);
}
