set(CMAKE_LEGACY_CYGWIN_WIN32 0) # Remove when CMake >= 2.8.4 is required
cmake_minimum_required (VERSION 2.8)
project (signalslib)
enable_language (CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package (Threads)
message (STATUS "Threads: ${CMAKE_THREAD_LIBS_INIT}")

list (APPEND includes_dir
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/versions-lib/include
  )
include_directories (${includes_dir})
list (APPEND cxx_features cxx_auto_type cxx_decltype cxx_defaulted_functions
  cxx_deleted_functions cxx_nullptr cxx_nonstatic_member_init
  cxx_rvalue_references)
list (APPEND cxx_options "-std=c++11" "-Wall" "-Wextra")

add_library (signalslib src/signals.cxx)
target_compile_options (signalslib PUBLIC ${cxx_options})
target_link_libraries (signalslib ${CMAKE_THREAD_LIBS_INIT})

add_executable (test0 test.cxx)
target_link_libraries (test0 signalslib)
target_compile_options(test0 PUBLIC ${cxx_options})

add_test (TestTest test0)
