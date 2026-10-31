file(GLOB_RECURSE HOT_RELOAD_MODULE_SOURCES "Private/*.cpp" "Private/*.h")

add_library(ShalltearHotReloadModuleV1 SHARED ${HOT_RELOAD_MODULE_SOURCES})
target_link_libraries(ShalltearHotReloadModuleV1 PRIVATE ShalltearCore)

add_library(ShalltearHotReloadModuleV2 SHARED ${HOT_RELOAD_MODULE_SOURCES})
target_link_libraries(ShalltearHotReloadModuleV2 PRIVATE ShalltearCore)
target_compile_definitions(ShalltearHotReloadModuleV2 PRIVATE SLT_HOTRELOAD_V2)

add_library(ShalltearHotReloadModuleBad SHARED ${HOT_RELOAD_MODULE_SOURCES})
target_link_libraries(ShalltearHotReloadModuleBad PRIVATE ShalltearCore)
target_compile_definitions(ShalltearHotReloadModuleBad PRIVATE SLT_HOTRELOAD_BAD)

add_library(ShalltearHotReloadModuleGone SHARED ${HOT_RELOAD_MODULE_SOURCES})
target_link_libraries(ShalltearHotReloadModuleGone PRIVATE ShalltearCore)
target_compile_definitions(ShalltearHotReloadModuleGone PRIVATE SLT_HOTRELOAD_GONE)

add_library(ShalltearHotReloadModuleAbi SHARED ${HOT_RELOAD_MODULE_SOURCES})
target_link_libraries(ShalltearHotReloadModuleAbi PRIVATE ShalltearCore)
target_compile_definitions(ShalltearHotReloadModuleAbi PRIVATE SLT_MODULE_ABI_HASH=0xBADABBADABBADD00ull)

if(SHALLTEAR_BUILD_SHARED)
    foreach(flavor V1 V2)
        target_link_libraries(ShalltearHotReloadModule${flavor} PRIVATE ShalltearEditor)
        target_compile_definitions(ShalltearHotReloadModule${flavor} PRIVATE SLT_WITH_EDITOR)
    endforeach()
endif()
