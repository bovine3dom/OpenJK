if(NOT InOpenJK)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif()

find_package(OpenGL REQUIRED)

set(SPRend2Files
	${MPDir}/rd-rend2/tr_allocator.cpp
	${MPDir}/rd-rend2/tr_backend.cpp
	${MPDir}/rd-rend2/tr_bsp.cpp
	${MPDir}/rd-rend2/tr_cache.cpp
	${MPDir}/rd-rend2/tr_cmds.cpp
	${MPDir}/rd-rend2/tr_curve.cpp
	${MPDir}/rd-rend2/tr_decals.cpp
	${MPDir}/rd-rend2/tr_extensions.cpp
	${MPDir}/rd-rend2/tr_extramath.cpp
	${MPDir}/rd-rend2/tr_fbo.cpp
	${MPDir}/rd-rend2/tr_flares.cpp
	${MPDir}/rd-rend2/tr_glsl.cpp
	${MPDir}/rd-rend2/tr_glsl_parse.cpp
	${MPDir}/rd-rend2/tr_image.cpp
	${MPDir}/rd-rend2/tr_image_stb.cpp
	${MPDir}/rd-rend2/tr_init.cpp
	${MPDir}/rd-rend2/tr_light.cpp
	${MPDir}/rd-rend2/tr_local.h
	${MPDir}/rd-rend2/tr_main.cpp
	${MPDir}/rd-rend2/tr_marks.cpp
	${MPDir}/rd-rend2/tr_mesh.cpp
	${MPDir}/rd-rend2/tr_model.cpp
	${MPDir}/rd-rend2/tr_model_iqm.cpp
	${MPDir}/rd-rend2/tr_postprocess.cpp
	${MPDir}/rd-rend2/tr_scene.cpp
	${MPDir}/rd-rend2/tr_shade.cpp
	${MPDir}/rd-rend2/tr_shade_calc.cpp
	${MPDir}/rd-rend2/tr_shader.cpp
	${MPDir}/rd-rend2/tr_shadows.cpp
	${MPDir}/rd-rend2/tr_sky.cpp
	${MPDir}/rd-rend2/tr_subs.cpp
	${MPDir}/rd-rend2/tr_surface.cpp
	${MPDir}/rd-rend2/tr_tangentspace.cpp
	${MPDir}/rd-rend2/tr_vbo.cpp
	${MPDir}/rd-rend2/tr_weather.cpp
	${MPDir}/rd-rend2/tr_world.cpp
	${MPDir}/rd-rend2/MikkTSpace/mikktspace.c
	${SPDir}/rd-vanilla/G2_API.cpp
	${SPDir}/rd-vanilla/G2_bones.cpp
	${SPDir}/rd-vanilla/G2_bolts.cpp
	${SPDir}/rd-vanilla/G2_misc.cpp
	${SPDir}/rd-vanilla/G2_surfaces.cpp
	${SPDir}/rd-vanilla/tr_ghoul2.cpp
	${SPDir}/rd-vanilla/tr_skin.cpp
	${SPDir}/rd-common/tr_font.cpp
	${SPDir}/rd-common/tr_image_load.cpp
	${SPDir}/rd-common/tr_image_jpg.cpp
	${SPDir}/rd-common/tr_image_tga.cpp
	${SPDir}/rd-common/tr_image_png.cpp
	${SPDir}/rd-common/tr_noise.cpp
	${SPDir}/qcommon/q_shared.cpp
	${SPDir}/qcommon/matcomp.cpp
	${SharedCommonFiles}
	${SharedCommonSafeFiles}
	tr_sp_import.h
	tr_sp_api.cpp
	tr_sp_effects.cpp
	${Rend2GLSLDir}/glsl_shaders.cpp)

set_source_files_properties(${Rend2GLSLDir}/glsl_shaders.cpp PROPERTIES GENERATED TRUE)
add_library(${SPRend2} SHARED ${SPRend2Files})
add_dependencies(${SPRend2} rend2_glsl)
target_compile_definitions(${SPRend2} PRIVATE ${SharedDefines} RENDERER _JK2EXE REND2_SP)
target_compile_options(${SPRend2} PRIVATE ${OPENJK_VISIBILITY_FLAGS})
target_include_directories(${SPRend2} PRIVATE
	${SharedDir} ${SPDir} ${MPDir}/rd-rend2 ${SPDir}/rd-rend2
	${SPDir}/rd-common ${GSLIncludeDirectory} ${OpenJKLibDir}
	${JPEG_INCLUDE_DIR} ${PNG_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIR}
	${MINIZIP_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIR} ${Rend2GLSLDir})
target_link_libraries(${SPRend2}
	${JPEG_LIBRARIES} ${PNG_LIBRARIES} ${ZLIB_LIBRARIES}
	${MINIZIP_LIBRARIES} ${OPENGL_LIBRARIES})
set_target_properties(${SPRend2} PROPERTIES PREFIX "" PROJECT_LABEL "SP Rend2 Renderer")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set_property(TARGET ${SPRend2} APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--no-undefined")
endif()

if(WIN32)
	install(TARGETS ${SPRend2} RUNTIME DESTINATION ${JKAInstallDir} COMPONENT ${JKASPClientComponent})
elseif(MakeApplicationBundles AND BuildSPEngine)
	install(TARGETS ${SPRend2} LIBRARY DESTINATION "${JKAInstallDir}/${SPEngine}.app/Contents/MacOS/" COMPONENT ${JKASPClientComponent})
else()
	install(TARGETS ${SPRend2} LIBRARY DESTINATION ${JKAInstallDir} COMPONENT ${JKASPClientComponent})
endif()
