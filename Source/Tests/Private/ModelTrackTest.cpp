#include "TestsPCH.h"

#include "FBXParser.h"
#include "glTFParser.h"
#include "ModelImport.h"

#include "Shalltear/Core/StringUtil.h"

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

	Path FixtureFile(const char* name) {
		std::filesystem::path dir = RepoDir("Source/Tests/Fixtures/Graphics");
		return dir.empty() ? Path() : Path((dir / name).generic_string());
	}

	Path CorpusFile(const char* name) {
		std::filesystem::path dir = RepoDir("External/Submodules/ufbx/data");
		return dir.empty() ? Path() : Path((dir / name).generic_string());
	}

	constexpr ModelParseOptions WithTracks{true};

	const ModelTakeRange* FindTake(const ModelParseResult& parsed, std::string_view name) {
		for (const ModelTakeRange& take : parsed.takes) {
			if (take.name == name) {
				return &take;
			}
		}
		return nullptr;
	}

	const ModelTrack* FindTrack(const ModelParseResult& parsed, const ModelTakeRange& take, i32 node, ModelChannel channel) {
		for (i32 index : take.tracks) {
			const ModelTrack& track = parsed.tracks[static_cast<size_t>(index)];
			if (track.node == node && track.channel == channel) {
				return &track;
			}
		}
		return nullptr;
	}

	i32 NodeNamed(const ModelParseResult& parsed, std::string_view name) {
		for (size_t i = 0; i < parsed.nodes.size(); ++i) {
			if (parsed.nodes[i].name == name) {
				return static_cast<i32>(i);
			}
		}
		return -1;
	}

	size_t TotalKeys(const ModelParseResult& parsed) {
		size_t keys = 0;
		for (const ModelTrack& track : parsed.tracks) {
			keys += track.times.size();
		}
		return keys;
	}

	void ExpectWellFormed(const ModelParseResult& parsed, std::string_view what) {
		for (size_t i = 0; i < parsed.tracks.size(); ++i) {
			const ModelTrack& track = parsed.tracks[i];
			ASSERT_GE(track.node, 0) << what << ": track " << i;
			ASSERT_LT(static_cast<size_t>(track.node), parsed.nodes.size()) << what << ": track " << i << " addresses node " << track.node << " of " << parsed.nodes.size();
			ASSERT_FALSE(track.times.empty()) << what << ": track " << i;
			ASSERT_EQ(track.values.size(), track.times.size() * ModelValuesPerKey(track.interp)) << what << ": track " << i;
			for (size_t k = 1; k < track.times.size(); ++k) {
				ASSERT_GE(track.times[k], track.times[k - 1]) << what << ": track " << i << " key " << k;
			}
		}

		Vector<u8> claimed(parsed.tracks.size(), 0);
		for (const ModelTakeRange& take : parsed.takes) {
			for (i32 index : take.tracks) {
				ASSERT_GE(index, 0) << what << ": take '" << take.name << "' names track " << index;
				ASSERT_LT(static_cast<size_t>(index), parsed.tracks.size()) << what << ": take '" << take.name << "' names track " << index << " of " << parsed.tracks.size();
				ASSERT_EQ(claimed[static_cast<size_t>(index)], 0) << what << ": track " << index;
				claimed[static_cast<size_t>(index)] = 1;
			}
		}
	}

	void ExpectSameGeometry(const ModelParseResult& a, const ModelParseResult& b) {
		ASSERT_EQ(a.nodes.size(), b.nodes.size());
		ASSERT_EQ(a.meshes.size(), b.meshes.size());
		ASSERT_EQ(a.materials.size(), b.materials.size());
		ASSERT_EQ(a.images.size(), b.images.size());
		ASSERT_EQ(a.skins.size(), b.skins.size());
		for (size_t i = 0; i < a.nodes.size(); ++i) {
			EXPECT_EQ(a.nodes[i].name, b.nodes[i].name) << "node " << i;
			EXPECT_EQ(a.nodes[i].parent, b.nodes[i].parent) << "node " << i;
			EXPECT_EQ(a.nodes[i].mesh, b.nodes[i].mesh) << "node " << i;
			EXPECT_EQ(a.nodes[i].skin, b.nodes[i].skin) << "node " << i;
			for (u32 e = 0; e < 16; ++e) {
				EXPECT_FLOAT_EQ(a.nodes[i].localMatrix.data[e], b.nodes[i].localMatrix.data[e]) << "node " << i << " element " << e;
			}
		}
		for (size_t i = 0; i < a.meshes.size(); ++i) {
			ASSERT_EQ(a.meshes[i].vertices.size(), b.meshes[i].vertices.size()) << "mesh " << i;
			ASSERT_EQ(a.meshes[i].indices.size(), b.meshes[i].indices.size()) << "mesh " << i;
			ASSERT_EQ(a.meshes[i].influences.size(), b.meshes[i].influences.size()) << "mesh " << i;
			EXPECT_EQ(std::memcmp(a.meshes[i].vertices.data(), b.meshes[i].vertices.data(),
								  a.meshes[i].vertices.size() * sizeof(MeshVertex)),
					  0)
				<< "mesh " << i;
		}
	}

} // anonymous namespace

