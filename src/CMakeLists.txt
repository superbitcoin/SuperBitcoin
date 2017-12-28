



#file(GLOB headers "*.h")

#add_subdirectory( src )
#add_subdirectory( bench )
add_subdirectory( compat )
#add_subdirectory( config )
add_subdirectory( consensus )
add_subdirectory( crypto )
add_subdirectory( leveldb )
add_subdirectory( policy )
add_subdirectory( primitives )
add_subdirectory( rpc )
add_subdirectory( script )
add_subdirectory( secp256k1 )
#univalue
#include(ProjectSecp256k1)

add_subdirectory( support )
add_subdirectory( wallet )
#add_subdirectory( zmq )
add_subdirectory( univalue )

file(GLOB sources "*.cpp")
file(GLOB headers "*.h")
#file(GLOB headers "primitives/*.h")



add_library(base ${sources} ${headers})
#target_link_libraries(base rpc)

#target_link_libraries(sbtcd
#        compat consensus crypto policy leveldb
#        primitives rpc script secp256k1 support
#        univalue wallet zmq  base ${EVENT_LIBRARIES}  libevent_pthreads.so ${Boost_LIBRARIES} miniupnpc ${OPENSSL_LIBRARIES}
#
#        )

#find_package(LevelDB)
#IF(LEVELDB_FOUND)
#message( FFFFFFFFFFFFFFFFFFF ${LEVELDB_LIBRARIES})
#ENDIF()
target_include_directories(base SYSTEM PUBLIC "${LEVELDB_INCLUDE_DIR}" "${LEVELDB_INCLUDE_DIR}/leveldb/helpers")


find_package(Miniupnpc)
find_package(OpenSSL)
find_package(Event)

add_subdirectory(sbtcd)
add_subdirectory(sbtc-tx)
add_subdirectory(sbtc-cli)



#add_executable(sbtcd ${SOURCE_FILES})
## 指定生成目标
##add_executable(sbtcd ${DIR_SRCS} )
#
#target_link_libraries(sbtcdcd
#        bench compat comsensus crypto leveldb policy
#        primitives rpc script secp256k1kk support
#        univalue wallet zmq
#        )




#add_executable(sbtcd bitcoind.cpp ${headers})
#target_link_libraries(fuckeddd)