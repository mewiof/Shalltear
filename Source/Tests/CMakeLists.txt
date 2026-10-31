set(GTEST_SUBMODULE_DIR ${CMAKE_SOURCE_DIR}/External/Submodules/googletest)
if(NOT EXISTS ${GTEST_SUBMODULE_DIR}/CMakeLists.txt)
    message(FATAL_ERROR
        "googletest submodule is missing. Run:\n"
        "    git submodule update --init --recursive")
endif()

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

set(sltWarningsAsErrors ${CMAKE_COMPILE_WARNING_AS_ERROR})
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
add_subdirectory(${GTEST_SUBMODULE_DIR} ${CMAKE_BINARY_DIR}/External/googletest EXCLUDE_FROM_ALL)
set(CMAKE_COMPILE_WARNING_AS_ERROR ${sltWarningsAsErrors})

include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES "Private/*.cpp" "Private/*.h")

add_executable(ShalltearTests ${TEST_SOURCES})

target_link_libraries(ShalltearTests
    PRIVATE
        ShalltearCore
        ShalltearEditor
        ImportPipeline
        Graphs
        NodeShader
        GTest::gtest
)

target_include_directories(ShalltearTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Private)

target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/Core/Private)

target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/Editor/Private)

target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/ImportPipeline/Private)

target_include_directories(ShalltearTests PRIVATE
    ${CMAKE_SOURCE_DIR}/Source/Graphs/Private
    ${CMAKE_SOURCE_DIR}/Source/NodeShader/Private)

target_compile_definitions(ShalltearTests PRIVATE SHALLTEAR_LOG_STRIP_LEVEL=0)

if(NOT SHALLTEAR_DISABLE_PCH)
    target_precompile_headers(ShalltearTests PRIVATE Private/TestsPCH.h)
endif()

if(TARGET Physics3D)
    if(SHALLTEAR_BUILD_SHARED)
        add_dependencies(ShalltearTests Physics3D)
    else()
        target_link_libraries(ShalltearTests PRIVATE Physics3D)
        target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/Physics3D/Private)
    endif()

    if(SHALLTEAR_PHYSICS_BACKEND STREQUAL "stub")
        target_compile_definitions(ShalltearTests PRIVATE SHALLTEAR_PHYSICS_BACKEND_STUB)
    endif()
endif()

if(TARGET Animation)
    if(SHALLTEAR_BUILD_SHARED)
        add_dependencies(ShalltearTests Animation)
        target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/Animation/Public)
    else()
        target_link_libraries(ShalltearTests PRIVATE Animation)
        target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/Animation/Private)
    endif()
endif()

if(TARGET PostProcessing)
    if(SHALLTEAR_BUILD_SHARED)
        add_dependencies(ShalltearTests PostProcessing)
        target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/PostProcessing/Public)
    else()
        target_link_libraries(ShalltearTests PRIVATE PostProcessing)
        target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/PostProcessing/Private)
    endif()
endif()

if(TARGET Audio)
    if(SHALLTEAR_BUILD_SHARED)
        add_dependencies(ShalltearTests Audio)
    else()
        target_link_libraries(ShalltearTests PRIVATE Audio)
        target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/Audio/Private)
    endif()
endif()

if(TARGET VisualEffects)
    if(SHALLTEAR_BUILD_SHARED)
        add_dependencies(ShalltearTests VisualEffects)
        target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/VisualEffects/Public)
    else()
        target_link_libraries(ShalltearTests PRIVATE VisualEffects)
        target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/VisualEffects/Private)
    endif()
endif()

if(TARGET ShalltearSampleModule)
    add_dependencies(ShalltearTests ShalltearSampleModule)
endif()
if(TARGET ShalltearHotReloadModuleV1)
    add_dependencies(ShalltearTests
        ShalltearHotReloadModuleV1
        ShalltearHotReloadModuleV2
        ShalltearHotReloadModuleBad
        ShalltearHotReloadModuleAbi
        ShalltearHotReloadModuleGone)
endif()
if(TARGET ShalltearPhysicsListenerModule)
    add_dependencies(ShalltearTests ShalltearPhysicsListenerModule)
endif()
if(TARGET ShalltearPhysicsScriptModule)
    add_dependencies(ShalltearTests ShalltearPhysicsScriptModule)
endif()
if(TARGET ShalltearAudioScriptModuleV1)
    add_dependencies(ShalltearTests ShalltearAudioScriptModuleV1 ShalltearAudioScriptModuleV2)
    target_include_directories(ShalltearTests PRIVATE ${CMAKE_SOURCE_DIR}/Source/AudioScriptModule/Private)
endif()
if(TARGET ShalltearVisualEffectsScriptModule)
    add_dependencies(ShalltearTests ShalltearVisualEffectsScriptModule)
endif()

gtest_discover_tests(ShalltearTests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)
