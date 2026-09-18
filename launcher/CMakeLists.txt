if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 10.15)
	# std::filesystem is available in Apple's libc++ from macOS 10.15.
	set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15)
endif()

set(BUILD_SHARED_LIBS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_TESTS OFF)
set(INSTALL_PROJECT OFF)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/cache" CACHE PATH "FetchContent cache directory")
include(FetchContent)
FetchContent_Declare(miniz
	URL https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip
	URL_HASH SHA256=f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a
	EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(miniz)
add_library(miniz STATIC "${miniz_SOURCE_DIR}/miniz.c")
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_include_directories(miniz PUBLIC "${miniz_SOURCE_DIR}")

add_library(openjk_jo_import STATIC jo_import.cpp jo_import.h)
target_compile_features(openjk_jo_import PUBLIC cxx_std_17)
target_link_libraries(openjk_jo_import PRIVATE miniz)

add_library(openjk_bootstrap STATIC bootstrap.cpp bootstrap.h)
target_compile_features(openjk_bootstrap PUBLIC cxx_std_17)
target_compile_definitions(openjk_bootstrap PRIVATE OPENJK_SP_ENGINE_NAME="${SPEngine}")
target_link_libraries(openjk_bootstrap PRIVATE miniz)
if(WIN32)
	target_link_libraries(openjk_bootstrap PRIVATE shell32)
endif()

add_executable(openjk-launcher launcher_main.cpp)
set_target_properties(openjk-launcher PROPERTIES OUTPUT_NAME "${LauncherExecutable}")
target_compile_features(openjk-launcher PRIVATE cxx_std_17)
target_link_libraries(openjk-launcher PRIVATE openjk_bootstrap openjk_jo_import)
if(WIN32)
	target_sources(openjk-launcher PRIVATE launcher.rc "${CMAKE_SOURCE_DIR}/shared/sys/win_manifest.manifest")
endif()
if(APPLE AND MakeApplicationBundles)
	target_compile_definitions(openjk_bootstrap PRIVATE OPENJK_LAUNCHER_BUNDLE=1)
	set_source_files_properties("${SPDir}/macosx/OpenJK.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
	target_sources(openjk-launcher PRIVATE "${SPDir}/macosx/OpenJK.icns")
	set_target_properties(openjk-launcher PROPERTIES
		MACOSX_BUNDLE TRUE
		OUTPUT_NAME "${JKASPBundleName}"
		MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/OpenJKLauncherInfo.plist")
	foreach(helper ${SPEngine} ${SPRDVanillaRenderer} ${SPRend2})
		if(TARGET ${helper})
			add_dependencies(openjk-launcher ${helper})
			add_custom_command(TARGET openjk-launcher POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_FILE:${helper}>" "$<TARGET_FILE_DIR:openjk-launcher>")
		endif()
	endforeach()
	if(BuildSPGame)
		add_dependencies(openjk-launcher ${SPGame})
		add_custom_command(TARGET openjk-launcher POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:openjk-launcher>/OpenJK/ui/fonts"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${SPGame}>" "$<TARGET_FILE_DIR:openjk-launcher>/OpenJK"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ui/jo_preparation.menu"
				"${CMAKE_SOURCE_DIR}/ui/jo_preparation.txt" "$<TARGET_FILE_DIR:openjk-launcher>/OpenJK/ui"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ui/rmlui"
				"$<TARGET_FILE_DIR:openjk-launcher>/OpenJK/ui/rmlui"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ui/fonts/plex"
				"$<TARGET_FILE_DIR:openjk-launcher>/OpenJK/ui/fonts/plex")
	endif()
endif()

if(BuildRmlUi)
	find_package(OpenGL REQUIRED)
	find_package(Threads REQUIRED)
	if(UseInternalSDL2)
		if(NOT WIN32)
			message(FATAL_ERROR "The bundled SDL2 libraries support Windows only")
		endif()
		add_library(openjk_launcher_sdl INTERFACE)
		target_include_directories(openjk_launcher_sdl INTERFACE "${OpenJKLibDir}/SDL2/include")
		if(CMAKE_SIZEOF_VOID_P EQUAL 4)
			target_link_libraries(openjk_launcher_sdl INTERFACE "${OpenJKLibDir}/SDL2/lib/x86/SDL2.lib")
		else()
			target_link_libraries(openjk_launcher_sdl INTERFACE "${OpenJKLibDir}/SDL2/lib/x64/SDL2.lib")
		endif()
	else()
		find_package(SDL2 REQUIRED CONFIG)
		if(WIN32 AND NOT TARGET SDL2::SDL2)
			message(FATAL_ERROR "The Windows launcher needs an SDL2::SDL2 CMake package target")
		endif()
		add_library(openjk_launcher_sdl INTERFACE)
		if(TARGET SDL2::SDL2)
			target_link_libraries(openjk_launcher_sdl INTERFACE SDL2::SDL2)
		else()
			target_include_directories(openjk_launcher_sdl INTERFACE ${SDL2_INCLUDE_DIRS})
			target_link_libraries(openjk_launcher_sdl INTERFACE ${SDL2_LIBRARIES})
		endif()
	endif()

	set(NFD_BUILD_TESTS OFF)
	set(NFD_INSTALL OFF)
	set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
	if(UNIX AND NOT APPLE)
		set(NFD_PORTAL ON)
	endif()
	FetchContent_Declare(nfd
		GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
		GIT_TAG 5381ddbf0b33d3b7b4c5679b0e796d77848a158d
		GIT_SUBMODULES_RECURSE TRUE
		EXCLUDE_FROM_ALL)
	FetchContent_MakeAvailable(nfd)

	target_sources(openjk-launcher PRIVATE ui.cpp ui.h ui_backend.cpp ui_backend.h
		music_synth.cpp music_synth.h music_player.cpp music_player.h
		"${RMLUI_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp"
		"${RMLUI_SOURCE_DIR}/Backends/RmlUi_Renderer_GL2.cpp")
	target_include_directories(openjk-launcher PRIVATE "${RMLUI_SOURCE_DIR}/Backends")
	target_compile_definitions(openjk-launcher PRIVATE OPENJK_LAUNCHER_UI=1
		RMLUI_SDL_VERSION_MAJOR=2 SDL_MAIN_HANDLED)
	target_link_libraries(openjk-launcher PRIVATE RmlUi::Core OpenGL::GL Threads::Threads
		openjk_launcher_sdl nfd::nfd)
	if(WIN32)
		set_property(TARGET openjk-launcher PROPERTY WIN32_EXECUTABLE TRUE)
		target_link_libraries(openjk-launcher PRIVATE shell32)
		if(UseInternalSDL2)
			if(CMAKE_SIZEOF_VOID_P EQUAL 4)
				set(launcher_sdl_dll "${OpenJKLibDir}/SDL2/bin/x86/SDL2.dll")
			else()
				set(launcher_sdl_dll "${OpenJKLibDir}/SDL2/bin/x64/SDL2.dll")
			endif()
			add_custom_command(TARGET openjk-launcher POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${launcher_sdl_dll}" "$<TARGET_FILE_DIR:openjk-launcher>")
			if(NOT BuildSPEngine)
				install(FILES "${launcher_sdl_dll}" DESTINATION ${JKASPInstallDir} COMPONENT ${JKASPClientComponent})
			endif()
		endif()
		if(NOT UseInternalSDL2)
			install(FILES $<TARGET_RUNTIME_DLLS:openjk-launcher>
				DESTINATION ${JKASPInstallDir} COMPONENT ${JKASPClientComponent} OPTIONAL)
		endif()
	endif()

	set(launcher_music "${CMAKE_CURRENT_SOURCE_DIR}/music/cantina-band.score")

	if(APPLE AND MakeApplicationBundles)
		set(launcher_build_resource_dir "$<TARGET_BUNDLE_DIR:openjk-launcher>/Contents/Resources/launcher")
	else()
		set(launcher_build_resource_dir "$<TARGET_FILE_DIR:openjk-launcher>/launcher-resources")
	endif()
	set_property(TARGET openjk-launcher APPEND PROPERTY LINK_DEPENDS
		"${launcher_music}" "${CMAKE_CURRENT_SOURCE_DIR}/ui/launcher.rml"
		"${CMAKE_CURRENT_SOURCE_DIR}/ui/launcher.rcss" "${CMAKE_CURRENT_SOURCE_DIR}/music/CREDITS.md")
	add_custom_command(TARGET openjk-launcher POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${launcher_build_resource_dir}/fonts"
		COMMAND ${CMAKE_COMMAND} -E rm -f "${launcher_build_resource_dir}/cantina-band.wav"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/ui/launcher.rml"
			"${CMAKE_CURRENT_SOURCE_DIR}/ui/launcher.rcss" "${launcher_music}"
			"${CMAKE_CURRENT_SOURCE_DIR}/music/CREDITS.md" "${launcher_build_resource_dir}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ui/fonts/plex/IBMPlexSans-SemiBold.ttf"
			"${CMAKE_SOURCE_DIR}/ui/fonts/plex/IBMPlexMono-Regular.ttf" "${launcher_build_resource_dir}/fonts")
	if(NOT (APPLE AND MakeApplicationBundles))
		install(FILES ui/launcher.rml ui/launcher.rcss "${launcher_music}" music/CREDITS.md
			DESTINATION "${JKASPInstallDir}/launcher" COMPONENT ${JKASPClientComponent})
		install(FILES "${CMAKE_SOURCE_DIR}/ui/fonts/plex/IBMPlexSans-SemiBold.ttf"
			"${CMAKE_SOURCE_DIR}/ui/fonts/plex/IBMPlexMono-Regular.ttf"
			DESTINATION "${JKASPInstallDir}/launcher/fonts" COMPONENT ${JKASPClientComponent})
	endif()
	install(FILES "${nfd_SOURCE_DIR}/LICENSE"
		DESTINATION "${JKASPInstallDir}/licenses/nativefiledialog-extended" COMPONENT ${JKASPClientComponent})
endif()

add_executable(openjk-import-jo import_jo_main.cpp)
set_target_properties(openjk-import-jo PROPERTIES OUTPUT_NAME "${JOImporterExecutable}")
target_compile_features(openjk-import-jo PRIVATE cxx_std_17)
target_link_libraries(openjk-import-jo PRIVATE openjk_jo_import)
if(MINGW)
	target_compile_definitions(openjk-launcher PRIVATE OPENJK_LAUNCHER_RC_MANIFEST=1)
	target_link_options(openjk-launcher PRIVATE -municode)
	target_link_options(openjk-import-jo PRIVATE -municode)
endif()

if(APPLE AND MakeApplicationBundles)
	install(TARGETS openjk-launcher BUNDLE DESTINATION ${JKAInstallDir} COMPONENT ${JKASPClientComponent})
	install(TARGETS openjk-import-jo RUNTIME DESTINATION ${JKASPInstallDir} COMPONENT ${JKASPClientComponent})
else()
	install(TARGETS openjk-launcher openjk-import-jo
		RUNTIME DESTINATION ${JKASPInstallDir}
		COMPONENT ${JKASPClientComponent})
endif()
install(FILES "${miniz_SOURCE_DIR}/LICENSE"
	DESTINATION "${JKASPInstallDir}/licenses/miniz"
	COMPONENT ${JKASPClientComponent})
install(FILES "${CMAKE_SOURCE_DIR}/docs/launcher.md"
	DESTINATION ${JKASPInstallDir}
	COMPONENT ${JKASPClientComponent})
if(UNIX AND NOT APPLE AND BuildRmlUi)
	install(PROGRAMS openjedvibe.desktop
		DESTINATION ${JKASPInstallDir}
		COMPONENT ${JKASPClientComponent})
endif()
if(APPLE AND MakeApplicationBundles)
	set(launcher_bundle_helpers)
	if(BuildSPEngine)
		list(APPEND launcher_bundle_helpers "${SPEngine}")
	endif()
	if(BuildSPRdVanilla)
		list(APPEND launcher_bundle_helpers "${SPRDVanillaRenderer}${CMAKE_SHARED_LIBRARY_SUFFIX}")
	endif()
	if(BuildSPRend2)
		list(APPEND launcher_bundle_helpers "${SPRend2}${CMAKE_SHARED_LIBRARY_SUFFIX}")
	endif()
	if(BuildSPGame)
		list(APPEND launcher_bundle_helpers "OpenJK/${SPGame}${CMAKE_SHARED_LIBRARY_SUFFIX}")
	endif()
	install(CODE "
		include(BundleUtilities)
		set(BU_CHMOD_BUNDLE_ITEMS ON)
		set(bundle_path \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${JKAInstallDir}/${JKASPBundleName}.app\")
		set(bundle_helpers)
		foreach(helper ${launcher_bundle_helpers})
			list(APPEND bundle_helpers \"\${bundle_path}/Contents/MacOS/\${helper}\")
		endforeach()
		fixup_bundle(\"\${bundle_path}\" \"\${bundle_helpers}\" \"\")
		" COMPONENT ${JKASPClientComponent})
endif()

if(BuildTests)
	if(BuildRmlUi)
		add_executable(openjk-launcher-music-tests "${CMAKE_SOURCE_DIR}/tests/launcher_music.cpp"
			music_synth.cpp music_player.cpp)
		target_compile_features(openjk-launcher-music-tests PRIVATE cxx_std_17)
		target_include_directories(openjk-launcher-music-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
		target_compile_definitions(openjk-launcher-music-tests PRIVATE SDL_MAIN_HANDLED)
		target_link_libraries(openjk-launcher-music-tests PRIVATE openjk_launcher_sdl)
		add_test(NAME launcher-synth COMMAND openjk-launcher-music-tests "${launcher_music}")
	endif()
	find_package(Python3 REQUIRED COMPONENTS Interpreter)
	add_test(NAME launcher-music COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/test-launcher-music.py")
	add_test(NAME native-jo-import COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/test-import-jo.py")
	set_tests_properties(native-jo-import PROPERTIES ENVIRONMENT
		"OPENJK_IMPORT_JO=$<TARGET_FILE:openjk-import-jo>;OPENJK_LAUNCHER=$<TARGET_FILE:openjk-launcher>")
endif()
