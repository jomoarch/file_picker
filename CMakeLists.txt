cmake_minimum_required(VERSION 3.16)
project(TesTTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  # /utf-8：源码含 UTF-8 字面量（状态栏箭头等），MSVC 默认按系统 ANSI
  # 代码页解码源码，会导致字符串损坏；/utf-8 使源与执行字符集均为 UTF-8
  add_compile_options(/W3 /utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---- 核心库 ----
add_library(file_picker STATIC
  src/helpfile.cpp
  src/listing.cpp
  src/picker.cpp
  src/renderer.cpp
  src/text.cpp
  src/wcwidth.cpp
  src/file_picker.cpp
)
if(WIN32)
  target_sources(file_picker PRIVATE src/terminal_windows.cpp)
else()
  target_sources(file_picker PRIVATE src/terminal_posix.cpp)
endif()

target_include_directories(file_picker PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(WIN32)
  target_link_libraries(file_picker PRIVATE kernel32 user32)
endif()

# ---- 演示程序 ----
add_executable(file_picker_demo test/src/file_picker_demo.cpp)
target_link_libraries(file_picker_demo PRIVATE file_picker)
# 把帮助文件复制到可执行文件旁，使默认查找（exe 所在目录）能命中
add_custom_command(TARGET file_picker_demo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/file_picker_help.txt
          ${CMAKE_BINARY_DIR}/file_picker_help.txt)

enable_testing()
add_test(NAME file_picker_unit COMMAND test_file_picker)