TEST(FBXTrackTest, TracksOnlyWhenAsked) {
	Path file = CorpusFile("maya_kenney_character_7700_binary.fbx");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult quiet = ParseFBX(file);
	ASSERT_TRUE(quiet.success) << quiet.error;
	EXPECT_TRUE(quiet.tracks.empty()) << quiet.tracks.size() << " curves";
	EXPECT_TRUE(quiet.takes.empty());

	ModelParseResult asked = ParseFBX(file, WithTracks);
	ASSERT_TRUE(asked.success) << asked.error;
	ASSERT_EQ(asked.takes.size(), 1u);
	EXPECT_EQ(asked.takes[0].name, "Take 001");
	EXPECT_EQ(asked.tracks.size(), 177u);
	EXPECT_EQ(TotalKeys(asked), 2305u);
	ExpectWellFormed(asked, "kenney");
}

TEST(FBXTrackTest, TrackRequestChangesNothingElse) {
	Path file = CorpusFile("maya_kenney_character_7700_binary.fbx");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult quiet = ParseFBX(file);
	ModelParseResult asked = ParseFBX(file, WithTracks);
	ASSERT_TRUE(quiet.success && asked.success);
	ExpectSameGeometry(quiet, asked);
}

TEST(FBXTrackTest, MultiTakeFileStatesBoundaries) {
	Path file = CorpusFile("maya_game_sausage_7500_binary_combined.fbx");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult parsed = ParseFBX(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.takes.size(), 3u);
	ExpectWellFormed(parsed, "sausage");

	struct Expected {
		const char* name;
		f32 begin;
		f32 end;
		size_t tracks;
		size_t keys;
	};
	const Expected expected[] = {
		{"wiggle", 0.041667f, 0.833333f, 21, 78},
		{"spin", 0.833333f, 1.666667f, 21, 81},
		{"deform", 1.666667f, 2.500000f, 21, 201},
	};
	for (size_t i = 0; i < std::size(expected); ++i) {
		const ModelTakeRange& take = parsed.takes[i];
		EXPECT_EQ(take.name, expected[i].name);
		EXPECT_NEAR(take.begin, expected[i].begin, 1e-5f) << take.name;
		EXPECT_NEAR(take.end, expected[i].end, 1e-5f) << take.name;
		EXPECT_EQ(take.tracks.size(), expected[i].tracks) << take.name;

		size_t keys = 0;
		f32 firstKey = 0.0f;
		for (i32 index : take.tracks) {
			keys += parsed.tracks[static_cast<size_t>(index)].times.size();
			firstKey = std::max(firstKey, parsed.tracks[static_cast<size_t>(index)].times.front());
		}
		EXPECT_EQ(keys, expected[i].keys) << take.name;
		EXPECT_GE(firstKey, take.begin - 1e-5f) << take.name;
		EXPECT_LE(firstKey, take.end + 1e-5f) << take.name;
	}
}

