cmake_minimum_required(VERSION 3.16)
project(SmartParkingBackend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Locate MySQL C Connector / MySQL Server 8.0
set(MYSQL_ROOT "C:/Program Files/MySQL/MySQL Server 8.0" CACHE PATH "Path to MySQL Server or Connector C")
find_path(MYSQL_INCLUDE_DIR mysql.h
    HINTS
        "${MYSQL_ROOT}/include"
        "C:/Program Files/MySQL/MySQL Connector C 6.1/include"
        "/usr/include/mysql"
)
find_file(MYSQL_DLL libmysql.dll
    HINTS
        "${MYSQL_ROOT}/lib"
        "${MYSQL_ROOT}/bin"
)
find_library(MYSQL_LIBRARY NAMES libmysql mysqlclient
    HINTS
        "${MYSQL_ROOT}/lib"
        "/usr/lib/x86_64-linux-gnu"
)

if(NOT MYSQL_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find mysql.h. Set -DMYSQL_ROOT=... to your MySQL installation.")
endif()

add_executable(smart_parking_server src/main.cpp)
target_include_directories(smart_parking_server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/crow"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio"
    "${MYSQL_INCLUDE_DIR}"
)
target_compile_definitions(smart_parking_server PRIVATE ASIO_STANDALONE)

if(WIN32)
    target_compile_definitions(smart_parking_server PRIVATE _WIN32_WINNT=0x0A00)
    if(MINGW AND MYSQL_DLL)
        target_link_options(smart_parking_server PRIVATE -static-libgcc -static-libstdc++)
        target_link_libraries(smart_parking_server PRIVATE "${MYSQL_DLL}" -Wl,-Bstatic -lpthread -Wl,-Bdynamic ws2_32 mswsock)
    else()
        target_link_libraries(smart_parking_server PRIVATE "${MYSQL_LIBRARY}" ws2_32 mswsock)
    endif()
    if(MYSQL_DLL)
        add_custom_command(TARGET smart_parking_server POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MYSQL_DLL}"
                "$<TARGET_FILE_DIR:smart_parking_server>/libmysql.dll"
        )
    endif()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(smart_parking_server PRIVATE "${MYSQL_LIBRARY}" Threads::Threads)
endif()

# Unit tests for DSA & Auth (no live DB required)
enable_testing()
add_executable(test_dsa tests/test_dsa.cpp)
target_include_directories(test_dsa PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_test(NAME DsaAndAuthTests COMMAND test_dsa)

