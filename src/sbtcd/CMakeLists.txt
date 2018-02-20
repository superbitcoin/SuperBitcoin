file(GLOB sources "*.cpp")
link_directories(../rpc)

add_executable(sbtcd ${sources} )
target_include_directories(sbtcd PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${Secp256k1_INCLUDE_DIR} )

#target_link_libraries(sbtcd
#        base
#        sbtccore p2p
#        miner utils block transaction chain chainstate
#
#        compat consensus crypto  policy
#        rpc script  support
#        univalue wallet   ${EVENT_LIBRARIES}  libevent_pthreads.so ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
#        ${LIBDB_CXX_LIBRARIES} ${LEVELDB_LIBRARIES} libmemenv.a ${Secp256k1_LIBRARY}
#        )

target_link_libraries(sbtcd
        base
        chaincontrol compat config framework mempool miner p2p rpc sbtccore univalue utils wallet
        ${EVENT_LIBRARIES} log4cpp  libevent_pthreads.so ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
        ${LIBDB_CXX_LIBRARIES} ${LEVELDB_LIBRARIES} libmemenv.a ${Secp256k1_LIBRARY}
        )



message( ${LEVELDB_LIBRARIES})
