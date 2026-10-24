cmake_minimum_required(VERSION 3.23)
# Open Transport Bug Patcher: stops Open Transport 1.3 offering TCP window scaling, which
# it mishandles (see README.md).
project(OpenTransportBugPatcher)

set(CMAKE_C_STANDARD 99)
SET(CMAKE_C_FLAGS "-Os")

add_application(OpenTransportBugPatcher
    src/main.c
    src/patch.c
    src/resources.r
    CREATOR "OTwF"
)

add_custom_target(build_appl ALL DEPENDS OpenTransportBugPatcher_APPL)

# Retro68 leaves the Finder flags at 0; turn on hasBundle and hasCustomIcon so
# the Finder shows the app's icon. The script is in the Speed Test's tools.
add_custom_command(TARGET OpenTransportBugPatcher_APPL POST_BUILD
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/set_finder_flags.py ${CMAKE_CURRENT_BINARY_DIR} OpenTransportBugPatcher
    COMMENT "Setting the Finder flags")

# A target name can't have spaces, so Rez copies the app once more under the
# name the Finder shows: "Open Transport Bug Patcher" (.bin, .dsk, .APPL).
add_custom_command(TARGET OpenTransportBugPatcher_APPL POST_BUILD
    COMMAND ${REZ} --copy OpenTransportBugPatcher.bin -o "Open Transport Bug Patcher.bin"
            --cc "Open Transport Bug Patcher.dsk" --cc "Open Transport Bug Patcher.APPL"
            --cc "%Open Transport Bug Patcher.ad" -t APPL -c OTwF
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/set_finder_flags.py ${CMAKE_CURRENT_BINARY_DIR} "Open Transport Bug Patcher"
    COMMENT "Copying the app as Open Transport Bug Patcher"
    VERBATIM)
