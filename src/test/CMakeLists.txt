file(GLOB sources "*.cpp")
file(GLOB data_sources "data/*.cpp")
link_directories(../rpc)

add_executable(sbtc-test ${sources} ${data_sources})
target_include_directories(sbtc-test PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${Secp256k1_INCLUDE_DIR} )

target_link_libraries(sbtc-test
        base
        chaincontrol compat config framwork mempool miner p2p rpc sbtccore univalue utils wallet
        ${EVENT_LIBRARIES}  libevent_pthreads.so ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
        ${LIBDB_CXX_LIBRARIES} ${LEVELDB_LIBRARIES} libmemenv.a ${Secp256k1_LIBRARY}
        )

