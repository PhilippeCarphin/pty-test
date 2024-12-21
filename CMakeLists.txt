cmake_minimum_required(VERSION 3.20)

project(pty C)

add_library(tlpi-pt STATIC tlpi-pt.c)
add_library(phil-expect STATIC phil-expect.c)

target_link_libraries(phil-expect PRIVATE tlpi-pt)

add_executable(expect-demo expect-demo.c)
target_link_libraries(expect-demo PRIVATE phil-expect)

add_executable(pty-demo pty-demo.c)
target_link_libraries(pty-demo PRIVATE phil-expect)

include(CTest)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --verbose)
add_dependencies(check pty-demo expect-demo)

add_test(NAME expect COMMAND $<TARGET_FILE:expect-demo>)

