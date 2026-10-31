file(GLOB_RECURSE NODE_SHADER_SOURCES "Private/*.cpp" "Private/*.h")

shalltear_add_package(NodeShader SOURCES ${NODE_SHADER_SOURCES})

target_compile_definitions(NodeShader PRIVATE SHALLTEAR_NODESHADER_BUILD)
target_include_directories(NodeShader PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/Private
	${CMAKE_SOURCE_DIR}/Source/Graphs/Private)

target_link_libraries(NodeShader PRIVATE Graphs ShalltearEditor)
