file(GLOB sources "*.cpp")
file(GLOB interfaceH "../interface/*.h")
link_directories(../rpc)

add_executable(sbtcd ${sources} ${interfaceH} )
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
        libboost_random.a ${Secp256k1_LIBRARY} ${Secp256k1_LIBRARY} contract-api eventmanager ${Secp256k1_LIBRARY} libboost_random.a contract libboost_random.a ${Secp256k1_LIBRARY} base chaincontrol
         compat config  libboost_random.a contract libboost_random.a p2p framework  ${Secp256k1_LIBRARY} contract-api ${Boost_LIBRARIES} contract ${Boost_LIBRARIES} mempool miner  rpc sbtccore univalue utils wallet
        ${EVENT_LIBRARIES} ${LOG4CPP_LIBRARYS} libevent_pthreads.so ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
        ${LIBDB_CXX_LIBRARIES} ${LEVELDB_LIBRARIES} libmemenv.a ${Secp256k1_LIBRARY}
        )



message( ${LEVELDB_LIBRARIES})
message( ${LOG4CPP_LIBRARYS})