#include "TestsPCH.h"

#include "TestImages.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/SpriteAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UIDocument.h"
#include "Shalltear/Core/UI/WidgetRegistry.h"

#include "Shalltear/Editor/UIDocumentOps.h"

#include <filesystem>
#include <vector>

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		return desc;
	}

	bool Parses(std::string_view text, str& error) {
		UIDocumentAsset document;
		return document.Parse(text, error);
	}

	void AddSprite(JSONValue& list, const char* id, const char* name, std::initializer_list<f64> rect) {
		JSONValue entry = JSONValue::MakeObject();
		entry.AddMember("id", JSONValue::MakeString(id));
		entry.AddMember("name", JSONValue::MakeString(name));
		JSONValue values = JSONValue::MakeArray();
		for (f64 value : rect) {
			values.Push(JSONValue::MakeNumber(value));
		}
		entry.AddMember("rect", values);
		list.Push(entry);
	}

	constexpr std::string_view SpriteOnly[] = {"SpriteAsset"};
	constexpr UI::WidgetAttribute IconAttributes[] = {
		{"icon", UI::WidgetAttributeType::Asset, {}, SpriteOnly},
	};
	const int ICON_OWNER = 0;
	const Asset* _iconReceived = nullptr;

	UI::WidgetResult MakeIcon(UI::Context& context, UI::ElementHandle parent, const UI::WidgetArgs& args) {
		_iconReceived = args.GetAsset("icon");
		UI::ElementHandle element = context.CreateElement();
		context.AppendChild(parent, element);
		return {element, {}};
	}

	class UIImageDocumentFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		std::vector<Asset*> _held;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_uiimagedoc_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			std::vector<u8> pixels(32 * 16 * 4, 255);
			TestImages::WriteTGA(_root / "Atlas.tga", 32, 16, pixels.data());
			ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(Path("project://Atlas.tga")).IsValid());
			JSONValue list = JSONValue::MakeArray();
			AddSprite(list, "frame", "Frame", {0.0, 0.0, 16.0, 16.0});
			AddSprite(list, "star", "Star", {16.0, 0.0, 16.0, 16.0});
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("compression", JSONValue::MakeString("none"));
			settings.AddMember("sprites", list);
			ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://Atlas.tga"), settings));
		}

		void TearDown() override {
			for (Asset* asset : _held) {
				AssetDatabase::UnloadAsset(asset);
			}
			UI::WidgetRegistry::UnregisterOwnedBy(&ICON_OWNER);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void Write(const char* name, std::string_view text) {
			ASSERT_TRUE(FileSystem::WriteTextFile(Path((_root / name).generic_string()), str(text)));
			ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(Path(str("project://") + name)).IsValid());
		}

		UIDocumentAsset* Load(const char* name) {
			Asset* loaded = AssetDatabase::LoadAsset(Path(str("project://") + name));
			if (!loaded || loaded->GetAssetTypeId() != UIDocumentAsset::GetReflectType()->name) {
				return nullptr;
			}
			_held.push_back(loaded);
			return static_cast<UIDocumentAsset*>(loaded);
		}

		static Guid SpriteGuid(const char* key) {
			Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
			AssetDatabase::GetProducts(AssetDatabase::LookupGuid(Path("project://Atlas.tga")), products);
			for (const AssetProduct& product : products) {
				if (product.key == key) {
					return product.guid;
				}
			}
			return {};
		}

		static const UI::ImageDesc* ImageOf(UI::Context& ctx, const UIDocumentAsset& document, std::string_view id) {
			Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Asset)};
			document.Instantiate(ctx, ctx.GetRoot(), {}, mapping);
			for (const UIInstantiateMapping& entry : mapping) {
				if (entry.node->id == id) {
					return ctx.GetImage(entry.element);
				}
			}
			return nullptr;
		}
	};

} // anonymous namespace

TEST(UIImageDocumentTest, ChoiceRefusesUnknownSpelling) {
	str error;
	EXPECT_FALSE(Parses(R"(<ui version="1"><image scale-mode="stretch"/></ui>)", error));
	EXPECT_NE(error.find("'stretch-to-fill'"), str::npos);
	EXPECT_TRUE(Parses(R"(<ui version="1"><image scale-mode="stretch-to-fill"/></ui>)", error)) << error;
}

