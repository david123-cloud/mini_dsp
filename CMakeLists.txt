cmake_minimum_required(VERSION 3.16)
project(mini_dsp LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF) # strict -std=c11, no GNU extensions

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MINI_DSP_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(MINI_DSP_WERROR "Promote compiler warnings to errors" OFF)

set(MINI_DSP_CORE_SOURCES
    framework/dsp_error.c
    framework/dsp_pipeline.c
    framework/dsp_block.c
    framework/fft.c
    module/biquad.c
    module/agc.c
    module/ns.c
    audio/wav.c
    audio/dsp_convert.c
)

add_library(dsp_core STATIC ${MINI_DSP_CORE_SOURCES})
target_include_directories(dsp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/framework
    ${CMAKE_CURRENT_SOURCE_DIR}/module
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dsp_core PRIVATE
        -Wall -Wextra -Wpedantic
        -Wshadow
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wundef
        -Wwrite-strings
    )
    if(MINI_DSP_WERROR)
        target_compile_options(dsp_core PRIVATE -Werror)
    endif()
    if(MINI_DSP_SANITIZE)
        target_compile_options(dsp_core PUBLIC
            -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(dsp_core PUBLIC -fsanitize=address,undefined)
    endif()
elseif(MSVC)
    target_compile_options(dsp_core PRIVATE /W4)
endif()

add_executable(dsp_demo
    demo/main.c
    demo/demo_wav.c
    demo/demo_fft.c
)
target_link_libraries(dsp_demo PRIVATE dsp_core)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dsp_demo PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(MINI_DSP_WERROR)
        target_compile_options(dsp_demo PRIVATE -Werror)
    endif()
    if(MINI_DSP_SANITIZE)
        target_compile_options(dsp_demo PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(dsp_demo PRIVATE -fsanitize=address,undefined)
    endif()
endif()
if(NOT MSVC)
    target_link_libraries(dsp_demo PRIVATE m)
endif()

# Self-checking test for the module algorithms (biquad EQ shapes, AGC, NS).
add_executable(module_selftest tests/module_selftest.c)
target_link_libraries(module_selftest PRIVATE dsp_core)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(module_selftest PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(MINI_DSP_WERROR)
        target_compile_options(module_selftest PRIVATE -Werror)
    endif()
    if(MINI_DSP_SANITIZE)
        target_compile_options(module_selftest PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(module_selftest PRIVATE -fsanitize=address,undefined)
    endif()
endif()
if(NOT MSVC)
    target_link_libraries(module_selftest PRIVATE m)
endif()

# CTest registration is opt-in: the demos are meant to be run by hand during
# your own experiments.  Turn it back on with -DMINI_DSP_REGISTER_TESTS=ON.
option(MINI_DSP_REGISTER_TESTS "Register the demos with CTest" OFF)

if(MINI_DSP_REGISTER_TESTS)
    enable_testing()

    # The FFT demo is fully self-checking: it returns non-zero on any failure.
    add_test(NAME dsp_fft_demo COMMAND dsp_demo fft)

    # The WAV demo needs tests/temp.wav, so it runs from the project root and
    # writes its result into the build directory (never into tests/).
    add_test(NAME dsp_wav_demo
        COMMAND dsp_demo wav tests/temp.wav ${CMAKE_CURRENT_BINARY_DIR}/tests_output.wav
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(NAME module_selftest COMMAND module_selftest)
endif()