TEST(FBXTrackTest, TheAnimationFrameIsTheGeometryFrame) {
	Path file = CorpusFile("maya_kenney_character_7700_binary.fbx");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult parsed = ParseFBX(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.takes.size(), 1u);

	size_t compared = 0;
	for (i32 index : parsed.takes[0].tracks) {
		const ModelTrack& track = parsed.tracks[static_cast<size_t>(index)];
		const ModelNode& node = parsed.nodes[static_cast<size_t>(track.node)];
		const Vec4 first = track.values.front();
		switch (track.channel) {
		case ModelChannel::Translation:
			EXPECT_NEAR(first.x, node.position.x, 1e-5f) << node.name;
			EXPECT_NEAR(first.y, node.position.y, 1e-5f) << node.name;
			EXPECT_NEAR(first.z, node.position.z, 1e-5f) << node.name;
			break;
		case ModelChannel::Rotation: {
			const f32 dot = first.x * node.rotation.x + first.y * node.rotation.y +
							first.z * node.rotation.z + first.w * node.rotation.w;
			EXPECT_NEAR(std::abs(dot), 1.0f, 1e-4f) << node.name;
			break;
		}
		case ModelChannel::Scale:
			EXPECT_NEAR(first.x, node.scale.x, 1e-5f) << node.name;
			EXPECT_NEAR(first.y, node.scale.y, 1e-5f) << node.name;
			EXPECT_NEAR(first.z, node.scale.z, 1e-5f) << node.name;
			break;
		case ModelChannel::MorphWeight:
			continue;
		}
		++compared;
	}
	EXPECT_EQ(compared, 177u);
}

TEST(FBXTrackTest, BakedRotationStaysInFrame) {
	Path file = CorpusFile("blender_293_barbarian_7400_binary.fbx");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult parsed = ParseFBX(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_FALSE(parsed.takes.empty());

	f32 worstDegrees = 0.0f;
	size_t worstKey = 0;
	str worstNode;
	size_t steps = 0;

	for (const ModelTrack& track : parsed.tracks) {
		if (track.channel != ModelChannel::Rotation) {
			continue;
		}
		for (size_t k = 1; k < track.values.size(); ++k) {
			const Vec4& before = track.values[k - 1];
			const Vec4& after = track.values[k];
			const f32 dot = std::min(1.0f, std::abs(before.x * after.x + before.y * after.y +
													before.z * after.z + before.w * after.w));
			const f32 degrees = 2.0f * std::acos(dot) * 57.2957795f;
			++steps;
			if (degrees > worstDegrees) {
				worstDegrees = degrees;
				worstKey = k;
				worstNode = parsed.nodes[static_cast<size_t>(track.node)].name;
			}
		}
	}

	EXPECT_LT(worstDegrees, 30.0f) << "'" << worstNode << "' turns " << worstDegrees << ' ' << (worstKey - 1) << " and " << worstKey;
	EXPECT_GE(steps, 800u);
}

TEST(FBXTrackTest, AConstantChannelCollapsesToOneKey) {
	Path file = CorpusFile("maya_kenney_character_7700_binary.fbx");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult parsed = ParseFBX(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.takes.size(), 1u);

	size_t oneKeyScale = 0;
	size_t manyKeyChannels = 0;
	for (i32 index : parsed.takes[0].tracks) {
		const ModelTrack& track = parsed.tracks[static_cast<size_t>(index)];
		if (track.channel == ModelChannel::Scale && track.times.size() == 1) {
			++oneKeyScale;
		}
		if (track.times.size() > 1) {
			++manyKeyChannels;
		}
	}
	EXPECT_EQ(oneKeyScale, 14u);
	EXPECT_GT(manyKeyChannels, 0u);
}

TEST(FBXTrackTest, StaticFileHasNoTake) {
	Path file = CorpusFile("blender_279_default_7400_binary.fbx");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult parsed = ParseFBX(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	EXPECT_TRUE(parsed.takes.empty());
	EXPECT_TRUE(parsed.tracks.empty());
}

TEST(FBXTrackTest, CorpusSurvivesTrackRequest) {
	std::filesystem::path dir = RepoDir("External/Submodules/ufbx/data");
	ASSERT_FALSE(dir.empty());

	size_t parsed = 0;
	size_t withTakes = 0;
	size_t multiTake = 0;
	size_t meshless = 0;
	std::error_code ec;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec)) {
		if (!entry.is_regular_file(ec)) {
			continue;
		}
		str narrow;
		try {
			narrow = entry.path().generic_string();
		} catch (const std::exception&) {
			continue;
		}
		if (StringUtil::ToLower(Path(narrow).GetExtension()) != ".fbx") {
			continue;
		}
		ModelParseResult result = ParseFBX(Path(narrow), WithTracks);
		if (!result.success) {
			continue;
		}
		if (result.meshes.empty()) {
			++meshless;
		}
		++parsed;
		if (!result.takes.empty()) {
			++withTakes;
		}
		if (result.takes.size() > 1) {
			++multiTake;
		}
		ExpectWellFormed(result, narrow);
		if (::testing::Test::HasFatalFailure()) {
			return;
		}
	}
	EXPECT_GE(parsed, 668u);
	EXPECT_GE(withTakes, 411u);
	EXPECT_GE(multiTake, 9u);
	EXPECT_GE(meshless, 60u) << meshless;
}

