#include "InspectorPanel.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorVariantCompiles.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/MaterialInspectorOps.h"
#include "Shalltear/Editor/ObjectField.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include <algorithm>
#include <format>

namespace Shalltear {

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		Color TextNormal() { return EditorTheme::GetColor(ColorToken::Text); }
		Color TextDim() { return EditorTheme::GetColor(ColorToken::TextDim); }
		Color TextStrong() { return EditorTheme::GetColor(ColorToken::TextStrong); }
		f32 FieldFont() { return EditorTheme::GetMetric(MetricToken::FontBody); }

		UI::ElementHandle MakeRow(UI::Context& context, UI::ElementHandle parent, std::string_view label) {
			UI::ElementHandle row = context.CreateElement();
			context.AppendChild(parent, row);
			UI::Style rowStyle;
			rowStyle.flexDirection = UI::FlexDirection::Row;
			rowStyle.alignItems = UI::Align::Center;
			rowStyle.padding = {8.0f, 1.5f, 8.0f, 1.5f};
			rowStyle.width = UI::Length::Percent(1.0f);
			context.SetStyle(row, rowStyle);

			UI::ElementHandle labelElement = context.CreateElement();
			context.AppendChild(row, labelElement);
			UI::Style labelStyle;
			labelStyle.width = UI::Length::Pixel(110.0f);
			labelStyle.flexShrink = 0.0f;
			context.SetStyle(labelElement, labelStyle);
			context.SetHoverable(labelElement, false);
			context.SetText(labelElement, UISystem::GetDefaultFont(), FieldFont(), TextDim(), label);
			return row;
		}

		void NoteMaterialEdited(const Guid& guid) {
			EditorPreviewStore::Invalidate(guid);
		}

		void SaveMaterial(const Guid& guid, MaterialAsset& material) {
			Path path = AssetDatabase::LookupPath(guid);
			if (path.ToString().empty()) {
				SLT_LOG_WARN("Editor", "MaterialInspector: material has no path. Not saved");
				return;
			}
			if (material.Export(FileSystem::Resolve(path))) {
				SLT_LOG_INFO("Editor", "MaterialInspector: saved '{}'", path.ToString());
			} else {
				SLT_LOG_WARN("Editor", "MaterialInspector: failed to write '{}'", path.ToString());
			}
		}

		std::string_view FallbackName(ShaderTextureFallback fallback) {
			switch (fallback) {
			case ShaderTextureFallback::Black:
				return "black";
			case ShaderTextureFallback::Normal:
				return "normal";
			case ShaderTextureFallback::Transparent:
				return "transparent";
			case ShaderTextureFallback::Gray:
				return "gray";
			case ShaderTextureFallback::White:
				break;
			}
			return "white";
		}

		const MaterialAsset::Param* ParamFor(const MaterialAsset& material, std::string_view name) {
			return material.FindParam(StringHash(name));
		}

		void BuildNumberRow(UI::Context& context, UI::ElementHandle row, MaterialAsset* target,
							const Guid& guid, const MaterialInspectorOps::Row& spec) {
			const MaterialAsset::Param* param = ParamFor(*target, spec.name);
			bool integral = spec.member == UniformMemberType::Int || spec.member == UniformMemberType::Bool;
			str name = spec.name;

			UI::Widgets::NumberFieldDesc number;
			number.value = param ? (integral ? static_cast<f64>(param->integer) : static_cast<f64>(param->numbers[0])) : 0.0;
			number.dragScale = integral ? 1.0 : 0.05;
			number.decimals = integral ? 0 : UI::Widgets::DEFAULT_DECIMALS;
			number.font = UISystem::GetDefaultFont();
			number.fontSizePx = FieldFont();
			Guid guidCopy = guid;
			number.onChange = [target, name, guidCopy, integral](f64 value) {
				if (integral) {
					target->SetInt(name, static_cast<i32>(value));
				} else {
					target->SetFloat(name, static_cast<f32>(value));
				}
				NoteMaterialEdited(guidCopy);
			};
			UI::Widgets::NumberField(context, row, number);
		}

