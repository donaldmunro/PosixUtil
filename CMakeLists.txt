cmake_minimum_required(VERSION 3.8.0)
project(CppExec)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-Wno-unused-function)

set(SOURCES Process.cc Process.hh)
set(INCLUDES "${PROJECT_SOURCE_DIR}")

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package( Threads REQUIRED )

add_executable(tester tester.cc ${SOURCES})
add_executable( unittests test.cc NamedSemaphore.cc NamedSemaphore.hh TmpFile.hh TmpFile.cc ${SOURCES} )
add_dependencies(unittests tester)
target_link_libraries(unittests Threads::Threads)

target_include_directories(unittests PUBLIC ${INCLUDES})
