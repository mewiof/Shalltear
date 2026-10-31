#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class Scene;

	enum class SceneLoadState : u8 {
		Pending,
		Reading,
		Parsing,
		Integrating,
		AwaitingActivation,
		Done,
		Failed,
	};

	class SHALLTEAR_CORE_API SceneLoadOperation : public Object {
	public:
		~SceneLoadOperation() override;

		SceneLoadState GetState() const;
		f32 GetProgress() const;
		bool IsDone() const;
		Handle<Scene> GetScene() const;
		str GetError() const;
		void SetActivationAllowed(bool allowed);
		void WaitForCompletion();

		struct Impl;
	private:
		friend class SceneSystem;
		friend struct SceneInternalAccess;

		SceneLoadOperation();

		Impl* _impl = nullptr;
	};

} // namespace Shalltear