TEST(UIImageDocumentTest, FillOriginMustFitMethod) {
	str error;
	EXPECT_FALSE(Parses(R"(<ui version="1"><image fill-method="horizontal" fill-origin="top"/></ui>)", error));
	EXPECT_NE(error.find("fill-origin"), str::npos);
	EXPECT_TRUE(Parses(R"(<ui version="1"><image fill-method="radial-90" fill-origin="top-right"/></ui>)", error)) << error;
	EXPECT_TRUE(Parses(R"(<ui version="1"><image fill-method="horizontal"/></ui>)", error)) << error;
}

TEST(UIImageDocumentTest, FillAmountOutsideRangeRefused) {
	str error;
	EXPECT_FALSE(Parses(R"(<ui version="1"><image fill-amount="1.5"/></ui>)", error));
	EXPECT_NE(error.find("fill-amount"), str::npos);
	EXPECT_FALSE(Parses(R"(<ui version="1"><image fill-amount="-0.1"/></ui>)", error));
}

TEST(UIImageDocumentTest, SliceScaleMustBeNonNegative) {
	str error;
	EXPECT_FALSE(Parses(R"(<ui version="1"><image slice-scale="-0.5"/></ui>)", error));
	EXPECT_NE(error.find("slice-scale"), str::npos);
	EXPECT_FALSE(Parses(R"(<ui version="1"><image slice-scale="inf"/></ui>)", error));
	EXPECT_NE(error.find("slice-scale"), str::npos);
	EXPECT_FALSE(Parses(R"(<ui version="1"><image slice-scale="thick"/></ui>)", error));
	EXPECT_TRUE(Parses(R"(<ui version="1"><image slice-scale="0"/></ui>)", error)) << error;
	EXPECT_TRUE(Parses(R"(<ui version="1"><image slice-scale="2.5"/></ui>)", error)) << error;
}

TEST(UIImageDocumentTest, BuilderSeedsUnitSliceScale) {
	const UIDocumentNode image = UIDocumentOps::MakeElement("image");
	str seeded;
	for (const UIDocumentAttribute& attribute : image.attributes) {
		if (attribute.name == "slice-scale") {
			seeded = attribute.value;
		}
	}
	EXPECT_EQ(seeded, "1");
}

TEST(UIImageDocumentTest, ImageRefusesChildren) {
	str error;
	EXPECT_FALSE(Parses(R"(<ui version="1"><panel><image><panel/></image></panel></ui>)", error));
	EXPECT_NE(error.find("<image"), str::npos);
}

