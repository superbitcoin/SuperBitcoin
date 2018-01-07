cmake_minimum_required(VERSION 3.9)
project(SuperBitcoin)



list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
list(APPEND CMAKE_MODULE_PATH "/home/root1/tool/clion-2017.2.3/bin/cmake/share/cmake-3.9/Modules")
list(APPEND CMAKE_MODULE_PATH "/usr/include")

INCLUDE( VersionMacros )
INCLUDE( SetupTargetMacros )


set( CMAKE_CXX_STANDARD 14 )
set(VERSION_MAJOR 2)
set(VERSION_MINOR 0)
set(VERSION_PATCH 1)

set(CLIENT_VERSION_BUILD 2)
set(CLIENT_VERSION_IS_RELEASE true)
set(CLIENT_VERSION_MAJOR 0)
set(CLIENT_VERSION_MINOR 16)
set(CLIENT_VERSION_REVISION 0)




set( CLI_CLIENT_EXECUTABLE_NAME sbtc-cli )
set( CLI_SEVER_EXECUTABLE_NAME sbtcd )
# http://stackoverflow.com/a/18369825
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC version must be at least 4.8!")
    endif()
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
        message(FATAL_ERROR "Clang version must be at least 3.3!")
    endif()
endif()


#set (ENABLE_INSTALLER 1)
#set (USE_PCH 1)

if (USE_PCH)
    include (cotire)
endif(USE_PCH)


set(BOOST_COMPONENTS)
LIST(APPEND BOOST_COMPONENTS thread
        date_time
        system
        filesystem
        program_options
        signals
        serialization
        chrono
        unit_test_framework
        context
        locale)
set( Boost_USE_STATIC_LIBS ON CACHE STRING "ON or OFF" )


IF( WIN32 )
    set(BOOST_ROOT $ENV{BOOST_ROOT})
    set(Boost_USE_MULTITHREADED ON)
    set(BOOST_ALL_DYN_LINK OFF) # force dynamic linking for all libraries
ENDIF(WIN32)

find_package(Boost 1.64 REQUIRED COMPONENTS ${BOOST_COMPONENTS})
# For Boost 1.53 on windows, coroutine was not in BOOST_LIBRARYDIR and do not need it to build,  but if boost versin >= 1.54, find coroutine otherwise will cause link errors
IF(NOT "${Boost_VERSION}" MATCHES "1.53(.*)")
    SET(BOOST_LIBRARIES_TEMP ${Boost_LIBRARIES})
    find_package(Boost 1.54 REQUIRED COMPONENTS coroutine)
    LIST(APPEND BOOST_COMPONENTS coroutine)
    SET(Boost_LIBRARIES ${BOOST_LIBRARIES_TEMP} ${Boost_LIBRARIES})
ENDIF()
if(Boost_FOUND)
    SET(HAVE_BOOST 1)
endif()

if(Boost_CHRONO_FOUND)
    SET(HAVE_BOOST_CHRONO 1)
    SET(HAVE_WORKING_BOOST_SLEEP_FOR 1)
endif()

if(Boost_FILESYSTEM_FOUND)
    SET(HAVE_BOOST_FILESYSTEM 1)
endif()

if(Boost_PROGRAM_OPTIONS_FOUND)
    SET(HAVE_BOOST_PROGRAM_OPTIONS 1)
endif()

if(Boost_SYSTEM_FOUND)
    SET(HAVE_BOOST_SYSTEM 1)
endif()

if(Boost_THREAD_FOUND)
    SET(HAVE_BOOST_THREAD 1)
endif()



find_package(libdb_cxx REQUIRED)

IF (LIBDB_CXX_INCLUDE_DIR AND LIBDB_CXX_LIBRARIES)
    message(STATUS "Found libdb libraries")
    include_directories(${LIBDB_CXX_INCLUDE_DIR})
    message(${LIBDB_CXX_LIBRARIES})
#    target_link_libraries(main ${LIBDB_CXX_LIBRARIES})
ENDIF (LIBDB_CXX_INCLUDE_DIR AND LIBDB_CXX_LIBRARIES)

