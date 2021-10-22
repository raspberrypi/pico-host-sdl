cmake_minimum_required(VERSION 3.13)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)
if (PICO_SDK_VERSION_STRING VERSION_LESS "1.2.0")
    message(FATAL_ERROR "Require at least Raspberry Pi Pico SDK version 1.2.0")
endif()

include(pico_extras_import.cmake)

project(pico_host_sdl)

if (PICO_PLATFORM STREQUAL "host")
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    add_library(pico_host_sdl INTERFACE)

    # need SDL2
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)

    # need math
    find_library(M_LIBRARY m REQUIRED)

    # optionally ALSA
    find_package(ALSA)

    target_include_directories(pico_host_sdl INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        # have both forms of these because windows doesn't have SDL2 prefix
        ${SDL2_INCLUDE_DIR}
        ${SDL2_IMAGE_INCLUDE_DIRS}
        ${SDL2_INCLUDE_DIR}/SDL2
        ${SDL2_IMAGE_INCLUDE_DIRS}/SDL2
        )

    target_link_libraries(pico_host_sdl INTERFACE ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES})

    IF (ALSA_FOUND)
        message("ALSA found")
        target_link_libraries(pico_host_sdl INTERFACE ${ALSA_LIBRARY})
        target_compile_definitions(pico_host_sdl INTERFACE -DNATIVE_AUDIO_ALSA)
    else()
        # todo we should use SDL by default, but because it is asynchronous callbacks\
        # it should really be integrated under our mu driver at the DMA level, which
        # would be fine, except that currently we allow sending DMA transfers of differing lengths
        # which SDL doesn't.
        message("ALSA not found, using SDL")
        target_compile_definitions(pico_host_sdl INTERFACE -DNATIVE_AUDIO_SDL2)
    endif()

    if (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        target_compile_definitions(pico_host_sdl INTERFACE _USE_MATH_DEFINES)
    endif()

    add_library(pico_host_video INTERFACE) # todo currently contains other stuff
    add_library(pico_host_audio INTERFACE)
    add_library(pico_host_timer INTERFACE)

    target_sources(pico_host_video INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/sdl_video.c)

    target_sources(pico_host_audio INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/sdl_audio.c)

    target_sources(pico_host_timer INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/sdl_timer.c
            )

    target_include_directories(pico_host_sdl INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

    target_link_libraries(pico_host_video INTERFACE
            pico_sync
            pico_scanvideo
            pico_multicore
            #pico_sd_card
            #pthread
            pico_host_sdl)

    target_link_libraries(pico_host_audio INTERFACE
            pico_audio
            pico_host_sdl
    )

    target_link_libraries(pico_host_timer INTERFACE
            pico_time
    )

    add_library(pico_sd_card INTERFACE)
    target_sources(pico_sd_card INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/sd_card.c)

    target_link_libraries(pico_sd_card INTERFACE
            pico_sd_card_headers)

    # todo for now everything depends on pico_host_video as that has startup etc.
    add_library(pico_scanvideo_dpi INTERFACE)
    target_link_libraries(pico_scanvideo_dpi INTERFACE pico_host_video pico_scanvideo)

    add_library(pico_audio_i2s INTERFACE)
    target_link_libraries(pico_audio_i2s INTERFACE pico_host_audio pico_host_video)

    add_library(pico_audio_pwm INTERFACE)
    target_link_libraries(pico_audio_pwm INTERFACE pico_host_audio pico_host_video)

    add_library(hardware_timer INTERFACE)

    target_sources(hardware_timer INTERFACE
            ${PICO_SDK_PATH}/src/host/hardware_timer/timer.c)
    target_include_directories(hardware_timer INTERFACE ${PICO_SDK_PATH}/src/host/hardware_timer/include)
    target_link_libraries(hardware_timer INTERFACE pico_host_timer pico_host_video)

    # we support alarms
    set(PICO_TIME_NO_ALARM_SUPPORT "0" CACHE INTERNAL "")
endif()

pico_is_top_level_project(PICO_HOST_SDL_TOP_LEVEL_PROJECT)

# The real work gets done in post_init which is called at the end of pico_sdk_init
list(APPEND PICO_SDK_POST_LIST_FILES ${CMAKE_CURRENT_LIST_DIR}/post_init.cmake)
list(APPEND PICO_CONFIG_HEADER_FILES ${CMAKE_CURRENT_LIST_DIR}/include/pico/pico_host_sdl.h)
if (PICO_HOST_SDL_TOP_LEVEL_PROJECT)
    message("pico_HOST_SDL: initialize SDK since we're the top-level")
    # Initialize the SDK
    pico_sdk_init()
else()
    pico_promote_common_scope_vars()
endif()

