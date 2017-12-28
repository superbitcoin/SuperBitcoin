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
include_directories(src/secp256k1)
include_directories(src/secp256k1/src)
include_directories(src/univalue/include)
include_directories(src/rpc)
include_directories(src/config)
include_directories(src/primitivesls)

include_directories ("${PROJECT_SOURCE_DIR}/src/secp256k1/contrib" )
include_directories ("${PROJECT_SOURCE_DIR}/src/secp256k1/include" )

include_directories ("${PROJECT_SOURCE_DIR}/src" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/include" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/include/leveldb" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/port" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/table" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/util" )






#include_directories(src/leveldb/include)
#include_directories(src/compat)
#include_directories(src/secp256k1/include)
#include_directories(src/univalue/include)

add_subdirectory( src )




#

#ADD_DEFINITIONS(-DCHEAT_IDE)
#
#
#add_executable(SuperBitcoin
#        src/bench/base58.cpp
#        src/bench/bench.cpp
#        src/bench/bench.h
#        src/bench/bench_bitcoin.cpp
#        src/bench/ccoins_caching.cpp
#        src/bench/checkblock.cpp
#        src/bench/checkqueue.cpp
#        src/bench/coin_selection.cpp
#        src/bench/crypto_hash.cpp
#        src/bench/Examples.cpp
#        src/bench/lockedpool.cpp
#        src/bench/mempool_eviction.cpp
#        src/bench/perf.cpp
#        src/bench/perf.h
#        src/bench/prevector_destructor.cpp
#        src/bench/rollingbloom.cpp
#        src/bench/verify_script.cpp
#        src/compat/byteswap.h
#        src/compat/endian.h
#        src/compat/glibc_compat.cpp
#        src/compat/glibc_sanity.cpp
#        src/compat/glibcxx_sanity.cpp
#        src/compat/sanity.h
#        src/compat/strnlen.cpp
#        src/consensus/consensus.h
#        src/consensus/merkle.cpp
#        src/consensus/merkle.h
#        src/consensus/params.h
#        src/consensus/tx_verify.cpp
#        src/consensus/tx_verify.h
#        src/consensus/validation.h
#        src/crypto/ctaes/bench.c
#        src/crypto/ctaes/ctaes.c
#        src/crypto/ctaes/ctaes.h
#        src/crypto/ctaes/test.c
#        src/crypto/aes.cpp
#        src/crypto/aes.h
#        src/crypto/chacha20.cpp
#        src/crypto/chacha20.h
#        src/crypto/common.h
#        src/crypto/hmac_sha256.cpp
#        src/crypto/hmac_sha256.h
#        src/crypto/hmac_sha512.cpp
#        src/crypto/hmac_sha512.h
#        src/crypto/ripemd160.cpp
#        src/crypto/ripemd160.h
#        src/crypto/sha1.cpp
#        src/crypto/sha1.h
#        src/crypto/sha256.cpp
#        src/crypto/sha256.h
#        src/crypto/sha256_sse4.cpp
#        src/crypto/sha512.cpp
#        src/crypto/sha512.h
#        src/leveldb/db/autocompact_test.cc
#        src/leveldb/db/builder.cc
#        src/leveldb/db/builder.h
#        src/leveldb/db/c.cc
#        src/leveldb/db/c_test.c
#        src/leveldb/db/corruption_test.cc
#        src/leveldb/db/db_bench.cc
#        src/leveldb/db/db_impl.cc
#        src/leveldb/db/db_impl.h
#        src/leveldb/db/db_iter.cc
#        src/leveldb/db/db_iter.h
#        src/leveldb/db/db_test.cc
#        src/leveldb/db/dbformat.cc
#        src/leveldb/db/dbformat.h
#        src/leveldb/db/dbformat_test.cc
#        src/leveldb/db/dumpfile.cc
#        src/leveldb/db/fault_injection_test.cc
#        src/leveldb/db/filename.cc
#        src/leveldb/db/filename.h
#        src/leveldb/db/filename_test.cc
#        src/leveldb/db/leveldbutil.cc
#        src/leveldb/db/log_format.h
#        src/leveldb/db/log_reader.cc
#        src/leveldb/db/log_reader.h
#        src/leveldb/db/log_test.cc
#        src/leveldb/db/log_writer.cc
#        src/leveldb/db/log_writer.h
#        src/leveldb/db/memtable.cc
#        src/leveldb/db/memtable.h
#        src/leveldb/db/recovery_test.cc
#        src/leveldb/db/repair.cc
#        src/leveldb/db/skiplist.h
#        src/leveldb/db/skiplist_test.cc
#        src/leveldb/db/snapshot.h
#        src/leveldb/db/table_cache.cc
#        src/leveldb/db/table_cache.h
#        src/leveldb/db/version_edit.cc
#        src/leveldb/db/version_edit.h
#        src/leveldb/db/version_edit_test.cc
#        src/leveldb/db/version_set.cc
#        src/leveldb/db/version_set.h
#        src/leveldb/db/version_set_test.cc
#        src/leveldb/db/write_batch.cc
#        src/leveldb/db/write_batch_internal.h
#        src/leveldb/db/write_batch_test.cc
#        src/leveldb/doc/bench/db_bench_sqlite3.cc
#        src/leveldb/doc/bench/db_bench_tree_db.cc
#        src/leveldb/helpers/memenv/memenv.cc
#        src/leveldb/helpers/memenv/memenv.h
#        src/leveldb/helpers/memenv/memenv_test.cc
#        src/leveldb/include/leveldb/c.h
#        src/leveldb/include/leveldb/cache.h
#        src/leveldb/include/leveldb/comparator.h
#        src/leveldb/include/leveldb/db.h
#        src/leveldb/include/leveldb/dumpfile.h
#        src/leveldb/include/leveldb/env.h
#        src/leveldb/include/leveldb/filter_policy.h
#        src/leveldb/include/leveldb/iterator.h
#        src/leveldb/include/leveldb/options.h
#        src/leveldb/include/leveldb/slice.h
#        src/leveldb/include/leveldb/status.h
#        src/leveldb/include/leveldb/table.h
#        src/leveldb/include/leveldb/table_builder.h
#        src/leveldb/include/leveldb/write_batch.h
#        src/leveldb/issues/issue178_test.cc
#        src/leveldb/issues/issue200_test.cc
#        src/leveldb/port/win/stdint.h
#        src/leveldb/port/atomic_pointer.h
#        src/leveldb/port/port.h
#        src/leveldb/port/port_example.h
#        src/leveldb/port/port_posix.cc
#        src/leveldb/port/port_posix.h
#        src/leveldb/port/port_posix_sse.cc
#        src/leveldb/port/port_win.cc
#        src/leveldb/port/port_win.h
#        src/leveldb/port/thread_annotations.h
#        src/leveldb/table/block.cc
#        src/leveldb/table/block.h
#        src/leveldb/table/block_builder.cc
#        src/leveldb/table/block_builder.h
#        src/leveldb/table/filter_block.cc
#        src/leveldb/table/filter_block.h
#        src/leveldb/table/filter_block_test.cc
#        src/leveldb/table/format.cc
#        src/leveldb/table/format.h
#        src/leveldb/table/iterator.cc
#        src/leveldb/table/iterator_wrapper.h
#        src/leveldb/table/merger.cc
#        src/leveldb/table/merger.h
#        src/leveldb/table/table.cc
#        src/leveldb/table/table_builder.cc
#        src/leveldb/table/table_test.cc
#        src/leveldb/table/two_level_iterator.cc
#        src/leveldb/table/two_level_iterator.h
#        src/leveldb/util/arena.cc
#        src/leveldb/util/arena.h
#        src/leveldb/util/arena_test.cc
#        src/leveldb/util/bloom.cc
#        src/leveldb/util/bloom_test.cc
#        src/leveldb/util/cache.cc
#        src/leveldb/util/cache_test.cc
#        src/leveldb/util/coding.cc
#        src/leveldb/util/coding.h
#        src/leveldb/util/coding_test.cc
#        src/leveldb/util/comparator.cc
#        src/leveldb/util/crc32c.cc
#        src/leveldb/util/crc32c.h
#        src/leveldb/util/crc32c_test.cc
#        src/leveldb/util/env.cc
#        src/leveldb/util/env_posix.cc
#        src/leveldb/util/env_posix_test.cc
#        src/leveldb/util/env_posix_test_helper.h
#        src/leveldb/util/env_test.cc
#        src/leveldb/util/env_win.cc
#        src/leveldb/util/filter_policy.cc
#        src/leveldb/util/hash.cc
#        src/leveldb/util/hash.h
#        src/leveldb/util/hash_test.cc
#        src/leveldb/util/histogram.cc
#        src/leveldb/util/histogram.h
#        src/leveldb/util/logging.cc
#        src/leveldb/util/logging.h
#        src/leveldb/util/mutexlock.h
#        src/leveldb/util/options.cc
#        src/leveldb/util/posix_logger.h
#        src/leveldb/util/random.h
#        src/leveldb/util/status.cc
#        src/leveldb/util/testharness.cc
#        src/leveldb/util/testharness.h
#        src/leveldb/util/testutil.cc
#        src/leveldb/util/testutil.h
#        src/policy/feerate.cpp
#        src/policy/feerate.h
#        src/policy/fees.cpp
#        src/policy/fees.h
#        src/policy/policy.cpp
#        src/policy/policy.h
#        src/policy/rbf.cpp
#        src/policy/rbf.h
#        src/primitives/block.cpp
#        src/primitives/block.h
#        src/primitives/transaction.cpp
#        src/primitives/transaction.h
#        src/qt/test/compattests.cpp
#        src/qt/test/compattests.h
#        src/qt/test/paymentrequestdata.h
#        src/qt/test/paymentservertests.cpp
#        src/qt/test/paymentservertests.h
#        src/qt/test/rpcnestedtests.cpp
#        src/qt/test/rpcnestedtests.h
#        src/qt/test/test_main.cpp
#        src/qt/test/uritests.cpp
#        src/qt/test/uritests.h
#        src/qt/test/wallettests.cpp
#        src/qt/test/wallettests.h
#        src/qt/addressbookpage.cpp
#        src/qt/addressbookpage.h
#        src/qt/addresstablemodel.cpp
#        src/qt/addresstablemodel.h
#        src/qt/askpassphrasedialog.cpp
#        src/qt/askpassphrasedialog.h
#        src/qt/bantablemodel.cpp
#        src/qt/bantablemodel.h
#        src/qt/bitcoin.cpp
#        src/qt/bitcoinaddressvalidator.cpp
#        src/qt/bitcoinaddressvalidator.h
#        src/qt/bitcoinamountfield.cpp
#        src/qt/bitcoinamountfield.h
#        src/qt/bitcoingui.cpp
#        src/qt/bitcoingui.h
#        src/qt/bitcoinstrings.cpp
#        src/qt/bitcoinunits.cpp
#        src/qt/bitcoinunits.h
#        src/qt/callback.h
#        src/qt/clientmodel.cpp
#        src/qt/clientmodel.h
#        src/qt/coincontroldialog.cpp
#        src/qt/coincontroldialog.h
#        src/qt/coincontroltreewidget.cpp
#        src/qt/coincontroltreewidget.h
#        src/qt/csvmodelwriter.cpp
#        src/qt/csvmodelwriter.h
#        src/qt/editaddressdialog.cpp
#        src/qt/editaddressdialog.h
#        src/qt/guiconstants.h
#        src/qt/guiutil.cpp
#        src/qt/guiutil.h
#        src/qt/intro.cpp
#        src/qt/intro.h
#        src/qt/macdockiconhandler.h
#        src/qt/macnotificationhandler.h
#        src/qt/modaloverlay.cpp
#        src/qt/modaloverlay.h
#        src/qt/networkstyle.cpp
#        src/qt/networkstyle.h
#        src/qt/notificator.cpp
#        src/qt/notificator.h
#        src/qt/openuridialog.cpp
#        src/qt/openuridialog.h
#        src/qt/optionsdialog.cpp
#        src/qt/optionsdialog.h
#        src/qt/optionsmodel.cpp
#        src/qt/optionsmodel.h
#        src/qt/overviewpage.cpp
#        src/qt/overviewpage.h
#        src/qt/paymentrequestplus.cpp
#        src/qt/paymentrequestplus.h
#        src/qt/paymentserver.cpp
#        src/qt/paymentserver.h
#        src/qt/peertablemodel.cpp
#        src/qt/peertablemodel.h
#        src/qt/platformstyle.cpp
#        src/qt/platformstyle.h
#        src/qt/qvalidatedlineedit.cpp
#        src/qt/qvalidatedlineedit.h
#        src/qt/qvaluecombobox.cpp
#        src/qt/qvaluecombobox.h
#        src/qt/receivecoinsdialog.cpp
#        src/qt/receivecoinsdialog.h
#        src/qt/receiverequestdialog.cpp
#        src/qt/receiverequestdialog.h
#        src/qt/recentrequeststablemodel.cpp
#        src/qt/recentrequeststablemodel.h
#        src/qt/rpcconsole.cpp
#        src/qt/rpcconsole.h
#        src/qt/sendcoinsdialog.cpp
#        src/qt/sendcoinsdialog.h
#        src/qt/sendcoinsentry.cpp
#        src/qt/sendcoinsentry.h
#        src/qt/signverifymessagedialog.cpp
#        src/qt/signverifymessagedialog.h
#        src/qt/splashscreen.cpp
#        src/qt/splashscreen.h
#        src/qt/trafficgraphwidget.cpp
#        src/qt/trafficgraphwidget.h
#        src/qt/transactiondesc.cpp
#        src/qt/transactiondesc.h
#        src/qt/transactiondescdialog.cpp
#        src/qt/transactiondescdialog.h
#        src/qt/transactionfilterproxy.cpp
#        src/qt/transactionfilterproxy.h
#        src/qt/transactionrecord.cpp
#        src/qt/transactionrecord.h
#        src/qt/transactiontablemodel.cpp
#        src/qt/transactiontablemodel.h
#        src/qt/transactionview.cpp
#        src/qt/transactionview.h
#        src/qt/utilitydialog.cpp
#        src/qt/utilitydialog.h
#        src/qt/walletframe.cpp
#        src/qt/walletframe.h
#        src/qt/walletmodel.cpp
#        src/qt/walletmodel.h
#        src/qt/walletmodeltransaction.cpp
#        src/qt/walletmodeltransaction.h
#        src/qt/walletview.cpp
#        src/qt/walletview.h
#        src/qt/winshutdownmonitor.cpp
#        src/qt/winshutdownmonitor.h
#        src/rpc/blockchain.cpp
#        src/rpc/blockchain.h
#        src/rpc/client.cpp
#        src/rpc/client.h
#        src/rpc/mining.cpp
#        src/rpc/mining.h
#        src/rpc/misc.cpp
#        src/rpc/net.cpp
#        src/rpc/protocol.cpp
#        src/rpc/protocol.h
#        src/rpc/rawtransaction.cpp
#        src/rpc/register.h
#        src/rpc/server.cpp
#        src/rpc/server.h
#        src/script/bitcoinconsensus.cpp
#        src/script/bitcoinconsensus.h
#        src/script/interpreter.cpp
#        src/script/interpreter.h
#        src/script/ismine.cpp
#        src/script/ismine.h
#        src/script/script.cpp
#        src/script/script.h
#        src/script/script_error.cpp
#        src/script/script_error.h
#        src/script/sigcache.cpp
#        src/script/sigcache.h
#        src/script/sign.cpp
#        src/script/sign.h
#        src/script/standard.cpp
#        src/script/standard.h
#        src/secp256k1/contrib/lax_der_parsing.c
#        src/secp256k1/contrib/lax_der_parsing.h
#        src/secp256k1/contrib/lax_der_privatekey_parsing.c
#        src/secp256k1/contrib/lax_der_privatekey_parsing.h
#        src/secp256k1/include/secp256k1.h
#        src/secp256k1/include/secp256k1_ecdh.h
#        src/secp256k1/include/secp256k1_recovery.h
#        src/secp256k1/src/java/org_bitcoin_NativeSecp256k1.c
#        src/secp256k1/src/java/org_bitcoin_NativeSecp256k1.h
#        src/secp256k1/src/java/org_bitcoin_Secp256k1Context.c
#        src/secp256k1/src/java/org_bitcoin_Secp256k1Context.h
#        src/secp256k1/src/modules/ecdh/main_impl.h
#        src/secp256k1/src/modules/ecdh/tests_impl.h
#        src/secp256k1/src/modules/recovery/main_impl.h
#        src/secp256k1/src/modules/recovery/tests_impl.h
#        src/secp256k1/src/basic-config.h
#        src/secp256k1/src/bench.h
#        src/secp256k1/src/bench_ecdh.c
#        src/secp256k1/src/bench_internal.c
#        src/secp256k1/src/bench_recover.c
#        src/secp256k1/src/bench_sign.c
#        src/secp256k1/src/bench_verify.c
#        src/secp256k1/src/ecdsa.h
#        src/secp256k1/src/ecdsa_impl.h
#        src/secp256k1/src/eckey.h
#        src/secp256k1/src/eckey_impl.h
#        src/secp256k1/src/ecmult.h
#        src/secp256k1/src/ecmult_const.h
#        src/secp256k1/src/ecmult_const_impl.h
#        src/secp256k1/src/ecmult_gen.h
#        src/secp256k1/src/ecmult_gen_impl.h
#        src/secp256k1/src/ecmult_impl.h
#        src/secp256k1/src/field.h
#        src/secp256k1/src/field_10x26.h
#        src/secp256k1/src/field_10x26_impl.h
#        src/secp256k1/src/field_5x52.h
#        src/secp256k1/src/field_5x52_asm_impl.h
#        src/secp256k1/src/field_5x52_impl.h
#        src/secp256k1/src/field_5x52_int128_impl.h
#        src/secp256k1/src/field_impl.h
#        src/secp256k1/src/gen_context.c
#        src/secp256k1/src/group.h
#        src/secp256k1/src/group_impl.h
#        src/secp256k1/src/hash.h
#        src/secp256k1/src/hash_impl.h
#        src/secp256k1/src/num.h
#        src/secp256k1/src/num_gmp.h
#        src/secp256k1/src/num_gmp_impl.h
#        src/secp256k1/src/num_impl.h
#        src/secp256k1/src/scalar.h
#        src/secp256k1/src/scalar_4x64.h
#        src/secp256k1/src/scalar_4x64_impl.h
#        src/secp256k1/src/scalar_8x32.h
#        src/secp256k1/src/scalar_8x32_impl.h
#        src/secp256k1/src/scalar_impl.h
#        src/secp256k1/src/scalar_low.h
#        src/secp256k1/src/scalar_low_impl.h
#        src/secp256k1/src/secp256k1.c
#        src/secp256k1/src/testrand.h
#        src/secp256k1/src/testrand_impl.h
#        src/secp256k1/src/tests.c
#        src/secp256k1/src/tests_exhaustive.c
#        src/secp256k1/src/util.h
#        src/support/allocators/secure.h
#        src/support/allocators/zeroafterfree.h
#        src/support/cleanse.cpp
#        src/support/cleanse.h
#        src/support/events.h
#        src/support/lockedpool.cpp
#        src/support/lockedpool.h
#        src/test/addrman_tests.cpp
#        src/test/allocator_tests.cpp
#        src/test/amount_tests.cpp
#        src/test/arith_uint256_tests.cpp
#        src/test/base32_tests.cpp
#        src/test/base58_tests.cpp
#        src/test/base64_tests.cpp
#        src/test/bip32_tests.cpp
#        src/test/blockencodings_tests.cpp
#        src/test/bloom_tests.cpp
#        src/test/bswap_tests.cpp
#        src/test/checkqueue_tests.cpp
#        src/test/coins_tests.cpp
#        src/test/compress_tests.cpp
#        src/test/crypto_tests.cpp
#        src/test/cuckoocache_tests.cpp
#        src/test/dbwrapper_tests.cpp
#        src/test/DoS_tests.cpp
#        src/test/getarg_tests.cpp
#        src/test/hash_tests.cpp
#        src/test/key_tests.cpp
#        src/test/limitedmap_tests.cpp
#        src/test/main_tests.cpp
#        src/test/mempool_tests.cpp
#        src/test/merkle_tests.cpp
#        src/test/miner_tests.cpp
#        src/test/multisig_tests.cpp
#        src/test/net_tests.cpp
#        src/test/netbase_tests.cpp
#        src/test/pmt_tests.cpp
#        src/test/policyestimator_tests.cpp
#        src/test/pow_tests.cpp
#        src/test/prevector_tests.cpp
#        src/test/raii_event_tests.cpp
#        src/test/random_tests.cpp
#        src/test/reverselock_tests.cpp
#        src/test/rpc_tests.cpp
#        src/test/sanity_tests.cpp
#        src/test/scheduler_tests.cpp
#        src/test/script_P2SH_tests.cpp
#        src/test/script_standard_tests.cpp
#        src/test/script_tests.cpp
#        src/test/scriptnum10.h
#        src/test/scriptnum_tests.cpp
#        src/test/serialize_tests.cpp
#        src/test/sighash_tests.cpp
#        src/test/sigopcount_tests.cpp
#        src/test/skiplist_tests.cpp
#        src/test/streams_tests.cpp
#        src/test/test_bitcoin.cpp
#        src/test/test_bitcoin.h
#        src/test/test_bitcoin_fuzzy.cpp
#        src/test/test_bitcoin_main.cpp
#        src/test/timedata_tests.cpp
#        src/test/torcontrol_tests.cpp
#        src/test/transaction_tests.cpp
#        src/test/txvalidationcache_tests.cpp
#        src/test/uint256_tests.cpp
#        src/test/univalue_tests.cpp
#        src/test/util_tests.cpp
#        src/test/versionbits_tests.cpp
#        src/univalue/gen/gen.cpp
#        src/univalue/include/univalue.h
#        src/univalue/lib/univalue.cpp
#        src/univalue/lib/univalue_escapes.h
#        src/univalue/lib/univalue_read.cpp
#        src/univalue/lib/univalue_utffilter.h
#        src/univalue/lib/univalue_write.cpp
#        src/univalue/test/unitester.cpp
#        src/wallet/test/accounting_tests.cpp
#        src/wallet/test/crypto_tests.cpp
#        src/wallet/test/wallet_test_fixture.cpp
#        src/wallet/test/wallet_test_fixture.h
#        src/wallet/test/wallet_tests.cpp
#        src/wallet/coincontrol.h
#        src/wallet/crypter.cpp
#        src/wallet/crypter.h
#        src/wallet/db.cpp
#        src/wallet/db.h
#        src/wallet/feebumper.cpp
#        src/wallet/feebumper.h
#        src/wallet/rpcdump.cpp
#        src/wallet/rpcwallet.cpp
#        src/wallet/rpcwallet.h
#        src/wallet/wallet.cpp
#        src/wallet/wallet.h
#        src/wallet/walletdb.cpp
#        src/wallet/walletdb.h
#        src/zmq/zmqabstractnotifier.cpp
#        src/zmq/zmqabstractnotifier.h
#        src/zmq/zmqconfig.h
#        src/zmq/zmqnotificationinterface.cpp
#        src/zmq/zmqnotificationinterface.h
#        src/zmq/zmqpublishnotifier.cpp
#        src/zmq/zmqpublishnotifier.h
#        src/addrdb.cpp
#        src/addrdb.h
#        src/addrman.cpp
#        src/addrman.h
#        src/amount.h
#        src/arith_uint256.cpp
#        src/arith_uint256.h
#        src/base58.cpp
#        src/base58.h
#        src/bitcoin-cli.cpp
#        src/bitcoin-tx.cpp
#        src/bitcoind.cpp
#        src/blockencodings.cpp
#        src/blockencodings.h
#        src/bloom.cpp
#        src/bloom.h
#        src/chain.cpp
#        src/chain.h
#        src/chainparams.cpp
#        src/chainparams.h
#        src/chainparamsbase.cpp
#        src/chainparamsbase.h
#        src/chainparamsseeds.h
#        src/checkpoints.cpp
#        src/checkpoints.h
#        src/checkqueue.h
#        src/clientversion.cpp
#        src/clientversion.h
#        src/coins.cpp
#        src/coins.h
#        src/compat.h
#        src/compressor.cpp
#        src/compressor.h
#        src/core_io.h
#        src/core_memusage.h
#        src/core_read.cpp
#        src/core_write.cpp
#        src/cuckoocache.h
#        src/dbwrapper.cpp
#        src/dbwrapper.h
#        src/fs.cpp
#        src/fs.h
#        src/hash.cpp
#        src/hash.h
#        src/httprpc.cpp
#        src/httprpc.h
#        src/httpserver.cpp
#        src/httpserver.h
#        src/indirectmap.h
#        src/init.cpp
#        src/init.h
#        src/key.cpp
#        src/key.h
#        src/keystore.cpp
#        src/keystore.h
#        src/limitedmap.h
#        src/memusage.h
#        src/merkleblock.cpp
#        src/merkleblock.h
#        src/miner.cpp
#        src/miner.h
#        src/net.cpp
#        src/net.h
#        src/net_processing.cpp
#        src/net_processing.h
#        src/netaddress.cpp
#        src/netaddress.h
#        src/netbase.cpp
#        src/netbase.h
#        src/netmessagemaker.h
#        src/noui.cpp
#        src/noui.h
#        src/pow.cpp
#        src/pow.h
#        src/prevector.h
#        src/protocol.cpp
#        src/protocol.h
#        src/pubkey.cpp
#        src/pubkey.h
#        src/random.cpp
#        src/random.h
#        src/rest.cpp
#        src/reverse_iterator.h
#        src/reverselock.h
#        src/scheduler.cpp
#        src/scheduler.h
#        src/serialize.h
#        src/streams.h
#        src/sync.cpp
#        src/sync.h
#        src/threadinterrupt.cpp
#        src/threadinterrupt.h
#        src/threadsafety.h
#        src/timedata.cpp
#        src/timedata.h
#        src/tinyformat.h
#        src/torcontrol.cpp
#        src/torcontrol.h
#        src/txdb.cpp
#        src/txdb.h
#        src/txmempool.cpp
#        src/txmempool.h
#        src/ui_interface.cpp
#        src/ui_interface.h
#        src/uint256.cpp
#        src/uint256.h
#        src/undo.h
#        src/util.cpp
#        src/util.h
#        src/utilmoneystr.cpp
#        src/utilmoneystr.h
#        src/utilstrencodings.cpp
#        src/utilstrencodings.h
#        src/utiltime.cpp
#        src/utiltime.h
#        src/validation.cpp
#        src/validation.h
#        src/validationinterface.cpp
#        src/validationinterface.h
#        src/version.h
#        src/versionbits.cpp
#        src/versionbits.h
#        src/warnings.cpp
#        src/warnings.h)