if( WIN32 )

    message( STATUS "Configuring Eos on WIN32")
    set( DB_VERSION 60 )
    set( BDB_STATIC_LIBS 1 )

    set( ZLIB_LIBRARIES "" )
    SET( DEFAULT_EXECUTABLE_INSTALL_DIR bin/ )

    set(CRYPTO_LIB)

    #looks like this flag can have different default on some machines.
    SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")

    # Probably cmake has a bug and vcxproj generated for executable in Debug conf. has disabled debug info
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")

    # On windows tcl should be installed to the directory pointed by setenv.bat script
    SET(TCL_INCLUDE_PATH $ENV{TCL_ROOT}/include)
    MESSAGE(STATUS "tcl INCLUDE PATH: ${TCL_INCLUDE_PATH}")

    FIND_PACKAGE(TCL)
    MESSAGE(STATUS "tcl_library: ${TCL_LIBRARY}")

    SET(TCL_LIBS "optimized;${TCL_LIBRARY};debug;")
    get_filename_component(TCL_LIB_PATH "${TCL_LIBRARY}" PATH)
    get_filename_component(TCL_LIB_NAME "${TCL_LIBRARY}" NAME_WE)
    get_filename_component(TCL_LIB_EXT "${TCL_LIBRARY}" EXT)

    SET(TCL_LIBS "${TCL_LIBS}${TCL_LIB_PATH}/${TCL_LIB_NAME}g${TCL_LIB_EXT}")
    SET(TCL_LIBRARY ${TCL_LIBS})