		void BuildToggleRow(UI::Context& context, UI::ElementHandle row, MaterialAsset* target, const Guid& guid,
							const MaterialInspectorOps::Row& spec) {
			const MaterialAsset::Param* param = ParamFor(*target, spec.name);
			str name = spec.name;
			Guid guidCopy = guid;
			UI::Widgets::Checkbox(context, row, param && param->integer != 0, [target, name, guidCopy](bool on) {
				target->SetInt(name, on ? 1 : 0);
				NoteMaterialEdited(guidCopy);
			});
		}

		void BuildSliderRow(UI::Context& context, UI::ElementHandle row, MaterialAsset* target,
							const Guid& guid, const MaterialInspectorOps::Row& spec) {
			const MaterialAsset::Param* param = ParamFor(*target, spec.name);
			bool integral = spec.member == UniformMemberType::Int || spec.member == UniformMemberType::Bool;
			str name = spec.name;

			UI::Widgets::SliderDesc slider;
			slider.minValue = spec.minValue;
			slider.maxValue = spec.maxValue;
			slider.value = param ? (integral ? static_cast<f32>(param->integer) : param->numbers[0]) : spec.minValue;
			slider.showValueField = true;
			slider.decimals = integral ? 0 : 2;
			slider.font = UISystem::GetDefaultFont();
			slider.fontSizePx = FieldFont();
			Guid guidCopy = guid;
			slider.onChange = [target, name, guidCopy, integral](f32 value) {
				if (integral) {
					target->SetInt(name, static_cast<i32>(value));
				} else {
					target->SetFloat(name, value);
				}
				NoteMaterialEdited(guidCopy);
			};
			UI::Widgets::Slider(context, row, slider);
		}

		f32 HDRIntensityOf(Color value) {
			f32 peak = std::max({value.r, value.g, value.b});
			return peak > 1.0f ? peak : 1.0f;
		}

		void BuildColorRow(UI::Context& context, UI::ElementHandle row, MaterialAsset* target,
						   const Guid& guid, const MaterialInspectorOps::Row& spec) {
			const MaterialAsset::Param* param = ParamFor(*target, spec.name);
			bool hasAlpha = spec.member == UniformMemberType::Vec4;
			Color stored = param ? Color{param->numbers[0], param->numbers[1], param->numbers[2],
										 hasAlpha ? param->numbers[3] : 1.0f}
								 : Color::Black();
			bool hdr = spec.kind == MaterialInspectorOps::RowKind::HDRColor;
			f32 intensity = hdr ? HDRIntensityOf(stored) : 1.0f;
			Color swatch{stored.r / intensity, stored.g / intensity, stored.b / intensity, stored.a};

			str name = spec.name;
			Guid guidCopy = guid;
			auto write = [target, name, guidCopy, hasAlpha](Color color) {
				if (hasAlpha) {
					target->SetColor(name, color);
				} else {
					target->SetVec3(name, Vec3{color.r, color.g, color.b});
				}
				NoteMaterialEdited(guidCopy);
			};

			UI::Widgets::ColorSwatchField(context, row, swatch, UISystem::GetDefaultFont(), FieldFont(),
										  [write, target, name, hdr](Color picked) {
											  f32 scale = 1.0f;
											  if (hdr) {
												  const MaterialAsset::Param* live = ParamFor(*target, name);
												  scale = live ? HDRIntensityOf(Color{live->numbers[0], live->numbers[1], live->numbers[2], 1.0f}) : 1.0f;
											  }
											  write(Color{picked.r * scale, picked.g * scale, picked.b * scale, picked.a});
										  });

			if (!hdr) {
				return;
			}
			UI::Widgets::NumberFieldDesc number;
			number.value = static_cast<f64>(intensity);
			number.minValue = 1.0;
			number.dragScale = 0.05;
			number.decimals = 2;
			number.label = "x";
			number.font = UISystem::GetDefaultFont();
			number.fontSizePx = FieldFont();
			number.onChange = [write, target, name](f64 value) {
				const MaterialAsset::Param* live = ParamFor(*target, name);
				Color current = live ? Color{live->numbers[0], live->numbers[1], live->numbers[2], live->numbers[3]} : Color::Black();
				f32 scale = HDRIntensityOf(current);
				f32 wanted = static_cast<f32>(value);
				write(Color{current.r / scale * wanted, current.g / scale * wanted, current.b / scale * wanted, current.a});
			};
			UI::Widgets::NumberField(context, row, number);
		}

