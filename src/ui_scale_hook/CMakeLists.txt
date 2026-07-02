cmake_minimum_required(VERSION 3.15)
project(HytaleUIScaleHook C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MINHOOK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/minhook")
if(EXISTS "${MINHOOK_ROOT}/include/MinHook.h")
  set(minhook_SOURCE_DIR "${MINHOOK_ROOT}")
else()
  include(FetchContent)
  FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
  )
  FetchContent_GetProperties(minhook)
  if(NOT minhook_POPULATED)
    FetchContent_Populate(minhook)
  endif()
endif()

set(MINHOOK_SOURCES
  ${minhook_SOURCE_DIR}/src/buffer.c
  ${minhook_SOURCE_DIR}/src/hook.c
  ${minhook_SOURCE_DIR}/src/trampoline.c
  ${minhook_SOURCE_DIR}/src/hde/hde64.c
  ${minhook_SOURCE_DIR}/src/hde/hde32.c
)

add_library(HytaleUIScaleHook SHARED main.cpp ${MINHOOK_SOURCES})

target_include_directories(HytaleUIScaleHook PRIVATE 
  ${minhook_SOURCE_DIR}/include
  ${minhook_SOURCE_DIR}/src
  ${minhook_SOURCE_DIR}/src/hde
)

target_compile_definitions(HytaleUIScaleHook PRIVATE UNICODE _UNICODE)

target_link_libraries(HytaleUIScaleHook PRIVATE opengl32)
