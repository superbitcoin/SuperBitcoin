file(GLOB sources "*.cpp")

link_directories(../rpc)

add_executable(sbtc-tx ${sources} )
target_include_directories(sbtc-tx PUBLIC ${CMAKE_CURRENT_BINARY_DIR} )

target_link_libraries(sbtc-tx
        base
        compat consensus crypto policy
        primitives rpc script  support
        univalue wallet   ${EVENT_LIBRARIES}  libevent_pthreads.so ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
        ${LIBDB_CXX_LIBRARIES} ${Secp256k1_LIBRARY}
        )
