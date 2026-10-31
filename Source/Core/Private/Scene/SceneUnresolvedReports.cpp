#include "SceneInternal.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

#include <algorithm>
#include <vector>

namespace Shalltear {

	namespace {

		class HeldUnresolvedReports final : public SceneInternal::UnresolvedReportHold {
		public:
			void HoldRows(std::string_view what, const std::vector<SceneInternal::UnresolvedTypeMention>& types) override {
				_scenes.push_back({str(what), types});
			}

			void HoldUnconverted(StringHash rowType) override {
				if (std::find(_unconverted.begin(), _unconverted.end(), rowType) == _unconverted.end()) {
					_unconverted.push_back(rowType);
				}
			}

			void Release() override {
				for (StringHash rowType : _unconverted) {
					if (!TypeRegistry::FindType(rowType)) {
						SceneInternal::LogUnconvertedRow(rowType);
					}
				}
				for (const HeldScene& scene : _scenes) {
					u32 rows = 0;
					std::vector<SceneInternal::UnresolvedTypeMention> missing;
					for (const SceneInternal::UnresolvedTypeMention& mention : scene.types) {
						if (!TypeRegistry::FindType(mention.type)) {
							missing.push_back(mention);
							rows += mention.rows;
						}
					}
					SceneInternal::LogUnresolvedRows(rows, missing, scene.what);
				}
				Delete(this);
			}
		private:
			struct HeldScene {
				str what;
				std::vector<SceneInternal::UnresolvedTypeMention> types;
			};

			std::vector<HeldScene> _scenes;
			std::vector<StringHash> _unconverted;
		};

	} // namespace

	void SceneSystem::HoldUnresolvedComponentReports() {
		auto* hold = New<HeldUnresolvedReports>(MemTag::Scene);
		if (!SceneInternal::InstallUnresolvedReportHold(*hold)) {
			Delete(hold);
		}
	}

} // namespace Shalltear
