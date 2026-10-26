cmake_minimum_required(VERSION 3.13)

# WICHTIG / IMPORTANT: Board und Plattform MÜSSEN vor dem Einbinden des SDK gesetzt werden.
# Board/platform MUST be set before including the SDK, otherwise you silently get an RP2040 build.
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include(pico_sdk_import.cmake)

project(c64_visualizer C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ---- Build options (use -DNAME=value on the cmake command line) ----
set(DISPLAY "WS2812" CACHE STRING "Display type: WS2812 (8x32 RGB matrix) or MAX7219")
set_property(CACHE DISPLAY PROPERTY STRINGS WS2812 MAX7219)
set(MAX7219_MODULES 4 CACHE STRING "Number of chained 8x8 MAX7219 modules (1 = single 8x8, 4 = 4-in-1 8x32)")
set(POWER_LIMIT_MA 350 CACHE STRING "WS2812 current limit in mA (350 = USB powered, 1500 = external 5V/2A supply)")
option(DEBUG_USB "Enable USB serial debug output" OFF)

add_executable(c64_visualizer
    src/main.c
    src/textfx.c
    src/analysis.c
    src/stereo.c
    src/light.c
    src/light_calc.c
    src/audio_capture.c
    src/visuals.c
    src/settings.c
)

# ---- Demo: built-in chiptune instead of the ADC, plays texts and every mode for 5 s ----
add_executable(c64viz_demo
    src/demo_main.c
    src/demo_audio.c
    src/analysis.c
    src/visuals.c
    src/textfx.c
    src/settings.c
)
target_link_libraries(c64viz_demo hardware_flash hardware_sync hardware_watchdog)

# ---- Matrix test: scrolling text with rainbow, no audio needed ----
add_executable(matrix_test
    src/matrix_test.c
)

set(ALL_TARGETS c64_visualizer matrix_test c64viz_demo)

# ---- Pico 2 W only (cmake -DPICO_BOARD=pico2_w): visualizer + WLAN (web page, Ultimate audio stream) ----
if(PICO_CYW43_SUPPORTED)
    add_executable(c64viz_wifi
        src/main.c
        src/textfx.c
        src/analysis.c
        src/stereo.c
        src/light.c
        src/light_calc.c
        src/audio_capture.c
        src/visuals.c
        src/settings.c
        src/net/net.c
        src/net/dhcpserver.c
        src/net/md5.c
        src/net/jukebox.c
        src/net/diag.c
        src/net/ota.c
        src/net/ota_apply.c
        src/net/mdns_lite.c
        src/net/u64scan.c
        src/net/jbuf.c
        src/net/devices.c
        src/net/wide.c
        src/net/news.c
        src/timeutil.c
    )
    target_compile_definitions(c64viz_wifi PRIVATE C64VIZ_WIFI C64VIZ_WIDE CYW43_HOST_NAME="c64viz")
    target_include_directories(c64viz_wifi PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/net)
    target_link_libraries(c64viz_wifi pico_cyw43_arch_lwip_threadsafe_background pico_rand pico_lwip_sntp
                          hardware_adc hardware_dma hardware_flash hardware_sync hardware_watchdog hardware_i2c)
    if(DEBUG_USB)
        pico_enable_stdio_usb(c64viz_wifi 1)
        target_compile_definitions(c64viz_wifi PRIVATE DEBUG_USB)
    else()
        pico_enable_stdio_usb(c64viz_wifi 0)
    endif()
    pico_enable_stdio_uart(c64viz_wifi 0)
    list(APPEND ALL_TARGETS c64viz_wifi)
endif()

if(DISPLAY STREQUAL "WS2812")
    foreach(t ${ALL_TARGETS})
        target_sources(${t} PRIVATE src/display_ws2812.c)
        pico_generate_pio_header(${t} ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
        target_link_libraries(${t} hardware_pio hardware_clocks hardware_dma)
        target_compile_definitions(${t} PRIVATE DISPLAY_WS2812 POWER_LIMIT_MA=${POWER_LIMIT_MA})
    endforeach()
elseif(DISPLAY STREQUAL "MAX7219")
    foreach(t ${ALL_TARGETS})
        target_sources(${t} PRIVATE src/display_max7219.c)
        target_link_libraries(${t} hardware_spi)
        target_compile_definitions(${t} PRIVATE DISPLAY_MAX7219 MAX7219_MODULES=${MAX7219_MODULES})
    endforeach()
else()
    message(FATAL_ERROR "DISPLAY must be WS2812 or MAX7219")
endif()

foreach(t ${ALL_TARGETS})
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
    target_link_libraries(${t} pico_stdlib pico_bootrom hardware_gpio)
endforeach()
target_link_libraries(c64_visualizer hardware_adc hardware_dma hardware_flash hardware_sync hardware_watchdog hardware_i2c)

if(DEBUG_USB)
    pico_enable_stdio_usb(c64_visualizer 1)
    target_compile_definitions(c64_visualizer PRIVATE DEBUG_USB)
else()
    pico_enable_stdio_usb(c64_visualizer 0)
endif()
pico_enable_stdio_uart(c64_visualizer 0)

pico_enable_stdio_usb(matrix_test 0)
pico_enable_stdio_usb(c64viz_demo 0)
pico_enable_stdio_uart(c64viz_demo 0)
pico_enable_stdio_uart(matrix_test 0)
pico_add_extra_outputs(c64_visualizer)
pico_add_extra_outputs(matrix_test)
pico_add_extra_outputs(c64viz_demo)
if(PICO_CYW43_SUPPORTED)
    pico_add_extra_outputs(c64viz_wifi)
endif()
