cmake_minimum_required(VERSION 3.20)

project(ma LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_C_EXTENSIONS FALSE)

set(CMAKE_BUILD_TYPE Release)

option(BUILD_SHARED_LIBS "Build to a shared library" ON)

option(SUBJECT_COMPATIBLE "Build the project so that it complies to the subject" OFF)

add_library(ma
	src/aligned_alloc.c
	src/arena.c
	src/calloc.c
	src/chunk.c
	src/common.c
	src/debug.c
	src/extensions.c
	src/free.c
	src/libc.c
	src/malloc.c
	src/opts.c
	src/realloc.c
	src/state.c
)

target_include_directories(ma PUBLIC include)
target_include_directories(ma INTERFACE include)

add_subdirectory(src/sysdeps)

target_link_libraries(ma sysdeps)

if(SUBJECT_COMPATIBLE)
	set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
	set(THREADS_PREFER_PTHREAD_FLAG TRUE)

	find_package(Threads REQUIRED)
	target_link_libraries(ma Threads::Threads)

	add_compile_definitions(MA_SEGREGATED_BESTFIT=1 MA_TRACK_CHUNKS=1
		MA_COMPILE_AS_LIBC=1 MA_USE_LIBFT=0
		MA_USE_THREADS=MA_PTHREAD_THREADS _POSIX_C_SOURCE=199506L
	)
endif()
