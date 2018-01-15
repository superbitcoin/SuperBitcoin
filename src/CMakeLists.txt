
add_subdirectory( compat )
add_subdirectory( consensus )
add_subdirectory( crypto )
add_subdirectory( policy )
add_subdirectory( primitives )
add_subdirectory( rpc )
add_subdirectory( script )
add_subdirectory( support )
add_subdirectory( wallet )
add_subdirectory( univalue )

file(GLOB sources "*.cpp")
file(GLOB headers "*.h")


add_library(base ${sources} ${headers})

find_package(LevelDB)
target_include_directories(base SYSTEM PUBLIC "${LEVELDB_INCLUDE_DIR}" "${LEVELDB_INCLUDE_DIR}/leveldb/helpers")
find_package(Secp256k1)
find_package(Miniupnpc)
find_package(OpenSSL)
find_package(Event)

add_subdirectory(sbtcd)
add_subdirectory(sbtc-tx)
add_subdirectory(sbtc-cli)


