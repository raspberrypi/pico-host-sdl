# Overview

This repository adds additional functionality to `PICO_PLATOFRM=host` to allow testing of `pico_audio_` and `pico_scanvideo` related applications in the host environment. It is not intended to be a complete simulator for the RP2040!!!!

It additionally provides support for `pico_multicore_` and support for `pico_time` timers.

# Using 
To use, configure your _host_ mode CMake build with:

`-DPICO_PLATFORM=host -DPICO_SDK_PRE_LIST_DIRS=<path_to>/pico_host_sdl`

You will get audio and video and two core support with semaphores/spinlocks etc via SDL.

# Notes

This has only been tested on macOS and Linux operating systems. It will _NOT_ work with the MSVC compiler, however it might work on Windows if you build with gcc or WSL2

The setup/build is currently a little convoluted because `pico-extras` is needed by this repository even if your application isn't using it (i.e. just uses the bare `pico-sdk`)