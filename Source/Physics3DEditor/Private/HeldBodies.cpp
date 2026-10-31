#include "Physics3DEditor.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include "Shalltear/Editor/EditorEvents.h"

#include "PhysicsWorld.h"

namespace Shalltear {

	namespace {

		class HeldBodyState : public Object {
		public:
			void OnTransformGesture(const EditorTransformGestureEvent& event) {
				GetPhysicsWorld().SetHeldBodies(event.objects);
			}
		};

		HeldBodyState* _state = nullptr;

	} // anonymous namespace

	namespace Physics3DEditor {

		void RegisterHeldBodies(const void*) {
			if (_state) {
				return;
			}
			_state = New<HeldBodyState>(MemTag::Physics);
			EditorEvents::TransformGesture.Subscribe(EventBus::GetGlobal(), Handle<HeldBodyState>(_state),
													 &HeldBodyState::OnTransformGesture);
		}

		void ReleaseHeldBodies() {
			if (!_state) {
				return;
			}
			GetPhysicsWorld().ReleaseHeldBodies();

			EventBus::GetGlobal().UnsubscribeAll(_state->GetInstanceId());
			Delete(_state);
			_state = nullptr;
		}

	} // namespace Physics3DEditor

} // namespace Shalltear