		void BuildVectorRow(UI::Context& context, UI::ElementHandle row, MaterialAsset* target,
							const Guid& guid, const MaterialInspectorOps::Row& spec) {
			const MaterialAsset::Param* param = ParamFor(*target, spec.name);
			u32 components = UniformMemberFloatCount(spec.member);
			if (components < 2 || components > 4) {
				return;
			}

			UI::Widgets::VectorFieldDesc vector;
			vector.componentCount = components;
			for (u32 i = 0; i < components; ++i) {
				vector.values[i] = param ? static_cast<f64>(param->numbers[i]) : 0.0;
			}
			vector.dragScale = 0.05;
			vector.font = UISystem::GetDefaultFont();
			vector.fontSizePx = FieldFont();
			str name = spec.name;
			Guid guidCopy = guid;
			UniformMemberType kind = spec.member;
			vector.onChange = [target, name, guidCopy, kind, components](u32 index, f64 value) {
				const MaterialAsset::Param* live = ParamFor(*target, name);
				f32 numbers[4] = {};
				if (live) {
					for (u32 i = 0; i < 4; ++i) {
						numbers[i] = live->numbers[i];
					}
				}
				if (index < components) {
					numbers[index] = static_cast<f32>(value);
				}
				switch (kind) {
				case UniformMemberType::Vec2:
					target->SetVec2(name, Vec2{numbers[0], numbers[1]});
					break;
				case UniformMemberType::Vec3:
					target->SetVec3(name, Vec3{numbers[0], numbers[1], numbers[2]});
					break;
				default:
					target->SetColor(name, Color{numbers[0], numbers[1], numbers[2], numbers[3]});
					break;
				}
				NoteMaterialEdited(guidCopy);
			};
			UI::Widgets::VectorField(context, row, vector);
		}

		str TextureCaption(const MaterialAsset& material, std::string_view slot, std::string_view binds) {
			TextureAsset* current = material.GetTexture(StringHash(slot)).Get();
			Path texturePath = current ? AssetDatabase::LookupPath(current->assetId) : Path();
			return texturePath.ToString().empty() ? std::format("None ({})", binds) : texturePath.GetFilename();
		}

		void BuildTextureRow(UI::Context& context, UI::ElementHandle row, MaterialAsset* target,
							 const Guid& guid, const ShaderAsset& shader, const MaterialInspectorOps::Row& spec) {
			str slotName = spec.name;
			Guid guidCopy = guid;

			ObjectField::Desc field;
			const RHI::TextureDimension slotDimension = shader.GetTextureSlotDimension(spec.name);
			str binds = str(FallbackName(shader.GetTextureFallback(spec.name)));
			if (slotDimension != RHI::TextureDimension::Texture2D) {
				binds += " " + str(RHI::TextureDimensionName(slotDimension));
			}
			str caption = TextureCaption(*target, slotName, binds);
			field.caption = caption;
			auto takes = [slotDimension](const UI::DragPayload& payload) { return ObjectField::TakesTexture(payload, slotDimension); };
			field.accepts = takes;
			field.onDrop = [target, slotName, guidCopy, takes](const UI::DragPayload& payload, Vec2) {
				if (!takes(payload)) {
					return;
				}
				target->SetTexture(slotName, payload.data.As<AssetDragData>().guid);
				NoteMaterialEdited(guidCopy);
			};
			field.onClear = [target, slotName, guidCopy]() {
				target->SetTexture(slotName, Guid{});
				NoteMaterialEdited(guidCopy);
			};
			field.resolveTarget = [target, slotName]() -> EditorPingTarget {
				TextureAsset* bound = target->GetTexture(StringHash(std::string_view{slotName})).Get();
				return {.asset = bound ? bound->assetId : Guid{}};
			};
			ObjectField::Parts parts = ObjectField::Build(context, row, field);

			PropertyDrawers::SetRowRefresh(context, row, [target, slotName, binds, captionText = parts.caption](UI::Context& ctx) {
				str now = TextureCaption(*target, slotName, binds);
				if (ctx.GetTextString(captionText) != now) {
					ctx.SetTextString(captionText, now);
				} }, nullptr);
		}

