macro(configure_project)

	ADD_DEFINITIONS(-DENABLE_WALLET)
	ADD_DEFINITIONS(-DHAVE_CONFIG_H)
	ADD_DEFINITIONS(-DHAVE_SYS_SELECT_H)
	ADD_DEFINITIONS(-DTESTS)
	ADD_DEFINITIONS(-DENABLE_ZMQ_FLAG)
	ADD_DEFINITIONS(-DENABLE_STATIC_FLAG)
	ADD_DEFINITIONS(-DREVISIVE_FLAG)



	option(ENABLE_WALLET  "ENABLE WALLET FLAG" ON)
    option(HAVE_CONFIG_H "Build with tests" ON)
    option(HAVE_SYS_SELECT_H "Build with tests" ON)
    option(TESTS "Build with tests" OFF)
    option(ENABLE_ZMQ_FLAG "Build with tests" OFF)
	option(ENABLE_STATIC_FLAG "enable static falg" ON)
	option(REVISIVE_FLAG " enable REVISIVE falg" ON)

	if (ENABLE_WALLET)
		SET( ENABLE_WALLET 1 )
	else(ENABLE_WALLET)
		SET( ENABLE_WALLET 0 )
	endif()


    if (HAVE_CONFIG_H)
        SET( HAVE_CONFIG_H 1 )
    else(HAVE_CONFIG_H)
        SET( HAVE_CONFIG_H 0 )
    endif()

    if (HAVE_SYS_SELECT_H)
        SET( HAVE_SYS_SELECT_H 1 )
    else(HAVE_SYS_SELECT_H)
        SET( HAVE_SYS_SELECT_H 0 )
    endif()

    if (ENABLE_ZMQ_FLAG)
        SET( ENABLE_ZMQ 1 )
    else(ENABLE_ZMQ_FLAG)
        SET( ENABLE_ZMQ 0 )
    endif()


	if (REVISIVE_FLAG)
		SET(CLIENT_VERSION_IS_RELEASE true)
	else ()
		SET(CLIENT_VERSION_IS_RELEASE false)
	endif ()


	SET(HAVE_BUILD_INFO 1)
	SET(GIT_ARCHIVE 1)
	SET(CLIENT_VERSION_BUILD 5)
	SET(CLIENT_VERSION_MAJOR 0)
	SET(CLIENT_VERSION_MINOR 17)
	SET(CLIENT_VERSION_REVISION 0)


    print_config()

endmacro()

macro(print_config)
	message("")
	message("------------------------------------------------------------------------")
	message("-- Configuring ${PROJECT_NAME}")
	message("------------------------------------------------------------------------")
	message("-- CMake ${CMAKE_VERSION} (path:${CMAKE_COMMAND})")
	message("-- CMAKE_BUILD_TYPE Build type                               ${CMAKE_BUILD_TYPE}")
	message("-- TARGET_PLATFORM  Target platform                          ${CMAKE_SYSTEM_NAME}")
	message("-- BUILD_SHARED_LIBS                                         ${BUILD_SHARED_LIBS}")
	message("--------------------------------------------------------------- features")
	message("-- ENABLE_WALLET       enable or disable flag                ${ENABLE_WALLET}")
	message("-- HAVE_CONFIG_H       Have config                           ${HAVE_CONFIG_H}")
	message("-- HAVE_SYS_SELECT_H   Have sys function select              ${HAVE_SYS_SELECT_H}")
    message("-- TESTS               Build tests                           ${TESTS}")
    message("-- ENABLE_ZMQ          enable ZMQ flag                       ${ENABLE_ZMQ}")
	message("-- ENABLE_STATIC_FLAG  enable static falg                    ${ENABLE_STATIC_FLAG}")
	message("-- EREVISIVE_FLAG  	enable revisive falg                    ${REVISIVE_FLAG}")




#	message("-- OPENSSL_INCLUDE_DIR  path:                      ${OPENSSL_INCLUDE_DIR}")
#	message("-- _OPENSSL_LIBDIR  path:                      ${_OPENSSL_LIBDIR}")
#	message("-- Boost_INCLUDE_DIR  path:                      ${Boost_INCLUDE_DIR}")
#	message("-- Boost_LIBRARY_DIR  path:                      ${Boost_LIBRARY_DIR}")
#	message("-- MINIUPNPC_INCLUDE_DIR  path:                      ${MINIUPNPC_INCLUDE_DIR}")
#	message("-- Secp256k1_INCLUDE_DIR  path:                      ${Secp256k1_INCLUDE_DIR}")
#	message("-- LOG4CPP_INCLUDE_DIR  path:                      ${LOG4CPP_INCLUDE_DIR}")
#	message("-- LEVELDB_INCLUDE_DIR  path:                      ${LEVELDB_INCLUDE_DIR}")


	message("------------------------------------------------------------------------")
	message("")
endmacro()
