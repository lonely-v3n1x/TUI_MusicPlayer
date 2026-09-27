cmake_minimum_required (VERSION 3.22)

project(TUI_MusicPlayer
  LANGUAGES CXX
  VERSION 1.0.0
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
if(ipo_ok)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections $<$<NOT:$<CONFIG:Debug>>:-s>)
include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/arthursonzogni/ftxui.git
  GIT_TAG        v5.0.0
  GIT_PROGRESS   TRUE
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_Declare(stb
  URL https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
  DOWNLOAD_NO_EXTRACT TRUE
)
# set(FXUI_ENABLE_DEV_WARNINGS ON)
# use "cmake -DFXUI_ENABLE_DEV_WARNINGS=ON .. "
FetchContent_MakeAvailable(ftxui stb)
# ------------------------------------------------------------------------------
add_library(cavacore STATIC IMPORTED)
set_target_properties(cavacore PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/include/libcavacore.a")
set_target_properties(cavacore PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_executable(test-starter src/main.cpp)
target_include_directories(test-starter PRIVATE ${stb_SOURCE_DIR})
# target_link_libraries(test-starter libcavacore)
# target_include_directories(test-starter PRIVATE include/)
# include_directories(include)

target_link_libraries(test-starter
  PRIVATE ftxui::screen
  PRIVATE ftxui::dom
  PRIVATE ftxui::component
  # PRIVATE -lj
  PRIVATE -lm
  PRIVATE -ldl
  PRIVATE -lpthread
  PRIVATE -lfftw3
  PRIVATE cavacore
)

# if (EMSCRIPTEN)
#   string(APPEND CMAKE_CXX_FLAGS " -s USE_PTHREADS")
#   string(APPEND CMAKE_EXE_LINKER_FLAGS " -s ASYNCIFY")
#   string(APPEND CMAKE_EXE_LINKER_FLAGS " -s PROXY_TO_PTHREAD")
#
#   foreach(file "index.html" "run_webassembly.py")
#     configure_file("src/${file}" ${file})
#   endforeach(file)
# endif()
#

