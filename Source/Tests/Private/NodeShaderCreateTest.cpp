#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "GraphSurface.h"
#include "NodeShaderImporter.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/GraphDocument.h"

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	const AssetBrowserOps::CreateMenuEntry* FindEntry(const Vector<AssetBrowserOps::CreateMenuEntry>& entries, std::string_view label) {
		for (const AssetBrowserOps::CreateMenuEntry& entry : entries) {
			if (entry.label == label) {
				return &entry;
			}
		}
		return nullptr;
	}

	str TargetOf(const GraphDocument& document) {
		for (const GraphSetting& setting : document.settings) {
			if (setting.key == "target") {
				return setting.value;
			}
		}
		return {};
	}

} // anonymous namespace

namespace {

	class NodeShaderCreateFixture : public MaterialShadingFixture {};

} // anonymous namespace

TEST_F(NodeShaderCreateFixture, CreateMenuOffersTargets) {
	Vector<AssetBrowserOps::CreateMenuEntry> entries{GetMemoryResource(MemTag::Editor)};
	AssetBrowserOps::GetCreateMenuEntries(entries);

	const AssetBrowserOps::CreateMenuEntry* lit = FindEntry(entries, "Node Shader/Lit");
	const AssetBrowserOps::CreateMenuEntry* unlit = FindEntry(entries, "Node Shader/Unlit");
	ASSERT_NE(lit, nullptr);
	ASSERT_NE(unlit, nullptr);
	EXPECT_EQ(lit->typeId, StringHash("ShaderAsset"));
	EXPECT_EQ(lit->fileName, "New Node Shader.slshader");
	EXPECT_NE(lit->templateIndex, unlit->templateIndex);
	ASSERT_NE(FindEntry(entries, "Shader"), nullptr);
	EXPECT_EQ(FindEntry(entries, "Shader")->fileName, "New Shader.hlsl");
}

TEST(NodeShaderCreateTest, EachTemplateIsGraphThatGeneratesTarget) {
	for (u32 index = 0; index < 2; ++index) {
		GraphDocument document;
		str error;
		ASSERT_TRUE(GraphDocumentOps::Read(NodeShaderImporter::TemplateDocument(index), document, error)) << error;
		EXPECT_EQ(document.type, GraphSurface::DOCUMENT_TYPE);
		ASSERT_EQ(document.contexts.size(), 2u);
		EXPECT_EQ(document.contexts[0].node.def, "Context/Vertex");
		EXPECT_EQ(document.contexts[1].node.def, "Context/Fragment");
		ASSERT_EQ(document.contexts[0].blocks.size(), 3u);
		EXPECT_EQ(document.contexts[0].blocks[0].def, "Block/Position");
		EXPECT_EQ(document.contexts[0].blocks[1].def, "Block/Normal");
		EXPECT_EQ(document.contexts[0].blocks[2].def, "Block/Tangent");
		str generated;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		EXPECT_TRUE(GraphSurface::Generate(document, "project://New Node Shader.slshader", generated, diagnostics)) << "template " << index << ' ' << (diagnostics.empty() ? str() : diagnostics[0].message);
	}
	GraphDocument lit;
	GraphDocument unlit;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(NodeShaderImporter::TemplateDocument(0), lit, error));
	ASSERT_TRUE(GraphDocumentOps::Read(NodeShaderImporter::TemplateDocument(1), unlit, error));
	EXPECT_EQ(TargetOf(lit), "\"lit\"");
	EXPECT_EQ(TargetOf(unlit), "\"unlit\"");
	EXPECT_EQ(lit.contexts[1].blocks.size(), 7u);
	EXPECT_EQ(unlit.contexts[1].blocks.size(), 2u);
}

TEST_F(NodeShaderCreateFixture, CreatingGraphWritesTemplateAndImportsShader) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	str unlitPath;
	const Guid unlit = AssetBrowserOps::CreateAsset(Path("project://"), StringHash("ShaderAsset"), "New Node Shader.slshader",
													&unlitPath, 1);
	ASSERT_TRUE(unlit.IsValid());
	EXPECT_EQ(unlitPath, "New Node Shader.slshader");
	const str written = FileSystem::ReadTextFile(Path("project://New Node Shader.slshader"), MemTag::Editor);
	EXPECT_EQ(written, NodeShaderImporter::TemplateDocument(1));

	Asset* shader = AssetDatabase::LoadAssetByGuid(unlit);
	ASSERT_NE(shader, nullptr);
	EXPECT_EQ(shader->GetAssetTypeId(), StringHash("ShaderAsset"));
	EXPECT_EQ(static_cast<ShaderAsset*>(shader)->integrator, ShaderTemplate::Unlit);
	AssetDatabase::UnloadAsset(shader);

	str secondPath;
	ASSERT_TRUE(AssetBrowserOps::CreateAsset(Path("project://"), StringHash("ShaderAsset"), "New Node Shader.slshader", &secondPath, 0)
					.IsValid());
	EXPECT_EQ(secondPath, "New Node Shader 1.slshader");

	str hlslPath;
	ASSERT_TRUE(AssetBrowserOps::CreateAsset(Path("project://"), StringHash("ShaderAsset"), {}, &hlslPath).IsValid());
	EXPECT_EQ(hlslPath, "New Shader.hlsl");
}
