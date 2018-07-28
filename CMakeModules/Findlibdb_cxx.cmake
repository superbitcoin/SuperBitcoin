



MESSAGE(STATUS "Using bundled Findlibdb.cmake...")

find_path(
		LIBDB_CXX_INCLUDE_DIR
		NAMES libdb_cxx
		DOC "libdb include dir"

)


IF (ENABLE_STATIC_FLAG)
	set(LIB_FILE libdb_cxx.a)
ELSE ()
	set(LIB_FILE db_cxx)
ENDIF ()
find_library(
		LIBDB_CXX_LIBRARIES
		NAMES ${LIB_FILE}
		DOC "libdb library"
)

set(LIBDB_INCLUDE_DIRS ${LIBDB_CXX_INCLUDE_DIR})
set(LIBDB_LIBRARIES ${LIBDB_CXX_LIBRARIES})



include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(leveldb DEFAULT_MSG
        LIBDB_LIBRARIES LIBDB_INCLUDE_DIRS)
mark_as_advanced (LIBDB_INCLUDE_DIRS LIBDB_LIBRARIES)
