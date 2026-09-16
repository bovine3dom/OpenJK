# The official SDK includes the CPU implementation and its dependencies.
set(SteamAudioSupported OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
	set(SteamAudioSupported ON)
endif()
option(BuildSteamAudio "Build SP environmental audio with Steam Audio" ${SteamAudioSupported})
if(BuildSteamAudio)
	if(NOT SteamAudioSupported)
		message(FATAL_ERROR "The packaged Steam Audio backend currently supports Linux x86_64")
	endif()
	include(FetchContent)
	FetchContent_Declare(steamaudio
		URL https://github.com/ValveSoftware/steam-audio/releases/download/v4.8.1/steamaudio_4.8.1.zip
		URL_HASH SHA256=4a0aa5ec1176f38f0b0993a37c2259d9e86f27e22d5e24f83ec4c3cb9a1d5449
		SOURCE_SUBDIR unused)
	FetchContent_MakeAvailable(steamaudio)
	file(DOWNLOAD https://raw.githubusercontent.com/ValveSoftware/steam-audio/v4.8.1/LICENSE.md
		"${steamaudio_BINARY_DIR}/LICENSE.md"
		EXPECTED_HASH SHA256=cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30
		TLS_VERIFY ON)
	add_library(SteamAudio SHARED IMPORTED)
	find_package(Threads REQUIRED)
	set_target_properties(SteamAudio PROPERTIES
		IMPORTED_LOCATION "${steamaudio_SOURCE_DIR}/lib/linux-x64/libphonon.so"
		INTERFACE_INCLUDE_DIRECTORIES "${steamaudio_SOURCE_DIR}/include")
	list(APPEND SPEngineLibraries SteamAudio Threads::Threads)
	list(APPEND SPEngineDefines USE_STEAM_AUDIO)
	list(APPEND SPEngineFiles "${SharedDir}/sound/steam_audio.cpp")
	install(FILES "${steamaudio_SOURCE_DIR}/lib/linux-x64/libphonon.so" DESTINATION JediAcademy)
	install(FILES "${steamaudio_SOURCE_DIR}/THIRDPARTY.md" DESTINATION JediAcademy/licenses/steamaudio)
	install(FILES "${steamaudio_BINARY_DIR}/LICENSE.md" DESTINATION JediAcademy/licenses/steamaudio)
endif()