		void EnsureVariantCompiled(MaterialAsset* target, ShaderAsset* shader) {
			if (!shader) {
				return;
			}
			Vector<str> effective{GetMemoryResource(MemTag::Editor)};
			target->GetEffectiveKeywords(effective);
			EditorVariantCompiles::Request(*shader, {effective.data(), effective.size()});
		}

		template <typename Enum, typename Name, typename Read, typename Write>
		void BuildStateOverrideRow(UI::Context& context, UI::ElementHandle row, Name name, Read read, Write write) {
			Vector<str> labels{GetMemoryResource(MemTag::Editor)};
			labels.push_back(std::format("Shader default ({})", name(read.shaderValue)));
			for (u8 i = 0; i < static_cast<u8>(Enum::Count); ++i) {
				labels.push_back(str(name(static_cast<Enum>(i))));
			}
			Vector<std::string_view> items{GetMemoryResource(MemTag::Editor)};
			for (const str& label : labels) {
				items.push_back(std::string_view{label});
			}

			UI::Widgets::DropdownDesc desc;
			desc.items = {items.data(), items.size()};
			desc.selected = read.override ? static_cast<u32>(*read.override) + 1 : 0;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FieldFont();
			desc.onSelect = [write](u32 index) {
				if (index == 0) {
					write(std::optional<Enum>{});
				} else {
					write(std::optional<Enum>{static_cast<Enum>(index - 1)});
				}
			};
			UI::Widgets::Dropdown(context, row, desc);
		}

		void BuildRenderStateRow(UI::Context& context, UI::ElementHandle row, MaterialAsset* target,
								 const Guid& guid, ShaderAsset& shader, const MaterialInspectorOps::Row& spec) {
			Guid guidCopy = guid;
			ShaderAsset* shaderPtr = &shader;
			switch (spec.kind) {
			case MaterialInspectorOps::RowKind::Blend: {
				struct Read {
					RHI::BlendMode shaderValue;
					std::optional<RHI::BlendMode> override;
				} read{shader.renderState.blend, target->blend};
				BuildStateOverrideRow<RHI::BlendMode>(
					context, row, [](RHI::BlendMode mode) { return RHI::BlendModeName(mode); }, read,
					[target, guidCopy](std::optional<RHI::BlendMode> value) {
						target->blend = value;
						NoteMaterialEdited(guidCopy);
					});
				break;
			}
			case MaterialInspectorOps::RowKind::Cull: {
				struct Read {
					RHI::CullMode shaderValue;
					std::optional<RHI::CullMode> override;
				} read{shader.renderState.cull, target->cull};
				BuildStateOverrideRow<RHI::CullMode>(
					context, row, [](RHI::CullMode mode) { return RHI::CullModeName(mode); }, read,
					[target, guidCopy](std::optional<RHI::CullMode> value) {
						target->cull = value;
						NoteMaterialEdited(guidCopy);
					});
				break;
			}
			case MaterialInspectorOps::RowKind::AlphaClip: {
				UI::Widgets::SliderDesc slider;
				slider.minValue = 0.0f;
				slider.maxValue = 1.0f;
				slider.value = target->alphaClipThreshold;
				slider.showValueField = true;
				slider.decimals = 2;
				slider.font = UISystem::GetDefaultFont();
				slider.fontSizePx = FieldFont();
				slider.onChange = [target, shaderPtr, guidCopy](f32 value) {
					target->SetAlphaClipThreshold(value);
					EnsureVariantCompiled(target, shaderPtr);
					NoteMaterialEdited(guidCopy);
				};
				UI::Widgets::Slider(context, row, slider);
				break;
			}
			case MaterialInspectorOps::RowKind::SortingPriority: {
				UI::Widgets::NumberFieldDesc number;
				number.value = static_cast<f64>(target->sortingPriority);
				number.dragScale = 1.0;
				number.decimals = 0;
				number.font = UISystem::GetDefaultFont();
				number.fontSizePx = FieldFont();
				number.onChange = [target, guidCopy](f64 value) {
					target->sortingPriority = static_cast<i32>(value);
					NoteMaterialEdited(guidCopy);
				};
				UI::Widgets::NumberField(context, row, number);
				break;
			}
			default:
				break;
			}
		}

