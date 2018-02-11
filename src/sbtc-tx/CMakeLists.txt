file(GLOB sources "*.cpp")

link_directories(../rpc)

add_executable(sbtc-tx ${sources} )
target_include_directories(sbtc-tx PUBLIC ${CMAKE_CURRENT_BINARY_DIR} )

target_link_libraries(sbtc-tx
        base
        chaincontrol compat config framework mempool miner p2p rpc sbtccore univalue utils wallet
        ${EVENT_LIBRARIES}  libevent_pthreads.so ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
        ${LIBDB_CXX_LIBRARIES} ${LEVELDB_LIBRARIES} libmemenv.a ${Secp256k1_LIBRARY}
        )
