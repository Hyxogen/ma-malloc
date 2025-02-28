project(sysdeps LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_C_EXTENSIONS TRUE)

if (UNIX)
add_library(sysdeps
	linux.c
)
endif()

target_include_directories(sysdeps PUBLIC ../../include)