TEST(glTFTrackTest, TracksOnlyWhenAsked) {
	Path file = FixtureFile("Fox.glb");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult quiet = ParseGLTF(file);
	ASSERT_TRUE(quiet.success) << quiet.error;
	EXPECT_TRUE(quiet.tracks.empty());
	EXPECT_TRUE(quiet.takes.empty());

	ModelParseResult asked = ParseGLTF(file, WithTracks);
	ASSERT_TRUE(asked.success) << asked.error;
	EXPECT_FALSE(asked.tracks.empty());
	ExpectSameGeometry(quiet, asked);
}

TEST(glTFTrackTest, EachAnimationBecomesOneTake) {
	Path file = FixtureFile("Fox.glb");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult parsed = ParseGLTF(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.takes.size(), 3u);
	ExpectWellFormed(parsed, "Fox.glb");

	const char* names[] = {"Survey", "Walk", "Run"};
	size_t total = 0;
	for (size_t i = 0; i < 3; ++i) {
		const ModelTakeRange* take = FindTake(parsed, names[i]);
		ASSERT_NE(take, nullptr) << names[i];
		EXPECT_EQ(take->tracks.size(), 21u) << names[i];
		EXPECT_FLOAT_EQ(take->begin, 0.0f) << names[i];
		EXPECT_GT(take->end, 0.0f) << names[i];
		total += take->tracks.size();
		for (i32 index : take->tracks) {
			const ModelTrack& track = parsed.tracks[static_cast<size_t>(index)];
			EXPECT_EQ(track.interp, ModelInterpolation::Linear) << names[i];
			EXPECT_NE(track.channel, ModelChannel::Scale) << names[i];
		}
	}
	EXPECT_EQ(parsed.tracks.size(), total);
	EXPECT_NEAR(FindTake(parsed, "Survey")->end, 3.4166667f, 1e-5f);
}

