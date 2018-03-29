# Find event
#
# Find the event includes and library
#
# if you nee to add a custom library search path, do it via CMAKE_PREFIX_PATH
#
# This module defines
#  EVENT_INCLUDE_DIRS, where to find header, etc.
#  EVENT_LIBRARIES, the libraries needed to use event.
#  EVENT_FOUND, If false, do not try to use event.

# only look in default directories
find_path(
	LOG4CPP_INCLUDE_DIR
	NAMES log4cpp
	DOC "log4cpp include dir"
)

IF (ENABLE_STATIC_FLAG)
	set(LIB_FILE liblog4cpp.a)
ELSE ()
	set(LIB_FILE log4cpp)
ENDIF ()

find_library(
	LOG4CPP_LIBRARY
	NAMES ${LIB_FILE}
	DOC "log4cpp library"
)





set(LOG4CPP_INCLUDE_DIRS ${LOG4CPP_INCLUDE_DIR})
set(LOG4CPP_LIBRARYS ${LOG4CPP_LIBRARY})

# debug library on windows
# same naming convention as in QT (appending debug library with d)
# boost is using the same "hack" as us with "optimized" and "debug"


# handle the QUIETLY and REQUIRED arguments and set EVENT_FOUND to TRUE
# if all listed variables are TRUE, hide their existence from configuration view
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(log4cpp DEFAULT_MSG
		LOG4CPP_LIBRARY LOG4CPP_INCLUDE_DIR)
mark_as_advanced (LOG4CPP_INCLUDE_DIR LOG4CPP_LIBRARY)