		void BuildKeywordRow(UI::Context& context, UI::ElementHandle row, MaterialAsset* target,
							 const Guid& guid, ShaderAsset& shader, const MaterialInspectorOps::Row& spec) {
			bool enabled = false;
			for (const str& name : target->keywords) {
				enabled = enabled || name == spec.name;
			}

			str keyword = spec.name;
			str member = spec.keywordMember;
			Guid guidCopy = guid;
			ShaderAsset* shaderPtr = &shader;
			UI::Widgets::Checkbox(context, row, enabled, [target, keyword, member, guidCopy, shaderPtr](bool on) {
				Vector<str> next{GetMemoryResource(MemTag::Editor)};
				for (const str& name : target->keywords) {
					if (name != keyword) {
						next.push_back(name);
					}
				}
				if (on) {
					next.push_back(keyword);
				}
				target->SetKeywords({next.data(), next.size()});

				if (!member.empty()) {
					if (const MaterialAsset::Param* param = ParamFor(*target, member);
						param && param->kind == UniformMemberType::Int) {
						target->SetInt(member, on ? 1 : 0);
					} else {
						target->SetFloat(member, on ? 1.0f : 0.0f);
					}
				}

				EnsureVariantCompiled(target, shaderPtr);
				NoteMaterialEdited(guidCopy);
			});
		}

		void BuildKeywordEnumRow(UI::Context& context, UI::ElementHandle row, MaterialAsset* target, const Guid& guid,
								 ShaderAsset& shader, const MaterialInspectorOps::Row& spec) {
			u32 selected = 0;
			if (const MaterialAsset::Param* param = ParamFor(*target, spec.keywordMember)) {
				const f32 holds = param->kind == UniformMemberType::Int ? static_cast<f32>(param->integer) : param->numbers[0];
				selected = holds >= 0.0f && holds < static_cast<f32>(spec.entries.size()) ? static_cast<u32>(std::lround(holds)) : 0u;
			}
			Vector<std::string_view> names{GetMemoryResource(MemTag::Editor)};
			for (u32 i = 0; i < spec.entries.size(); ++i) {
				names.push_back(spec.entries[i].name);
				for (const str& name : target->keywords) {
					selected = name == spec.entries[i].keyword ? i : selected;
				}
			}

			Vector<MaterialInspectorOps::KeywordEntry> entries = spec.entries;
			str member = spec.keywordMember;
			Guid guidCopy = guid;
			ShaderAsset* shaderPtr = &shader;
			UI::Widgets::DropdownDesc desc;
			desc.items = {names.data(), names.size()};
			desc.selected = selected;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FieldFont();
			desc.onSelect = [target, entries, member, guidCopy, shaderPtr](u32 index) {
				if (index >= entries.size()) {
					return;
				}
				Vector<str> next{GetMemoryResource(MemTag::Editor)};
				for (const str& name : target->keywords) {
					const bool entry = std::any_of(entries.begin(), entries.end(),
												   [&](const MaterialInspectorOps::KeywordEntry& candidate) { return candidate.keyword == name; });
					if (!entry) {
						next.push_back(name);
					}
				}
				next.push_back(entries[index].keyword);
				target->SetKeywords({next.data(), next.size()});
				if (const MaterialAsset::Param* param = ParamFor(*target, member); param && param->kind == UniformMemberType::Int) {
					target->SetInt(member, static_cast<i32>(index));
				} else {
					target->SetFloat(member, static_cast<f32>(index));
				}
				EnsureVariantCompiled(target, shaderPtr);
				NoteMaterialEdited(guidCopy);
			};
			UI::Widgets::Dropdown(context, row, desc);
		}

