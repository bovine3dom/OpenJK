# Compile shared raster sources with SP types. This target has no renderer exports.
find_package(OpenGL REQUIRED)
add_library(sp-rend2-port OBJECT
	${MPDir}/rd-rend2/tr_allocator.cpp
	${MPDir}/rd-rend2/tr_extramath.cpp
	${MPDir}/rd-rend2/tr_glsl_parse.cpp
	${MPDir}/rd-rend2/tr_glsl.cpp
	${MPDir}/rd-rend2/tr_subs.cpp
	${MPDir}/rd-rend2/tr_tangentspace.cpp
	${MPDir}/rd-rend2/MikkTSpace/mikktspace.c
	${Rend2GLSLDir}/glsl_shaders.cpp)
set_source_files_properties(${Rend2GLSLDir}/glsl_shaders.cpp PROPERTIES GENERATED TRUE)
add_dependencies(sp-rend2-port rend2_glsl)
target_compile_definitions(sp-rend2-port PRIVATE ${SharedDefines} RENDERER _JK2EXE REND2_SP)
target_include_directories(sp-rend2-port PRIVATE
	${SharedDir} ${SPDir} ${MPDir}/rd-rend2 ${GSLIncludeDirectory}
	${OPENGL_INCLUDE_DIR} ${Rend2GLSLDir})
