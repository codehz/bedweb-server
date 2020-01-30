cmake_minimum_required(VERSION 3.3)

project(bedweb-server)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(ExternalProject)

find_package(OpenSSL REQUIRED)
add_library(ssl INTERFACE)
target_link_libraries(ssl INTERFACE OpenSSL::SSL OpenSSL::Crypto)

set(ROOTFS ${CMAKE_BINARY_DIR}/rootfs)
file(MAKE_DIRECTORY ${ROOTFS}/include)

ExternalProject_Add(
  wsrpc
  GIT_REPOSITORY https://github.com/CodeHz/wsrpc
  GIT_SHALLOW ON
  UPDATE_COMMAND ""
  INSTALL_DIR ${ROOTFS}
  CMAKE_ARGS -DOPENSSL=ON -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
)
add_library(libwsrpc INTERFACE IMPORTED)
add_dependencies(libwsrpc wsrpc)
target_include_directories(libwsrpc INTERFACE ${ROOTFS}/include)
target_link_libraries(libwsrpc INTERFACE
  ${ROOTFS}/lib/librpcws.a
  ${ROOTFS}/lib/librpc.a
  ${ROOTFS}/lib/libws.a
  ${ROOTFS}/lib/libminsec.a
  ssl
)

ExternalProject_Add(
  yaml
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp
  GIT_SHALLOW ON
  UPDATE_COMMAND ""
  INSTALL_DIR ${ROOTFS}
  CMAKE_ARGS -DYAML_BUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
)
add_library(libyaml INTERFACE IMPORTED)
add_dependencies(libyaml yaml)
target_include_directories(libyaml INTERFACE ${ROOTFS}/include)
target_link_libraries(libyaml INTERFACE
  ${ROOTFS}/lib/libyaml-cpp.a
)

ExternalProject_Add(
  cpuid
  GIT_REPOSITORY https://github.com/anrieff/libcpuid
  GIT_SHALLOW ON
  UPDATE_COMMAND ""
  INSTALL_DIR ${ROOTFS}
  CONFIGURE_COMMAND libtoolize && autoreconf --install && ./configure --prefix=<INSTALL_DIR>
  BUILD_IN_SOURCE ON
  BUILD_COMMAND make
  INSTALL_COMMAND make install
)
add_library(libcpuid INTERFACE IMPORTED)
add_dependencies(libcpuid cpuid)
target_include_directories(libcpuid INTERFACE ${ROOTFS}/include)
target_link_libraries(libcpuid INTERFACE
  ${ROOTFS}/lib/libcpuid.a
)

find_package(Boost REQUIRED COMPONENTS system)

file(GLOB_RECURSE sources LIST_DIRECTORIES false CONFIGURE_DEPENDS src/*.cpp)
add_executable(bedweb ${sources})
target_link_libraries(bedweb libwsrpc libyaml libcpuid Boost::system util)
set_property(TARGET bedweb PROPERTY CXX_STANDARD 17)