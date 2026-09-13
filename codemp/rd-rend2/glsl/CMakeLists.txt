find_package(OpenGL REQUIRED)
add_executable(compact_glsl
	compact.cpp
	../tr_allocator.cpp
	../tr_glsl_parse.cpp)
target_compile_definitions(compact_glsl PRIVATE ${SharedDefines} GLSL_BUILDTOOL NOMINMAX)
target_include_directories(compact_glsl PRIVATE
	${SharedDir} ${MPDir} ${MPDir}/rd-rend2 ${GSLIncludeDirectory} ${OPENGL_INCLUDE_DIR})

file(GLOB Rend2GLSLFiles "${CMAKE_CURRENT_SOURCE_DIR}/*.glsl")
add_custom_command(
	OUTPUT glsl_shaders.cpp glsl_shaders.h
	COMMAND compact_glsl
		${CMAKE_CURRENT_BINARY_DIR}/glsl_shaders.cpp
		${CMAKE_CURRENT_BINARY_DIR}/glsl_shaders.h
		${Rend2GLSLFiles}
	DEPENDS compact_glsl ${Rend2GLSLFiles}
	VERBATIM)
add_custom_target(rend2_glsl DEPENDS
	${CMAKE_CURRENT_BINARY_DIR}/glsl_shaders.cpp
	${CMAKE_CURRENT_BINARY_DIR}/glsl_shaders.h)
set(Rend2GLSLDir ${CMAKE_CURRENT_BINARY_DIR} PARENT_SCOPE)