		void BuildRow(UI::Context& context, UI::ElementHandle row, MaterialAsset* target, const Guid& guid,
					  ShaderAsset& shader, const MaterialInspectorOps::Row& spec) {
			using MaterialInspectorOps::RowKind;
			switch (spec.kind) {
			case RowKind::Number:
				BuildNumberRow(context, row, target, guid, spec);
				break;
			case RowKind::Slider:
				BuildSliderRow(context, row, target, guid, spec);
				break;
			case RowKind::Toggle:
				BuildToggleRow(context, row, target, guid, spec);
				break;
			case RowKind::Color:
			case RowKind::HDRColor:
				BuildColorRow(context, row, target, guid, spec);
				break;
			case RowKind::Vector:
				BuildVectorRow(context, row, target, guid, spec);
				break;
			case RowKind::Texture:
				BuildTextureRow(context, row, target, guid, shader, spec);
				break;
			case RowKind::Keyword:
				BuildKeywordRow(context, row, target, guid, shader, spec);
				break;
			case RowKind::KeywordEnum:
				BuildKeywordEnumRow(context, row, target, guid, shader, spec);
				break;
			case RowKind::Blend:
			case RowKind::Cull:
			case RowKind::AlphaClip:
			case RowKind::SortingPriority:
				BuildRenderStateRow(context, row, target, guid, shader, spec);
				break;
			case RowKind::Unsupported: {
				UI::ElementHandle text = context.CreateElement();
				context.AppendChild(row, text);
				context.SetText(text, UISystem::GetDefaultFont(), FieldFont(), TextDim(),
								std::format("({}, no drawer)", UniformMemberTypeName(spec.member)));
				break;
			}
			}
		}

		u64 GroupFoldKey(const Guid& shaderGuid, std::string_view groupName, bool builtIn) {
			if (builtIn) {
				return StringHash(std::format("mat-builtin:{}", groupName)).GetValue();
			}
			return StringHash(std::format("mat-group:{}/{}", shaderGuid, groupName)).GetValue();
		}

		bool BuildGroupHeader(UI::Context& context, UI::ElementHandle parent, InspectorPanel* panel,
							  const Guid& shaderGuid, std::string_view groupName, bool builtIn) {
			u64 key = GroupFoldKey(shaderGuid, groupName, builtIn);
			bool collapsed = panel && panel->IsSectionCollapsed(key);

			UI::ElementHandle header = MakeRow(context, parent, "");
			UI::ElementHandle caret = context.CreateElement();
			context.AppendChild(header, caret);
			EditorIcons::SetGlyphOrText(context, caret,
										collapsed ? EditorIcons::Glyph::CaretRight : EditorIcons::Glyph::CaretDown,
										collapsed ? ">" : "v", TextDim(),
										EditorTheme::GetMetric(MetricToken::IconGutter), FieldFont());
			UI::Style caretStyle = context.GetStyle(caret);
			caretStyle.flexShrink = 0.0f;
			caretStyle.width = UI::Length::Pixel(EditorTheme::GetMetric(MetricToken::IconGutter));
			caretStyle.margin = {0.0f, 0.0f, EditorTheme::GetMetric(MetricToken::IconGap), 0.0f};
			caretStyle.textAlign = UI::Align::Center;
			caretStyle.cursor = SystemCursor::Hand;
			context.SetStyle(caret, caretStyle);
			context.Events(caret).onClick = [panel, key]() {
				if (panel) {
					panel->ToggleSection(key);
				}
			};

			UI::ElementHandle title = context.CreateElement();
			context.AppendChild(header, title);
			context.SetHoverable(title, false);
			context.SetText(title, UISystem::GetDefaultFont(), FieldFont(), TextStrong(), groupName);
			return !collapsed;
		}

