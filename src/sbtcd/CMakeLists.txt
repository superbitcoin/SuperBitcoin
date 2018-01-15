file(GLOB sources "*.cpp")
link_directories(../rpc)

add_executable(sbtcd ${sources} )
target_include_directories(sbtcd PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${Secp256k1_INCLUDE_DIR} )

target_link_libraries(sbtcd
        base
        compat consensus crypto  policy
        primitives rpc script  support
        univalue wallet   ${EVENT_LIBRARIES}  libevent_pthreads.so ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
        ${LIBDB_CXX_LIBRARIES} ${LEVELDB_LIBRARIES} libmemenv.a ${Secp256k1_LIBRARY}
        )

message( ${Secp256k1_LIBRARY})
