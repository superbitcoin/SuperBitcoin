

#add_subdirectory( base )
add_subdirectory( chaincontrol )
add_subdirectory( compat )
add_subdirectory( config )
add_subdirectory( contract )
add_subdirectory( contract-api)
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


#file(GLOB sources "*.cpp" "utils/*.cpp" )
#file(GLOB headers "*.h" "utils/*.h" )

file(GLOB sources "*.cpp" "base/*.cpp" "chaincontrol/*.cpp" "compat/*.cpp" "config/*.cpp" "framework/*.cpp"
        "mempool/*.cpp" "miner/*.cpp" "p2p/*.cpp"  "rpc/*.cpp" "sbtccore/*.cpp" "sbtccore/block/*.cpp" "sbtccore/transaction/*.cpp"
        "sbtccore/transaction/script/*.cpp" "univalue/*.cpp" "eventmanager/*.cpp" "interface/*.cpp" "utils/*.cpp" "wallet/*.cpp"   "utils/crypto/*.cpp" "utils/crypto/allocators/*.cpp" "utils/crypto/ctaes/*.c" )
#"zmq/*.cpp"
file(GLOB headers "*.h"  "base/*.hpp" "chaincontrol/*.h" "compat/*.h" "config/*.h" "framework/*.h" "framework/*.hpp" "mempool/*.h" "miner/*.h" "p2p/*.h" "rpc/*.h" "sbtccore/*.h"
        "sbtccore/transaction/*.h" "sbtccore/block/*.h" "btccore/transaction/script/*.h" "univalue/*.h" "utils/*.h" "wallet/*.h"
        "utils/crypto/*.h" "utils/crypto/allocators/*.h" "eventmanager/*.h" "interface/*.h" "utils/crypto/ctaes/*.h" )
#"zmq/*.h"

add_library(base ${sources} ${headers} )

find_package(LevelDB)
target_include_directories(base SYSTEM PUBLIC "${LEVELDB_INCLUDE_DIR}" "${LEVELDB_INCLUDE_DIR}/leveldb/helpers")
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