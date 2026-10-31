if(NOT SHALLTEAR_BUILD_SHARED)
	return()
endif()

file(GLOB_RECURSE AUDIO_SCRIPT_MODULE_SOURCES "Private/*.cpp" "Private/*.h")

foreach(flavor V1 V2)
	add_library(ShalltearAudioScriptModule${flavor} SHARED ${AUDIO_SCRIPT_MODULE_SOURCES})
	target_link_libraries(ShalltearAudioScriptModule${flavor} PRIVATE ShalltearCore Audio)
	target_compile_options(ShalltearAudioScriptModule${flavor} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wfunction-effects -Werror=function-effects>)
endforeach()
target_compile_definitions(ShalltearAudioScriptModuleV2 PRIVATE SLT_AUDIO_SCRIPT_V2)
