file(GLOB sources "*.cpp")
#file(GLOB headers "*.h")

#add_library(wallet ${sources} ${headers})


link_directories(../rpc)

add_executable(sbtc-tx ${sources} )
target_include_directories(sbtc-tx PUBLIC ${CMAKE_CURRENT_BINARY_DIR} )
#message(ddddddddd  ${LEVELDB_LIBRARIES})

target_link_libraries(sbtc-tx
        base
        compat consensus crypto leveldb policy
        primitives rpc script secp256k1 support
        univalue wallet   ${EVENT_LIBRARIES}  libevent_pthreads.so ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
        ${LIBDB_CXX_LIBRARIES}
        )
#${LEVELDB_LIBRARIES}

#target_link_libraries( eos_chain fc chainbase eos_types Logging IR WAST WASM Runtime )
#
#target_include_directories( eos_chain
#        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include"
#        "${CMAKE_CURRENT_SOURCE_DIR}/../wasm-jit/Include"
#        )
#
#if(MSVC)
#    set_source_files_properties( db_init.cpp db_block.cpp database.cpp block_log.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
#endif(MSVC)
#
#INSTALL( TARGETS
#        fuckeddd
#
#        RUNTIME DESTINATION bin
#        LIBRARY DESTINATION lib
#        ARCHIVE DESTINATION lib
#        )
#INSTALL( FILES ${headers} DESTINATION include )
