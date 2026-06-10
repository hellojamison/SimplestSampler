cmake_minimum_required(VERSION 3.28)

project(ptsl_markers_helper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

option(PTSL_USE_SYSTEM_GRPC "Use installed grpc/protobuf packages" OFF)
set(PTSL_GRPC_SOURCE_DIR "" CACHE PATH "Path to a grpc source checkout with submodules")

if(PTSL_USE_SYSTEM_GRPC)
  find_package(Protobuf CONFIG REQUIRED)
  find_package(gRPC CONFIG REQUIRED)
endif()

if(NOT PTSL_USE_SYSTEM_GRPC)
  if(NOT EXISTS "${PTSL_GRPC_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "PTSL_GRPC_SOURCE_DIR must point at a grpc checkout with submodules")
  endif()

  set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
  set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_LIBPROTOC OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(utf8_range_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
  set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_CPP_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE BOOL "" FORCE)
  set(gRPC_SSL_PROVIDER "module" CACHE STRING "" FORCE)
  set(gRPC_ZLIB_PROVIDER "package" CACHE STRING "" FORCE)
  set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "" FORCE)
  set(gRPC_ABSL_PROVIDER "module" CACHE STRING "" FORCE)
  set(gRPC_CARES_PROVIDER "module" CACHE STRING "" FORCE)
  set(gRPC_RE2_PROVIDER "module" CACHE STRING "" FORCE)

  add_subdirectory("${PTSL_GRPC_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/grpc" EXCLUDE_FROM_ALL)
endif()

if(TARGET gRPC::grpc++)
  set(PTSL_GRPC_TARGET gRPC::grpc++)
elseif(TARGET grpc++)
  set(PTSL_GRPC_TARGET grpc++)
else()
  message(FATAL_ERROR "Could not find a grpc++ target after configuring dependencies")
endif()

if(TARGET protobuf::libprotobuf)
  set(PTSL_PROTOBUF_TARGET protobuf::libprotobuf)
elseif(TARGET libprotobuf)
  set(PTSL_PROTOBUF_TARGET libprotobuf)
else()
  message(FATAL_ERROR "Could not find a libprotobuf target after configuring dependencies")
endif()

add_executable(ptsl_markers_helper
  src/ptsl_markers_helper.cpp
)

target_include_directories(ptsl_markers_helper PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

foreach(version_part IN ITEMS MAJOR MINOR REVISION)
  if(DEFINED PTSL_PROTOCOL_VERSION_${version_part})
    target_compile_definitions(ptsl_markers_helper PRIVATE
      PTSL_PROTOCOL_VERSION_${version_part}=${PTSL_PROTOCOL_VERSION_${version_part}}
    )
  endif()
endforeach()

target_link_libraries(ptsl_markers_helper PRIVATE
  ${PTSL_GRPC_TARGET}
  ${PTSL_PROTOBUF_TARGET}
)

if(APPLE)
  find_library(APPLICATION_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
  find_library(CARBON_FRAMEWORK Carbon REQUIRED)

  add_executable(cue_global_hotkey_helper
    src/cue_global_hotkey_helper.cpp
  )

  target_link_libraries(cue_global_hotkey_helper PRIVATE
    ${APPLICATION_SERVICES_FRAMEWORK}
    ${CARBON_FRAMEWORK}
  )
endif()
