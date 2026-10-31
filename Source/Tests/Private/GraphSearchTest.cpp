#include "TestsPCH.h"
#include "Shalltear/Editor/GraphSearch.h"

using namespace Shalltear;

namespace {

	GraphSearchEntry Entry(std::string_view name, std::string_view category = {}, std::string_view synonyms = {}) {
		GraphSearchEntry entry;
		entry.name = str(name);
		entry.category = str(category);
		entry.synonyms = str(synonyms);
		return entry;
	}

	Vector<str> Ranked(std::string_view query, std::span<const GraphSearchEntry> library) {
		Vector<u32> indices{GetMemoryResource(MemTag::Editor)};
		GraphSearchOps::Rank(query, library, indices);
		Vector<str> names{GetMemoryResource(MemTag::Editor)};
		for (const u32 index : indices) {
			names.push_back(library[index].name);
		}
		return names;
	}

} // anonymous namespace

TEST(GraphSearchOpsTest, EveryTermMustMatch) {
	const GraphSearchEntry library[] = {
		Entry("Sample Texture 2D", "Input/Texture"),
		Entry("Multiply", "Math/Basic", "times product"),
		Entry("Add", "Math/Basic", "plus sum"),
		Entry("SampleGradient", "Input/Gradient"),
	};
	EXPECT_EQ(Ranked("tex", library), (Vector<str>{"Sample Texture 2D"}));
	EXPECT_EQ(Ranked("TIMES", library), (Vector<str>{"Multiply"}));
	EXPECT_EQ(Ranked("gradient", library), (Vector<str>{"SampleGradient"}));
	EXPECT_EQ(Ranked("ply", library), (Vector<str>{"Multiply"}));
	EXPECT_EQ(Ranked("math", library).size(), 2u);
	EXPECT_EQ(Ranked("sample tex", library), (Vector<str>{"Sample Texture 2D"}));
	EXPECT_TRUE(Ranked("zebra", library).empty());
}

TEST(GraphSearchOpsTest, MatchOrderRanking) {
	const GraphSearchEntry library[] = {
		Entry("Pinoiser"),
		Entry("Voronoi", "Procedural/Noise"),
		Entry("Turbulence", "Procedural", "noise"),
		Entry("Gradient Noisey"),
		Entry("Simple Noise"),
		Entry("Noise Sampler"),
	};
	EXPECT_EQ(Ranked("noise", library), (Vector<str>{"Noise Sampler", "Simple Noise", "Gradient Noisey", "Turbulence",
													 "Voronoi", "Pinoiser"}));
}

TEST(GraphSearchOpsTest, TieBreaksByLengthThenOrder) {
	const GraphSearchEntry library[] = {
		Entry("Simple Noise"),
		Entry("Big Noise"),
		Entry("Twin Noise", "A"),
		Entry("Twin Noise", "B"),
	};
	Vector<u32> order{GetMemoryResource(MemTag::Editor)};
	GraphSearchOps::Rank("noise", library, order);
	EXPECT_EQ(order, (Vector<u32>{1, 2, 3, 0}));

	GraphSearchOps::Rank("", library, order);
	EXPECT_EQ(order, (Vector<u32>{0, 1, 2, 3}));
	GraphSearchOps::Rank("   ", library, order);
	EXPECT_EQ(order.size(), 4u);
}

namespace {

	Vector<str> Lit(std::string_view query, std::string_view name) {
		Vector<GraphSearchSpan> spans{GetMemoryResource(MemTag::Editor)};
		GraphSearchOps::Highlights(query, name, spans);
		Vector<str> pieces{GetMemoryResource(MemTag::Editor)};
		for (const GraphSearchSpan& span : spans) {
			pieces.push_back(str(name.substr(span.begin, span.end - span.begin)));
		}
		return pieces;
	}

} // anonymous namespace

TEST(GraphSearchOpsTest, QueryHighlightsEveryOccurrence) {
	EXPECT_EQ(Lit("tex", "Sample Texture 2D"), (Vector<str>{"Tex"}));
	EXPECT_EQ(Lit("SAMPLE tex", "Sample Texture 2D"), (Vector<str>{"Sample", "Tex"}));
	EXPECT_EQ(Lit("SampleTex", "Sample Texture 2D"), (Vector<str>{"Sample", "Tex"}));
	EXPECT_EQ(Lit("sampletex", "Sample Texture 2D"), (Vector<str>{"Sample", "Tex"}));
	EXPECT_EQ(Lit("e", "Sample Texture"), (Vector<str>{"e", "e", "e"}));
	EXPECT_EQ(Lit("ply", "Multiply"), (Vector<str>{"ply"}));
	EXPECT_EQ(Lit("ti tip", "Multiply"), (Vector<str>{"tip"}));
	EXPECT_TRUE(Lit("times", "Multiply").empty());
	EXPECT_TRUE(Lit("", "Multiply").empty());
}

TEST(GraphSearchOpsTest, LastTermCompletesWord) {
	EXPECT_EQ(GraphSearchOps::Completion("mul", "Multiply"), "tiply");
	EXPECT_EQ(GraphSearchOps::Completion("MUL", "Multiply"), "tiply");
	EXPECT_EQ(GraphSearchOps::Completion("sample t", "Sample Texture 2D"), "exture");
	EXPECT_EQ(GraphSearchOps::Completion("Te", "SampleTexture2D"), "xture2D");
	EXPECT_EQ(GraphSearchOps::Completion("s", "Simple Sine"), "imple");
	EXPECT_EQ(GraphSearchOps::Completion("noise", "Noise Noisey"), "");
	EXPECT_EQ(GraphSearchOps::Completion("sample ", "Sample Texture 2D"), "");
	EXPECT_EQ(GraphSearchOps::Completion("ply", "Multiply"), "");
	EXPECT_EQ(GraphSearchOps::Completion("", "Multiply"), "");
}
