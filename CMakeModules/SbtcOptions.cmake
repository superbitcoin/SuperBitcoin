macro(configure_project)

	ADD_DEFINITIONS(-DENABLE_WALLET)
	ADD_DEFINITIONS(-DHAVE_CONFIG_H)
	ADD_DEFINITIONS(-DHAVE_SYS_SELECT_H)
	ADD_DEFINITIONS(-DTESTS)
	ADD_DEFINITIONS(-DENABLE_ZMQ_FLAG)
	ADD_DEFINITIONS(-DENABLE_STATIC_FLAG)




	option(ENABLE_WALLET  "ENABLE WALLET FLAG" ON)
    option(HAVE_CONFIG_H "Build with tests" ON)
    option(HAVE_SYS_SELECT_H "Build with tests" ON)
    option(TESTS "Build with tests" OFF)
    option(ENABLE_ZMQ_FLAG "Build with tests" OFF)
	option(ENABLE_STATIC_FLAG "enable static falg" ON)

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


#	set( Boost_INCLUDE_DIR /usr/include)
#	set( OPENSSL_INCLUDE_DIR /usr/local/include)
#	set( MINIUPNPC_INCLUDE_DIR /usr/include)
#	set( Secp256k1_INCLUDE_DIR  /usr/local/include)
#	set( LOG4CPP_INCLUDE_DIR /usr/local/include)
#	set( LEVELDB_INCLUDE_DIR /usr//include)



    print_config()

endmacro()

macro(print_config)
	message("")
	message("------------------------------------------------------------------------")
	message("-- Configuring ${PROJECT_NAME}")
	message("------------------------------------------------------------------------")
	message("-- CMake ${CMAKE_VERSION} (${CMAKE_COMMAND})")
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

	message("-- OPENSSL_INCLUDE_DIR  path:                      ${OPENSSL_INCLUDE_DIR}")
	message("-- _OPENSSL_LIBDIR  path:                      ${_OPENSSL_LIBDIR}")

	message("-- Boost_INCLUDE_DIR  path:                      ${Boost_INCLUDE_DIR}")
	message("-- Boost_LIBRARY_DIR  path:                      ${Boost_LIBRARY_DIR}")

	message("-- MINIUPNPC_INCLUDE_DIR  path:                      ${MINIUPNPC_INCLUDE_DIR}")

	message("-- Secp256k1_INCLUDE_DIR  path:                      ${Secp256k1_INCLUDE_DIR}")

	message("-- LOG4CPP_INCLUDE_DIR  path:                      ${LOG4CPP_INCLUDE_DIR}")

	message("-- LEVELDB_INCLUDE_DIR  path:                      ${LEVELDB_INCLUDE_DIR}")


	message("------------------------------------------------------------------------")
	message("")
endmacro()