TEST(UIImageDocumentTest, TintIsAColor) {
	str error;
	EXPECT_FALSE(Parses(R"(<ui version="1"><image tint="red"/></ui>)", error));
	EXPECT_TRUE(Parses(R"(<ui version="1"><image tint="#ff000080"/></ui>)", error)) << error;
}

TEST(UIImageDocumentTest, TintReachesElement) {
	UIDocumentAsset document;
	str error;
	ASSERT_TRUE(document.Parse(R"(<ui version="1"><image id="i" tint="#00ff00"/></ui>)", error)) << error;
	UI::Context ctx(Desc(), nullptr, nullptr);
	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Asset)};
	document.Instantiate(ctx, ctx.GetRoot(), {}, mapping);
	ASSERT_EQ(mapping.size(), 1u);
	const UI::Element* element = ctx.GetElement(mapping[0].element);
	ASSERT_NE(element, nullptr);
	EXPECT_EQ(element->tint, Color::FromHex("#00ff00"));
	EXPECT_FALSE(ctx.HasImage(mapping[0].element));
}

TEST_F(UIImageDocumentFixture, SpriteResolvesByName) {
	Write("Hud.slxml", R"(<ui version="1"><panel><image id="hp" src="project://Atlas.tga#Frame" scale-mode="sliced" fill-method="horizontal" fill-amount="0.5"/></panel></ui>)");
	UIDocumentAsset* document = Load("Hud.slxml");
	ASSERT_NE(document, nullptr);
	ASSERT_EQ(document->assets.size(), 1u);
	ASSERT_NE(document->assets[0].asset.Get(), nullptr);
	EXPECT_EQ(document->assets[0].asset->assetId, SpriteGuid("Sprites/frame"));

	UI::Context ctx(Desc(), nullptr, nullptr);
	const UI::ImageDesc* image = ImageOf(ctx, *document, "hp");
	ASSERT_NE(image, nullptr);
	ASSERT_NE(image->sprite.Get(), nullptr);
	EXPECT_EQ(image->sprite->assetId, SpriteGuid("Sprites/frame"));
	EXPECT_EQ(image->scaleMode, UI::ImageScaleMode::Sliced);
	EXPECT_EQ(image->fill.method, UI::ImageFillMethod::Horizontal);
	EXPECT_EQ(image->fill.origin, UI::ImageFillOrigin::Left);
	EXPECT_FLOAT_EQ(image->fill.amount, 0.5f);
}

TEST_F(UIImageDocumentFixture, SliceScaleReachesImage) {
	Write("Hud.slxml", R"(<ui version="1"><panel><image id="thin" src="project://Atlas.tga#Frame" scale-mode="sliced" slice-scale="0.5"/><image id="plain" src="project://Atlas.tga#Frame" scale-mode="sliced"/></panel></ui>)");
	UIDocumentAsset* document = Load("Hud.slxml");
	ASSERT_NE(document, nullptr);
	UI::Context ctx(Desc(), nullptr, nullptr);
	const UI::ImageDesc* thin = ImageOf(ctx, *document, "thin");
	ASSERT_NE(thin, nullptr);
	EXPECT_FLOAT_EQ(thin->sliceScale, 0.5f);
	UI::Context other(Desc(), nullptr, nullptr);
	const UI::ImageDesc* plain = ImageOf(other, *document, "plain");
	ASSERT_NE(plain, nullptr);
	EXPECT_FLOAT_EQ(plain->sliceScale, 1.0f);
}

TEST_F(UIImageDocumentFixture, ContentVersionFollowsPixelsPerUnit) {
	Write("Hud.slxml", R"(<ui version="1"><panel><image src="project://Atlas.tga#Star"/></panel></ui>)");
	UIDocumentAsset* document = Load("Hud.slxml");
	ASSERT_NE(document, nullptr);
	const u64 before = document->GetContentVersion();

	JSONValue list = JSONValue::MakeArray();
	AddSprite(list, "frame", "Frame", {0.0, 0.0, 16.0, 16.0});
	AddSprite(list, "star", "Star", {16.0, 0.0, 16.0, 16.0});
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("compression", JSONValue::MakeString("none"));
	settings.AddMember("sprites", list);
	settings.AddMember("pixelsPerUnit", JSONValue::MakeNumber(200.0));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://Atlas.tga"), settings));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Atlas.tga")));

	EXPECT_NE(document->GetContentVersion(), before);
	const SpriteAsset* star = static_cast<const SpriteAsset*>(document->assets[0].asset.Get());
	ASSERT_NE(star->GetTexture(), nullptr);
	EXPECT_EQ(star->GetTexture()->settings.pixelsPerUnit, 200.0f);
}

TEST_F(UIImageDocumentFixture, PathWithoutNameIsWholeTexture) {
	Write("Hud.slxml", R"(<ui version="1"><panel><image id="bg" src="project://Atlas.tga"/></panel></ui>)");
	UIDocumentAsset* document = Load("Hud.slxml");
	ASSERT_NE(document, nullptr);
	UI::Context ctx(Desc(), nullptr, nullptr);
	const UI::ImageDesc* image = ImageOf(ctx, *document, "bg");
	ASSERT_NE(image, nullptr);
	EXPECT_FALSE(image->sprite.IsValid());
	ASSERT_NE(image->texture.Get(), nullptr);
	EXPECT_EQ(image->texture->assetId, AssetDatabase::LookupGuid(Path("project://Atlas.tga")));
}

TEST_F(UIImageDocumentFixture, SameSourceHeldOnce) {
	Write("Hud.slxml", R"(<ui version="1"><panel><image src="project://Atlas.tga#Star"/><image src="project://Atlas.tga#Star"/><image src="project://Atlas.tga#Frame"/></panel></ui>)");
	UIDocumentAsset* document = Load("Hud.slxml");
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(document->assets.size(), 2u);
}

TEST_F(UIImageDocumentFixture, UnresolvedSourceRefusesDocument) {
	Write("Missing.slxml", R"(<ui version="1"><panel><image src="project://Nope.png"/></panel></ui>)");
	EXPECT_EQ(Load("Missing.slxml"), nullptr);
	Write("NoSprite.slxml", R"(<ui version="1"><panel><image src="project://Atlas.tga#Nope"/></panel></ui>)");
	EXPECT_EQ(Load("NoSprite.slxml"), nullptr);
	Write("Sheet.slss", ".hud { width: 10px; }");
	Write("Wrong.slxml", R"(<ui version="1"><panel><image src="project://Sheet.slss"/></panel></ui>)");
	EXPECT_EQ(Load("Wrong.slxml"), nullptr);
}

TEST_F(UIImageDocumentFixture, DocumentHoldsSpriteResident) {
	Write("Hud.slxml", R"(<ui version="1"><panel><image src="project://Atlas.tga#Star"/></panel></ui>)");
	UIDocumentAsset* document = Load("Hud.slxml");
	ASSERT_NE(document, nullptr);
	const Guid star = SpriteGuid("Sprites/star");
	AssetDatabase::CollectGarbage();
	EXPECT_NE(AssetDatabase::FindLoadedByGuid(star), nullptr);

	_held.clear();
	AssetDatabase::UnloadAsset(document);
	for (u32 pass = 0; pass < 4 && AssetDatabase::CollectGarbage() > 0; ++pass) {
	}
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(star), nullptr);
}

TEST_F(UIImageDocumentFixture, OverrideSwapsTemplatesSprite) {
	Write("Slot.slxml", R"(<ui version="1"><panel><image id="icon" src="project://Atlas.tga#Frame"/></panel></ui>)");
	Write("Bar.slxml", R"(<ui version="1"><template src="project://Slot.slxml" name="Slot"/><panel>)"
					   R"(<instance id="a" template="Slot"/>)"
					   R"(<instance id="b" template="Slot"><attribute-overrides element-id="icon" src="project://Atlas.tga#Star"/></instance>)"
					   R"(</panel></ui>)");
	UIDocumentAsset* document = Load("Bar.slxml");
	ASSERT_NE(document, nullptr);
	ASSERT_EQ(document->assets.size(), 1u);

	UI::Context ctx(Desc(), nullptr, nullptr);
	Vector<UIInstantiateMapping> mapping{GetMemoryResource(MemTag::Asset)};
	document->Instantiate(ctx, ctx.GetRoot(), {}, mapping);
	std::vector<Guid> sprites;
	for (const UIInstantiateMapping& entry : mapping) {
		if (entry.node->tag == "image") {
			const UI::ImageDesc* image = ctx.GetImage(entry.element);
			ASSERT_NE(image, nullptr);
			sprites.push_back(image->sprite.Get() ? image->sprite->assetId : Guid{});
		}
	}
	ASSERT_EQ(sprites.size(), 2u);
	EXPECT_EQ(sprites[0], SpriteGuid("Sprites/frame"));
	EXPECT_EQ(sprites[1], SpriteGuid("Sprites/star"));
}

TEST_F(UIImageDocumentFixture, OverrideToMissingSpriteRefused) {
	Write("Slot.slxml", R"(<ui version="1"><panel><image id="icon" src="project://Atlas.tga#Frame"/></panel></ui>)");
	Write("Bar.slxml", R"(<ui version="1"><template src="project://Slot.slxml" name="Slot"/><panel>)"
					   R"(<instance template="Slot"><attribute-overrides element-id="icon" src="project://Atlas.tga#Gone"/></instance>)"
					   R"(</panel></ui>)");
	EXPECT_EQ(Load("Bar.slxml"), nullptr);
}

TEST_F(UIImageDocumentFixture, ContentVersionFollowsSpriteEdit) {
	Write("Hud.slxml", R"(<ui version="1"><panel><image src="project://Atlas.tga#Star"/></panel></ui>)");
	UIDocumentAsset* document = Load("Hud.slxml");
	ASSERT_NE(document, nullptr);
	const u64 before = document->GetContentVersion();
	EXPECT_EQ(document->GetContentVersion(), before);

	JSONValue list = JSONValue::MakeArray();
	AddSprite(list, "frame", "Frame", {0.0, 0.0, 16.0, 16.0});
	AddSprite(list, "star", "Star", {16.0, 0.0, 8.0, 8.0});
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("compression", JSONValue::MakeString("none"));
	settings.AddMember("sprites", list);
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://Atlas.tga"), settings));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Atlas.tga")));

	EXPECT_NE(document->GetContentVersion(), before);
	const SpriteAsset* star = static_cast<const SpriteAsset*>(document->assets[0].asset.Get());
	ASSERT_NE(star->GetDefinition(), nullptr);
	EXPECT_EQ(star->GetDefinition()->rect, RectI(16, 0, 8, 8));
}

TEST_F(UIImageDocumentFixture, BuilderEditResolvesNewSource) {
	Write("Hud.slxml", R"(<ui version="1"><panel><image id="icon" src="project://Atlas.tga#Frame"/></panel></ui>)");
	UIDocumentAsset* document = Load("Hud.slxml");
	ASSERT_NE(document, nullptr);

	str error;
	ASSERT_TRUE(UIDocumentOps::SetAttribute(*document, 1, "src", "project://Atlas.tga#Star", error)) << error;
	ASSERT_EQ(document->assets.size(), 1u);
	EXPECT_EQ(document->assets[0].asset->assetId, SpriteGuid("Sprites/star"));
	UI::Context ctx(Desc(), nullptr, nullptr);
	const UI::ImageDesc* image = ImageOf(ctx, *document, "icon");
	ASSERT_NE(image, nullptr);
	ASSERT_NE(image->sprite.Get(), nullptr);
	EXPECT_EQ(image->sprite->assetId, SpriteGuid("Sprites/star"));

	EXPECT_FALSE(UIDocumentOps::SetAttribute(*document, 1, "src", "project://Atlas.tga#Nope", error));
	EXPECT_NE(error.find("Nope"), str::npos);
	ASSERT_EQ(document->assets.size(), 1u);
	EXPECT_EQ(document->assets[0].asset->assetId, SpriteGuid("Sprites/star"));
	EXPECT_NE(document->Write().find("#Star"), str::npos);
}

TEST_F(UIImageDocumentFixture, CustomWidgetGetsDeclaredAsset) {
	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"item-icon", &MakeIcon, IconAttributes}, &ICON_OWNER));
	Write("Hud.slxml", R"(<ui version="1"><panel><image src="project://Atlas.tga#Star"/><item-icon icon="project://Atlas.tga#Star"/></panel></ui>)");
	UIDocumentAsset* document = Load("Hud.slxml");
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(document->assets.size(), 1u);

	UI::Context ctx(Desc(), nullptr, nullptr);
	_iconReceived = nullptr;
	document->Instantiate(ctx, ctx.GetRoot());
	ASSERT_NE(_iconReceived, nullptr);
	EXPECT_EQ(_iconReceived->assetId, SpriteGuid("Sprites/star"));
}

TEST_F(UIImageDocumentFixture, RepeatedSourceCheckedPerWidget) {
	ASSERT_TRUE(UI::WidgetRegistry::Register(UI::WidgetDesc{"item-icon", &MakeIcon, IconAttributes}, &ICON_OWNER));
	for (std::string_view body : {R"(<image src="project://Atlas.tga"/><item-icon icon="project://Atlas.tga"/>)",
								  R"(<item-icon icon="project://Atlas.tga"/><image src="project://Atlas.tga"/>)"}) {
		UIDocumentAsset document;
		str error;
		ASSERT_TRUE(document.Parse(str(R"(<ui version="1"><panel>)") + str(body) + str("</panel></ui>"), error)) << error;
		EXPECT_FALSE(document.ResolveAssetReferences(error)) << body;
		EXPECT_NE(error.find("<item-icon>"), str::npos) << error;
		EXPECT_NE(error.find("SpriteAsset"), str::npos) << error;
		EXPECT_TRUE(document.assets.empty());
	}

	Write("Hud.slxml", R"(<ui version="1"><panel><image src="project://Atlas.tga"/><item-icon icon="project://Atlas.tga#Star"/></panel></ui>)");
	UIDocumentAsset* document = Load("Hud.slxml");
	ASSERT_NE(document, nullptr);
	ASSERT_EQ(document->assets.size(), 2u);
	str error;
	EXPECT_FALSE(UIDocumentOps::SetAttribute(*document, 2, "icon", "project://Atlas.tga", error));
	EXPECT_NE(error.find("SpriteAsset"), str::npos) << error;
	EXPECT_EQ(document->assets.size(), 2u);

	UI::Context ctx(Desc(), nullptr, nullptr);
	_iconReceived = nullptr;
	document->Instantiate(ctx, ctx.GetRoot());
	ASSERT_NE(_iconReceived, nullptr);
	EXPECT_EQ(_iconReceived->assetId, SpriteGuid("Sprites/star"));
}
