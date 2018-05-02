file(GLOB sources "*.cpp")
file(GLOB interfaceH "../interface/*.h")
link_directories(../rpc)

add_executable(sbtcd ${sources} ${interfaceH} )
target_include_directories(sbtcd PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${Secp256k1_INCLUDE_DIR} )


set(LIB_FILE )

IF (ENABLE_ZMQ)
    set(LIB_FILE ${LIB_FILE} zmq ${LIBZMQ_LIBRARIES})
ENDIF ()

IF (ENABLE_STATIC_FLAG)
    set(LIB_FILE ${LIB_FILE} -ldl libsnappy.a)
ENDIF ()


target_link_libraries(sbtcd
        libboost_random.a ${Secp256k1_LIBRARY}  contract-api eventmanager  libboost_random.a contract libboost_random.a ${Secp256k1_LIBRARY} base chaincontrol
         compat config  libboost_random.a contract libboost_random.a p2p framework  ${Secp256k1_LIBRARY} contract-api ${Boost_LIBRARIES} contract ${Boost_LIBRARIES} mempool miner  rpc sbtccore univalue utils wallet
        ${EVENT_LIBRARIES} ${LOG4CPP_LIBRARYS} libevent_pthreads.a ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
        ${LIBDB_CXX_LIBRARIES} ${LEVELDB_LIBRARIES} libmemenv.a ${Secp256k1_LIBRARY}  ${LIB_FILE}
        )


#
#message( ${LEVELDB_LIBRARIES})