else( WIN32 ) # Apple AND Linux

    find_library(READLINE_LIBRARIES NAMES readline)
    find_path(READLINE_INCLUDE_DIR readline/readline.h)
    #if(NOT READLINE_INCLUDE_DIR OR NOT READLINE_LIBRARIES)
    #  MESSAGE(FATAL_ERROR "Could not find lib readline.")
    #endif()

    if( APPLE )
        # Apple Specific Options Here
        message( STATUS "Configuring  on OS X" )
        #set( CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++14 -Wall -Wno-invalid-partial-specialization -Wno-deprecated-declarations" )
        set( CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++14 -Wall -Wno-deprecated-declarations" )
    else( APPLE )
        # Linux Specific Options Here
        message( STATUS "Configuring  on Linux" )
        set( CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall" )
        set( rt_library rt )
        set( pthread_library pthread)
        if ( NOT DEFINED crypto_library )
            # I'm not sure why this is here, I guess someone has openssl and can't detect it with find_package()?
            # if you have a normal install, you can define crypto_library to the empty string to avoid a build error
            set( crypto_library crypto)
        endif ()
        if ( FULL_STATIC_BUILD )
            set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
        endif ( FULL_STATIC_BUILD )

        if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )
            if( CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL 4.0.0 OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 4.0.0 )
                set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-partial-specialization" )
            endif()
        endif()
    endif( APPLE )

    if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-builtin-memcmp" )
    endif()

    if( "${CMAKE_GENERATOR}" STREQUAL "Ninja" )
        if( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" )
            set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics" )
        endif()
    endif()

    # based on http://www.delorie.com/gnu/docs/gdb/gdb_70.html
    # uncomment this line to tell GDB about macros (slows compile times)
    # set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -gdwarf-2 -g3" )

endif( WIN32 )

find_package(LevelDB REQUIRED)
#message(FATAL_ERROR "${LevelDB_INCLUDE_DIR}")
message( STATUS ${LevelDB_INCLUDE_DIR})

ADD_DEFINITIONS(-DHAVE_CONFIG_H)
ADD_DEFINITIONS(-DHAVE_SYS_SELECT_H)

include_directories(/usr/local/include/boost)
include_directories(/usr/include)

include_directories(src)
#include_directories(src/secp256k1)
#include_directories(src/secp256k1/src)
include_directories(src/univalue/include)
include_directories(src/rpc)
include_directories(src/config)
include_directories(src/primitivesls)

include_directories ("${PROJECT_SOURCE_DIR}/src/secp256k1/contrib" )
include_directories ("${PROJECT_SOURCE_DIR}/src/secp256k1/include" )

include_directories ("${PROJECT_SOURCE_DIR}/src" )
#include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb" )
#include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/include" )
#include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/include/leveldb" )
#include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/port" )
#include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/table" )
#include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/util" )

#找文件夹
include_directories (${CMAKE_ROOT}/Modules)
INCLUDE(CheckIncludeFiles)
#CHECK_INCLUDE_FILES(malloc.h HAVE_MALLOC_H)
CHECK_INCLUDE_FILES(byteswap.h HAVE_BYTESWAP_H)
CHECK_INCLUDE_FILES(endian.h HAVE_ENDIAN_H)
CHECK_INCLUDE_FILES(dlfcn.h HAVE_DLFCN_H)
CHECK_INCLUDE_FILES(inttypes.h HAVE_INTTYPES_H)
CHECK_INCLUDE_FILES(memory.h HAVE_MEMORY_H)
CHECK_INCLUDE_FILES(miniupnpc/miniupnpc.h HAVE_MINIUPNPC_MINIUPNPC_H)
CHECK_INCLUDE_FILES(miniupnpc/miniwget.h HAVE_MINIUPNPC_MINIWGET_H)
CHECK_INCLUDE_FILES(miniupnpc/upnpcommands.h HAVE_MINIUPNPC_UPNPCOMMANDS_H)
CHECK_INCLUDE_FILES(miniupnpc/upnperrors.h HAVE_MINIUPNPC_UPNPERRORS_H)
CHECK_INCLUDE_FILES(thread.h HAVE_PTHREAD)
CHECK_INCLUDE_FILES(pthread.h HAVE_PTHREAD_PRIO_INHERIT)
CHECK_INCLUDE_FILES(stdint.h HAVE_STDINT_H)
CHECK_INCLUDE_FILES(stdio.h HAVE_STDIO_H)
CHECK_INCLUDE_FILES(stdlib.h HAVE_STDLIB_H)
CHECK_INCLUDE_FILES(strings.h HAVE_STRINGS_H)
CHECK_INCLUDE_FILES(string.h HAVE_STRING_H)
CHECK_INCLUDE_FILES(sys/prctl.h HAVE_SYS_PRCTL_H)
CHECK_INCLUDE_FILES(sys/select.h HAVE_SYS_SELECT_H)
CHECK_INCLUDE_FILES(sys/stat.h HAVE_SYS_STAT_H)
CHECK_INCLUDE_FILES(sys/types.h HAVE_SYS_TYPES_H)
CHECK_INCLUDE_FILES(unistd.h HAVE_UNISTD_H)



INCLUDE(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" HAVE_CXX11)


INCLUDE(CheckFunctionExists)
CHECK_FUNCTION_EXISTS(daemon HAVE_DECL_DAEMON)
CHECK_FUNCTION_EXISTS(strerror_r HAVE_DECL_STRERROR_R)
CHECK_FUNCTION_EXISTS(strnlen HAVE_DECL_STRNLEN)
CHECK_FUNCTION_EXISTS(getentropy HAVE_GETENTROPY)
CHECK_FUNCTION_EXISTS(malloc_info HAVE_MALLOC_INFO)
CHECK_FUNCTION_EXISTS(mallopt  HAVE_MALLOPT_ARENA_MAX)
CHECK_FUNCTION_EXISTS(strerror_r HAVE_STRERROR_R)
CHECK_FUNCTION_EXISTS(getrandom HAVE_SYS_GETRANDOM)








INCLUDE(CheckSymbolExists)
CHECK_SYMBOL_EXISTS(bswap_16 "byteswap.h" HAVE_DECL_BSWAP_16)
CHECK_SYMBOL_EXISTS(bswap_32 "byteswap.h" HAVE_DECL_BSWAP_32)
CHECK_SYMBOL_EXISTS(bswap_64 "byteswap.h" HAVE_DECL_BSWAP_64)

CHECK_SYMBOL_EXISTS(be16toh "endian.h" HAVE_DECL_BE16TOH)
CHECK_SYMBOL_EXISTS(be32toh "endian.h" HAVE_DECL_BE32TOH)
CHECK_SYMBOL_EXISTS(be64toh "endian.h" HAVE_DECL_BE64TOH)

CHECK_SYMBOL_EXISTS(htobe16 "endian.h" HAVE_DECL_HTOBE16)
CHECK_SYMBOL_EXISTS(htobe32 "endian.h" HAVE_DECL_HTOBE32)
CHECK_SYMBOL_EXISTS(htobe64 "endian.h" HAVE_DECL_HTOBE64)

CHECK_SYMBOL_EXISTS(htole16 "endian.h" HAVE_DECL_HTOLE16)
CHECK_SYMBOL_EXISTS(htole32 "endian.h" HAVE_DECL_HTOLE32)
CHECK_SYMBOL_EXISTS(htole64 "endian.h" HAVE_DECL_HTOLE64)

CHECK_SYMBOL_EXISTS(le16toh "endian.h" HAVE_DECL_LE16TOH)
CHECK_SYMBOL_EXISTS(le32toh "endian.h" HAVE_DECL_LE32TOH)
CHECK_SYMBOL_EXISTS(le64toh "endian.h" HAVE_DECL_LE64TOH)

CHECK_SYMBOL_EXISTS(MSG_DONTWAIT "x86_64-linux-gnu/bits/socket.h" HAVE_MSG_DONTWAIT)
CHECK_SYMBOL_EXISTS(MSG_NOSIGNAL "x86_64-linux-gnu/bits/socket.h" HAVE_MSG_NOSIGNAL)

set(HAVE_MSG_DONTWAIT 1)
set(HAVE_MSG_NOSIGNAL 1)


# 加入一个配置头文件，用于处理 CMake 对源码的设置
configure_file (
        "${PROJECT_SOURCE_DIR}/src/config/sbtc-config.h.in"
        "${PROJECT_SOURCE_DIR}/src/config/sbtc-config.h"
)




add_subdirectory( src )



