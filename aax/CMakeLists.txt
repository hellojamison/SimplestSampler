cmake_minimum_required(VERSION 3.13)

project(SimplestSamplerAAX LANGUAGES CXX)

if(NOT DEFINED AAX_SDK_ROOT)
    if(DEFINED ENV{AAX_SDK_ROOT})
        set(AAX_SDK_ROOT "$ENV{AAX_SDK_ROOT}")
    else()
        set(AAX_SDK_ROOT "/Users/jamisonrabbe/Downloads/aax-sdk-2-9-0")
    endif()
endif()

if(NOT EXISTS "${AAX_SDK_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "AAX_SDK_ROOT does not point to the AAX SDK: ${AAX_SDK_ROOT}")
endif()

set(AAX_SDK_BUILD_DIR "${CMAKE_BINARY_DIR}/../aax-sdk-build" CACHE PATH "AAX SDK cmake build directory")

if(NOT EXISTS "${AAX_SDK_BUILD_DIR}/AAX_SDKConfig.cmake")
    message(STATUS "Configuring AAX SDK at ${AAX_SDK_BUILD_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S "${AAX_SDK_ROOT}"
            -B "${AAX_SDK_BUILD_DIR}"
            -G Xcode
            -DAAX_BUILD_EXAMPLES=OFF
        RESULT_VARIABLE _aax_sdk_configure_result
    )
    if(NOT _aax_sdk_configure_result EQUAL 0)
        message(FATAL_ERROR "Failed to configure AAX SDK (exit ${_aax_sdk_configure_result})")
    endif()
endif()

set(AAX_SDK_DIR "${AAX_SDK_BUILD_DIR}" CACHE PATH "AAX SDK package directory")
find_package(AAX_SDK REQUIRED)

add_subdirectory(SimplestSamplerAudioSuite)
