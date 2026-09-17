# Build only Recast. The automap does not need Detour runtime navigation.
include(FetchContent)
FetchContent_Declare(automap_recast
	GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
	GIT_TAG 6dc1667f580357e8a2154c28b7867bea7e8ad3a7
	SOURCE_SUBDIR automap-unused)
FetchContent_MakeAvailable(automap_recast)
file(GLOB AutomapRecastSources CONFIGURE_DEPENDS "${automap_recast_SOURCE_DIR}/Recast/Source/*.cpp")
add_library(automap_recast STATIC ${AutomapRecastSources})
target_include_directories(automap_recast PUBLIC "${automap_recast_SOURCE_DIR}/Recast/Include")
install(FILES "${automap_recast_SOURCE_DIR}/License.txt"
	DESTINATION "${JKASPInstallDir}/licenses/recast"
	COMPONENT ${JKASPClientComponent})
