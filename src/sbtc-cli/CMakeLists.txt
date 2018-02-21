file(GLOB sources "*.cpp")

link_directories(../rpc)

add_executable(sbtc-cli ${sources} )
target_include_directories(sbtc-cli PUBLIC ${CMAKE_CURRENT_BINARY_DIR} )

target_link_libraries(sbtc-cli
        base
        chaincontrol compat config framework mempool miner p2p rpc sbtccore univalue utils wallet
        ${EVENT_LIBRARIES} ${LOG4CPP_LIBRARYS} libevent_pthreads.so ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
        ${LIBDB_CXX_LIBRARIES}  ${LEVELDB_LIBRARIES} libmemenv.a ${Secp256k1_LIBRARY}
        )