		void AssignShader(MaterialAsset* target, InspectorPanel* panel, const Guid& materialGuid, const Guid& shaderGuid) {
			Asset* resolved = shaderGuid.IsValid() ? AssetDatabase::LoadAssetByGuid(shaderGuid) : nullptr;
			if (resolved && resolved->GetAssetTypeId() == StringHash("ShaderAsset") && static_cast<ShaderAsset*>(resolved)->IsKernel()) {
				SLT_LOG_WARN("Editor", "'{}' is a compute kernel (not a shader)", resolved->virtualPath);
				AssetDatabase::UnloadAsset(resolved);
				return;
			}
			target->shaderGuid = shaderGuid;
			target->shader = {};
			if (resolved && resolved->GetAssetTypeId() == StringHash("ShaderAsset")) {
				target->shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(resolved));
			}
			if (ShaderAsset* shader = target->shader.Get()) {
				Vector<str> textureSlots{GetMemoryResource(MemTag::Editor)};
				shader->GetMaterialTextureSlots(textureSlots);
				target->SyncToLayout(shader->GetMaterialParamLayout(), {textureSlots.data(), textureSlots.size()},
									 {shader->memberDefaults.data(), shader->memberDefaults.size()});
				target->ApplyKeywordBindings({shader->keywordBindings.data(), shader->keywordBindings.size()});
			}
			EnsureVariantCompiled(target, target->shader.Get());
			if (panel) {
				panel->RequestRebuild();
			}
			NoteMaterialEdited(materialGuid);
		}

		void BuildShaderDropdown(UI::Context& context, UI::ElementHandle row, MaterialAsset* target,
								 InspectorPanel* panel, const Guid& materialGuid) {
			Vector<MaterialInspectorOps::ShaderChoice> choices{GetMemoryResource(MemTag::Editor)};
			MaterialInspectorOps::CollectShaderChoices(choices);

			Vector<std::string_view> items{GetMemoryResource(MemTag::Editor)};
			items.push_back("None");
			u32 selected = 0;
			for (size_t i = 0; i < choices.size(); ++i) {
				items.push_back(std::string_view{choices[i].displayName});
				if (choices[i].guid == target->shaderGuid) {
					selected = static_cast<u32>(i + 1);
				}
			}
			str assigned;
			if (selected == 0 && target->shaderGuid.IsValid()) {
				Path path = AssetDatabase::LookupPath(target->shaderGuid);
				assigned = path.ToString().empty() ? str("(missing shader)") : path.GetFilename();
				items.push_back(std::string_view{assigned});
				selected = static_cast<u32>(items.size() - 1);
			}

			Vector<Guid> byIndex{GetMemoryResource(MemTag::Editor)};
			byIndex.push_back(Guid{});
			for (const MaterialInspectorOps::ShaderChoice& choice : choices) {
				byIndex.push_back(choice.guid);
			}
			if (byIndex.size() < items.size()) {
				byIndex.push_back(target->shaderGuid);
			}

			UI::Widgets::DropdownDesc desc;
			desc.items = {items.data(), items.size()};
			desc.selected = selected;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FieldFont();
			Vector<Guid> resolved = byIndex;
			desc.onSelect = [target, panel, materialGuid, resolved](u32 index) {
				if (index < resolved.size()) {
					AssignShader(target, panel, materialGuid, resolved[index]);
				}
			};
			UI::Widgets::DropdownParts parts = UI::Widgets::Dropdown(context, row, desc);

			Path shaderPath = AssetDatabase::LookupPath(target->shaderGuid);
			if (!shaderPath.ToString().empty()) {
				context.SetTooltip(parts.root, shaderPath.ToString());
			}

			context.Events(parts.root).onDragOver = [](const UI::DragPayload& payload) {
				return payload.type == "Asset"_hash && payload.data.Is<AssetDragData>() &&
					   StringHash{payload.data.As<AssetDragData>().assetTypeHash} == StringHash("ShaderAsset");
			};
			context.Events(parts.root).onDrop = [target, panel, materialGuid](const UI::DragPayload& payload, Vec2) {
				if (!payload.data.Is<AssetDragData>()) {
					return;
				}
				AssignShader(target, panel, materialGuid, payload.data.As<AssetDragData>().guid);
			};
		}

		void BuildShaderParamRows(UI::Context& context, UI::ElementHandle parent,
								  const Guid& guid, MaterialAsset& material, ShaderAsset& shader) {
			const UniformBlockLayout* layout = shader.GetMaterialParamLayout();
			Vector<str> textureSlots{GetMemoryResource(MemTag::Editor)};
			shader.GetMaterialTextureSlots(textureSlots);

			material.SyncToLayout(layout, {textureSlots.data(), textureSlots.size()},
								  {shader.memberDefaults.data(), shader.memberDefaults.size()});

			Guid guidCopy = guid;
			MaterialAsset* target = &material;
			InspectorPanel* panel = InspectorPanel::GetBuildingPanel();

			Vector<MaterialInspectorOps::Group> groups{GetMemoryResource(MemTag::Editor)};
			MaterialInspectorOps::BuildLayout(shader, groups);
			for (const MaterialInspectorOps::Group& group : groups) {
				if (!BuildGroupHeader(context, parent, panel, shader.assetId, group.name, group.builtIn)) {
					continue;
				}
				for (const MaterialInspectorOps::Row& spec : group.rows) {
					UI::ElementHandle row = MakeRow(context, parent, spec.DisplayName());
					BuildRow(context, row, target, guidCopy, shader, spec);
				}
			}

			bool anyOrphan = false;
			auto orphanHeader = [&]() {
				if (!anyOrphan) {
					AddInspectorHeader(context, parent, "Unused (not in shader)", TextDim());
					anyOrphan = true;
				}
			};
			std::span<const str> slotSpan{textureSlots.data(), textureSlots.size()};
			for (const MaterialAsset::Param& param : target->params) {
				if (MaterialAsset::IsOrphanParam(param.name, layout, slotSpan)) {
					orphanHeader();
					MakeRow(context, parent, param.name);
				}
			}

			bool embedded = AssetDatabase::GetProductSource(guidCopy).IsValid() &&
							AssetDatabase::LookupPath(guidCopy).ToString().empty();
			if (!embedded) {
				UI::ElementHandle actions = MakeRow(context, parent, "");
				UI::Widgets::ButtonDesc save;
				save.label = "Save";
				save.font = UISystem::GetDefaultFont();
				save.fontSizePx = FieldFont();
				save.onClick = [guidCopy, target]() { SaveMaterial(guidCopy, *target); };
				UI::Widgets::Button(context, actions, save);
			}
		}

	} // anonymous namespace

	void RegisterBuiltInAssetInspectors() {
		PropertyDrawers::Initialize();

		PropertyDrawers::RegisterAssetInspector(
			StringHash("MaterialAsset"),
			[](UI::Context& context, UI::ElementHandle parent, const Guid& guid, Asset& asset) {
				MaterialAsset& material = static_cast<MaterialAsset&>(asset);
				InspectorPanel* panel = InspectorPanel::GetBuildingPanel();

				AddInspectorHeader(context, parent, "Material", TextStrong());

				UI::ElementHandle shaderRow = MakeRow(context, parent, "Shader");
				ShaderAsset* shader = material.shader.Get();

				MaterialAsset* target = &material;
				Guid guidCopy = guid;
				BuildShaderDropdown(context, shaderRow, target, panel, guidCopy);

				if (!shader) {
					UI::ElementHandle hint = MakeRow(context, parent, "");
					UI::ElementHandle text = context.CreateElement();
					context.AppendChild(hint, text);
					context.SetText(text, UISystem::GetDefaultFont(), FieldFont(), TextDim(),
									"Assign a shader to edit parameters");
					return;
				}
				BuildShaderParamRows(context, parent, guid, material, *shader);
			});
	}

} // namespace Shalltear