TEST(glTFTrackTest, CurveIsMirroredWithNodeItDrives) {
	Path file = FixtureFile("animated.gltf");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult parsed = ParseGLTF(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	const i32 node = NodeNamed(parsed, "Mirror");
	ASSERT_GE(node, 0);
	const ModelNode& mirrored = parsed.nodes[static_cast<size_t>(node)];

	EXPECT_FLOAT_EQ(mirrored.position.x, -1.0f);

	const ModelTakeRange* take = FindTake(parsed, "Parity");
	ASSERT_NE(take, nullptr);

	const ModelTrack* translation = FindTrack(parsed, *take, node, ModelChannel::Translation);
	ASSERT_NE(translation, nullptr);
	ASSERT_EQ(translation->times.size(), 1u);
	EXPECT_FLOAT_EQ(translation->values[0].x, mirrored.position.x);
	EXPECT_FLOAT_EQ(translation->values[0].y, mirrored.position.y);
	EXPECT_FLOAT_EQ(translation->values[0].z, mirrored.position.z);

	const ModelTrack* rotation = FindTrack(parsed, *take, node, ModelChannel::Rotation);
	ASSERT_NE(rotation, nullptr);
	const Vec4 q = rotation->values[0];
	const f32 dot = q.x * mirrored.rotation.x + q.y * mirrored.rotation.y + q.z * mirrored.rotation.z + q.w * mirrored.rotation.w;
	EXPECT_NEAR(std::abs(dot), 1.0f, 1e-5f);
	EXPECT_NEAR(std::abs(q.x), 0.5f, 1e-5f);
	EXPECT_NEAR(q.y, -q.x, 1e-5f);
	EXPECT_NEAR(q.z, -q.x, 1e-5f);

	const ModelTrack* scale = FindTrack(parsed, *take, node, ModelChannel::Scale);
	ASSERT_NE(scale, nullptr);
	EXPECT_FLOAT_EQ(scale->values[0].x, 2.0f);
	EXPECT_FLOAT_EQ(scale->values[0].y, 3.0f);
	EXPECT_FLOAT_EQ(scale->values[0].z, 4.0f);
}

TEST(glTFTrackTest, InterpolationKindsKeepLayout) {
	Path file = FixtureFile("animated.gltf");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult parsed = ParseGLTF(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	ExpectWellFormed(parsed, "animated.gltf");

	const ModelTakeRange* kinds = FindTake(parsed, "Kinds");
	ASSERT_NE(kinds, nullptr);

	const ModelTrack* step = FindTrack(parsed, *kinds, NodeNamed(parsed, "Stepper"), ModelChannel::Translation);
	ASSERT_NE(step, nullptr);
	EXPECT_EQ(step->interp, ModelInterpolation::Step);
	ASSERT_EQ(step->times.size(), 2u);
	ASSERT_EQ(step->values.size(), 2u);
	EXPECT_FLOAT_EQ(step->values[1].x, -10.0f);

	const ModelTrack* spline = FindTrack(parsed, *kinds, NodeNamed(parsed, "Spliner"), ModelChannel::Translation);
	ASSERT_NE(spline, nullptr);
	EXPECT_EQ(spline->interp, ModelInterpolation::CubicSpline);
	ASSERT_EQ(spline->times.size(), 2u);
	ASSERT_EQ(spline->values.size(), 6u);
	EXPECT_EQ(ModelValuesPerKey(spline->interp), 3u);
	EXPECT_FLOAT_EQ(spline->values[4].x, -4.0f);
	EXPECT_FLOAT_EQ(spline->values[4].y, 5.0f);
	EXPECT_FLOAT_EQ(spline->values[2].x, -1.0f);
}

TEST(glTFTrackTest, UndrivableChannelDropped) {
	Path file = FixtureFile("animated.gltf");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult parsed = ParseGLTF(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;

	const ModelTakeRange* kinds = FindTake(parsed, "Kinds");
	ASSERT_NE(kinds, nullptr);
	EXPECT_EQ(kinds->tracks.size(), 2u);

	EXPECT_EQ(NodeNamed(parsed, "Unreachable"), -1);
}

TEST(glTFTrackTest, AnUnnamedAnimationStillNamesItsTake) {
	Path file = FixtureFile("SimpleSkin.gltf");
	ASSERT_FALSE(file.ToString().empty());

	ModelParseResult parsed = ParseGLTF(file, WithTracks);
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.takes.size(), 1u);
	EXPECT_EQ(parsed.takes[0].name, "animation0");
	EXPECT_FALSE(parsed.takes[0].tracks.empty());
	ExpectWellFormed(parsed, "SimpleSkin.gltf");
}

TEST(ModelTrackTest, OptionReachesBothFormatsThroughNeutralSeam) {
	Path fbx = CorpusFile("maya_kenney_character_7700_binary.fbx");
	Path glTF = FixtureFile("Fox.glb");
	ASSERT_FALSE(fbx.ToString().empty());
	ASSERT_FALSE(glTF.ToString().empty());

	EXPECT_TRUE(ParseModel(fbx).takes.empty());
	EXPECT_TRUE(ParseModel(glTF).takes.empty());

	ModelParseResult askedFBX = ParseModel(fbx, WithTracks);
	ModelParseResult askedGLTF = ParseModel(glTF, WithTracks);
	ASSERT_TRUE(askedFBX.success) << askedFBX.error;
	ASSERT_TRUE(askedGLTF.success) << askedGLTF.error;
	EXPECT_EQ(askedFBX.takes.size(), 1u);
	EXPECT_EQ(askedGLTF.takes.size(), 3u);
}
