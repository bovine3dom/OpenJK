find_package(OpenGL REQUIRED)
add_executable(compact_glsl
	compact.cpp
	../tr_allocator.cpp
	../tr_glsl_parse.cpp)
target_compile_definitions(compact_glsl PRIVATE ${SharedDefines} GLSL_BUILDTOOL NOMINMAX)
target_include_directories(compact_glsl PRIVATE
	${SharedDir} ${MPDir} ${MPDir}/rd-rend2 ${GSLIncludeDirectory} ${OPENGL_INCLUDE_DIR})

file(GLOB Rend2GLSLFiles "${CMAKE_CURRENT_SOURCE_DIR}/*.glsl")
include(FetchContent)
FetchContent_Declare(smaa
    URL https://codeload.github.com/iryoku/smaa/tar.gz/71c806a838bdd7d517df19192a20f0c61b3ca29d
    URL_HASH SHA256=046a435f5713f8bfe8148fe6305c2abf1409fc2122bee6070061c675e1e55f57)
FetchContent_MakeAvailable(smaa)
file(READ "${smaa_SOURCE_DIR}/SMAA.hlsl" SMAA_HEADER)
configure_file(smaa.glsl.in "${CMAKE_CURRENT_BINARY_DIR}/smaa.glsl" @ONLY)
list(APPEND Rend2GLSLFiles "${CMAKE_CURRENT_BINARY_DIR}/smaa.glsl")
set(SMAAIncludeDir "${smaa_SOURCE_DIR}" CACHE INTERNAL "Pinned SMAA source directory")
install(FILES "${smaa_SOURCE_DIR}/LICENSE.txt" DESTINATION "${JKAInstallDir}/licenses" RENAME SMAA.txt)
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
