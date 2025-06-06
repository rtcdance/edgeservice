cmake_minimum_required(VERSION 3.10)
project(edgeservice)
set(CMAKE_CXX_STANDARD 17)

# vcpkg toolchain
set(VCPKG_CMAKE_DIR "${CMAKE_SOURCE_DIR}/3rdparty/vcpkg/scripts/buildsystems/vcpkg.cmake")
if(EXISTS "${VCPKG_CMAKE_DIR}")
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_CMAKE_DIR}" CACHE STRING "Vcpkg toolchain file")
endif()

# 只用vcpkg的find_package和CMake targets
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/3rdparty/include/json/include
    ${CMAKE_SOURCE_DIR}/3rdparty/include/spdlog/include
    ${CMAKE_SOURCE_DIR}/3rdparty/include/cpp-httplib
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http
    ${CMAKE_CURRENT_SOURCE_DIR}/src/grpc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/inspect
    ${CMAKE_CURRENT_SOURCE_DIR}/src/proto
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

include(FindProtobuf)
include(${CMAKE_CURRENT_LIST_DIR}/vcpkg_installed/x64-osx/share/grpc/gRPCConfig.cmake OPTIONAL)
include(${CMAKE_CURRENT_LIST_DIR}/vcpkg_installed/x64-osx/share/protobuf/protobuf-generate.cmake OPTIONAL)

set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/edgeservice.proto
)

# 生成 pb 代码
protobuf_generate_cpp(
    PROTO_SRCS PROTO_HDRS ${PROTO_FILES}
    PROTOC_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/proto
)

# gRPC 代码生成
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/proto/edgeservice.grpc.pb.cc ${CMAKE_CURRENT_SOURCE_DIR}/src/proto/edgeservice.grpc.pb.h
    COMMAND protobuf::protoc
    ARGS --grpc_out=${CMAKE_CURRENT_SOURCE_DIR}/src/proto
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
         ${CMAKE_CURRENT_SOURCE_DIR}/proto/edgeservice.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/edgeservice.proto
    COMMENT "Generating gRPC sources for edgeservice.proto"
)

set(GRPC_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/proto/edgeservice.grpc.pb.cc)
set(GRPC_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/src/proto/edgeservice.grpc.pb.h)

set(SRC
    src/main.cpp
    src/http/http_server.cpp
    src/inspect/inspect_impl.cpp
    src/grpc/grpc_server.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

add_library(utils STATIC
    src/utils/http_utils.h
    src/utils/config_utils.cpp
)

target_include_directories(utils PUBLIC src/utils)
target_link_libraries(utils PUBLIC)

add_executable(edgeservice ${SRC})

target_link_libraries(edgeservice PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
    protobuf::libprotoc
    absl::log_severity
    absl::raw_logging_internal
    absl::strings
    absl::base
    absl::throw_delegate
    absl::time
    absl::time_zone
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    z
    utils
)

if(UNIX AND NOT APPLE)
    target_link_libraries(edgeservice PRIVATE uuid)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(INSTALL_SUBDIR "Debug")
else()
    set(INSTALL_SUBDIR "Release")
endif()

install(TARGETS edgeservice 
        DESTINATION ${INSTALL_SUBDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/conf DESTINATION ${INSTALL_SUBDIR})