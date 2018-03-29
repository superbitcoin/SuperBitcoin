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
	EVENT_INCLUDE_DIR
	NAMES event2/event.h
	DOC "event include dir"
)

IF (ENABLE_STATIC_FLAG)
	set(LIB_FILE libevent.a)
ELSE ()
	set(LIB_FILE event)
ENDIF ()

find_library(
	EVENT_LIBRARY
	NAMES ${LIB_FILE}
	DOC "event library"
)


set(EVENT_INCLUDE_DIRS ${EVENT_INCLUDE_DIR})
set(EVENT_LIBRARIES ${EVENT_LIBRARY})

# debug library on windows
# same naming convention as in QT (appending debug library with d)
# boost is using the same "hack" as us with "optimized" and "debug"
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")

	find_library(
		EVENT_LIBRARY_DEBUG
		NAMES eventd
		DOC "event debug library"
	)

	set(EVENT_LIBRARIES "iphlpapi" optimized ${EVENT_LIBRARIES} debug ${EVENT_LIBRARY_DEBUG})

endif()

# handle the QUIETLY and REQUIRED arguments and set EVENT_FOUND to TRUE
# if all listed variables are TRUE, hide their existence from configuration view
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(event DEFAULT_MSG
	EVENT_LIBRARY EVENT_INCLUDE_DIR)
mark_as_advanced (EVENT_INCLUDE_DIR EVENT_LIBRARY)

