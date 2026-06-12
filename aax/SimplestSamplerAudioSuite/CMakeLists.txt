cmake_minimum_required(VERSION 3.13)

project(SimplestSamplerAudioSuite VERSION 1.0.0 LANGUAGES CXX OBJCXX)

aax_plugin(
    TARGET_NAME SimplestSamplerAudioSuite
    OUTPUT_NAME SimplestSamplerAudioSuite
    VERSION ${PROJECT_VERSION}
    COPYRIGHT_STRING "Copyright 2026 Take One Audio"
    BUNDLE_IDENTIFIER "com.hellojamison.simplestsampler.audiosuite"
)

target_sources(SimplestSamplerAudioSuite
    PRIVATE
        Source/SimplestSampler_AS_Describe.cpp
        Source/SimplestSampler_AS_Parameters.cpp
        Source/SimplestSampler_AS_HostProcessor.cpp
        Source/SimplestSampler_AS_GUI.mm
        Source/CaptureWavWriter.cpp
        Source/PluginLibraryBridge.cpp
        "${AAX_SDK_ROOT}/Extensions/GUI/AAX_CEffectGUI_Cocoa.mm"
)

target_include_directories(SimplestSamplerAudioSuite
    PRIVATE
        Source
        "${AAX_SDK_ROOT}/Extensions/GUI"
)

if(APPLE)
    set_target_properties(SimplestSamplerAudioSuite PROPERTIES
        OSX_ARCHITECTURES "arm64"
        MACOSX_RPATH ON
    )
    target_link_libraries(SimplestSamplerAudioSuite PRIVATE
        "-framework AppKit"
        "-framework Cocoa"
    )
endif()
