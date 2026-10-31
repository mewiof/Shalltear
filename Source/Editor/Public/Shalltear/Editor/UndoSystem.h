#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/Variant.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditTarget.h"

#include <string_view>

namespace Shalltear {

	class PropertySetCommand;
	class SetFieldStateCommand;
	class Scene;

	class SHALLTEAR_EDITOR_API IEditorCommand {
	public:
		virtual ~IEditorCommand() = default;
		virtual void Apply() = 0;
		virtual void Revert() = 0;
		virtual str GetLabel() const = 0;
		virtual bool TryCoalesce(const IEditorCommand& next) {
			(void)next;
			return false;
		}
		virtual const PropertySetCommand* AsPropertySet() const { return nullptr; }
		virtual const SetFieldStateCommand* AsFieldState() const { return nullptr; }
		virtual Handle<Scene> GetAffectedScene() const { return {}; }
	};

	class SHALLTEAR_EDITOR_API PropertySetCommand final : public IEditorCommand {
	public:
		PropertySetCommand(EditTarget target, StringHash field, Variant before, Variant after, std::string_view label = {});

		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
		bool TryCoalesce(const IEditorCommand& next) override;
		const PropertySetCommand* AsPropertySet() const override { return this; }
		Handle<Scene> GetAffectedScene() const override;
	private:
		void Write(const Variant& value);

		EditTarget _target;
		StringHash _field;
		Variant _before;
		Variant _after;
		str _label;
	};

	class SHALLTEAR_EDITOR_API UndoSystem {
	public:
		static void Initialize();
		static void Terminate();

		static void Push(UniquePtr<IEditorCommand> command);

		static void BeginGroup(std::string_view label);
		static void EndGroup();
		static bool IsGroupOpen();

		static bool Undo();
		static bool Redo();
		static bool CanUndo();
		static bool CanRedo();
		static str GetUndoLabel();
		static str GetRedoLabel();

		static void Clear();
		static u32 GetUndoDepth();
		static u32 GetRedoDepth();
	};

} // namespace Shalltear
