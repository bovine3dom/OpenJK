function(openjk_add_rmlui_dependencies)
	# Keep dependency settings local. Do not change other OpenJK libraries.
	set(BUILD_SHARED_LIBS OFF)
	set(BUILD_FRAMEWORK OFF)
	set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
	set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/cache" CACHE PATH "FetchContent cache directory")
	include(FetchContent)
	foreach(feature ZLIB BZIP2 PNG HARFBUZZ BROTLI)
		set(FT_DISABLE_${feature} ON)
		set(FT_REQUIRE_${feature} OFF)
		# FreeType also checks inherited discovery results when it configures features.
		set(${feature}_FOUND FALSE)
	endforeach()
	set(HarfBuzz_FOUND FALSE)
	set(BROTLIDEC_FOUND FALSE)
	set(FT_ENABLE_ERROR_STRINGS OFF)
	set(SKIP_INSTALL_ALL ON)
	# FreeType includes CPack. Keep its generated files separate from OpenJK's.
	set(CPACK_OUTPUT_CONFIG_FILE "${CMAKE_BINARY_DIR}/cache/FreeTypeCPackConfig.cmake")
	set(CPACK_SOURCE_OUTPUT_CONFIG_FILE "${CMAKE_BINARY_DIR}/cache/FreeTypeCPackSourceConfig.cmake")
	FetchContent_Declare(freetype
		URL https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz
		URL_HASH SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289
		EXCLUDE_FROM_ALL)
	FetchContent_MakeAvailable(freetype)
	add_library(Freetype::Freetype ALIAS freetype)
	# RmlUi accepts an existing target; do not search for a system FreeType.
	set(CMAKE_DISABLE_FIND_PACKAGE_Freetype TRUE)

	set(RMLUI_FONT_ENGINE freetype)
	foreach(feature SAMPLES TESTS SHELL LUA_BINDINGS LOTTIE_PLUGIN SVG_PLUGIN
		HARFBUZZ_SAMPLE TRACY_PROFILING THIRDPARTY_CONTAINERS PRECOMPILED_HEADERS
		COMPILER_OPTIONS WARNINGS_AS_ERRORS CUSTOM_CONFIGURATION
		BACKEND_SIMULATE_TOUCH IME_SAMPLE_USE_NOTO_FONTS INSTALL_LICENSES_AND_BUILD_INFO)
		set(RMLUI_${feature} OFF)
	endforeach()
	set(RMLUI_INSTALL_DEPENDENCIES_DIR "")
	FetchContent_Declare(rmlui
		URL https://codeload.github.com/mikke89/RmlUi/tar.gz/refs/tags/6.3
		URL_HASH SHA256=d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede
		EXCLUDE_FROM_ALL)
	FetchContent_MakeAvailable(rmlui)

	file(SHA256 "${CMAKE_SOURCE_DIR}/ui/fonts/plex/IBMPlexMono-Regular.ttf" plex_hash)
	if(NOT plex_hash STREQUAL "fe11304a5fe956d5744e9b6a246cc83d90425245e75a62230044966ca96a7f50")
		message(FATAL_ERROR "IBM Plex Mono does not match its pinned source hash")
	endif()
	set(font_install_dir "${JKAInstallDir}/OpenJK/ui/fonts")
	if(APPLE AND MakeApplicationBundles)
		set(font_install_dir "${JKAInstallDir}/${SPEngine}.app/Contents/MacOS/OpenJK/ui/fonts")
	endif()
	install(DIRECTORY "${CMAKE_SOURCE_DIR}/ui/fonts/plex"
		DESTINATION "${font_install_dir}" COMPONENT ${JKASPClientComponent})

	install(FILES "${CMAKE_SOURCE_DIR}/docs/rmlui-dependencies.md"
		"${CMAKE_SOURCE_DIR}/docs/rmlui-reticle.md"
		"${CMAKE_SOURCE_DIR}/docs/force-wheel.md"
		DESTINATION "${JKAInstallDir}" COMPONENT ${JKASPClientComponent})
	install(FILES "${rmlui_SOURCE_DIR}/LICENSE.txt"
		DESTINATION "${JKAInstallDir}/licenses/rmlui" COMPONENT ${JKASPClientComponent})
	install(FILES "${freetype_SOURCE_DIR}/LICENSE.TXT"
		"${freetype_SOURCE_DIR}/docs/FTL.TXT"
		"${freetype_SOURCE_DIR}/docs/GPLv2.TXT"
		"${freetype_SOURCE_DIR}/src/gzip/zlib.h"
		DESTINATION "${JKAInstallDir}/licenses/freetype" COMPONENT ${JKASPClientComponent})
endfunction()
