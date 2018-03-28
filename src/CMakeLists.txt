

add_subdirectory( base )
add_subdirectory( chaincontrol )
add_subdirectory( compat )
add_subdirectory( config )
add_subdirectory( contract )
add_subdirectory( contract-api)
add_subdirectory( eventmanager)
add_subdirectory( framework )
add_subdirectory( mempool )
add_subdirectory( miner )
add_subdirectory( p2p )
add_subdirectory( rpc )
add_subdirectory( sbtccore )
add_subdirectory( univalue )
add_subdirectory( utils )
add_subdirectory( wallet )
add_subdirectory( zmq )


find_package(LevelDB)
find_package(Secp256k1)
find_package(Miniupnpc)
find_package(OpenSSL)
find_package(Event)
find_package(log4cpp)



add_subdirectory(sbtcd)
add_subdirectory(sbtc-tx)
add_subdirectory(sbtc-cli)
if (TESTS)
    add_subdirectory(test)
endif()