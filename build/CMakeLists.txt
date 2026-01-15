cmake_minimum_required(VERSION 3.16)

project(SO_Projekt_Jaskinia VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
endif()

include_directories(include)

add_executable(SymulacjaJaskini src/main.cpp)
add_executable(Przewodnik src/guide.cpp)
add_executable(Kasjer src/cashier.cpp)
add_executable(Zwiedzajacy src/visitor.cpp)
add_executable(Straznik src/guard.cpp)