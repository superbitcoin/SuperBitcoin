file(GLOB sources "*.cpp" "block/*.cpp" "transaction/*.cpp" "transaction/script/*.cpp")
file(GLOB headers "block/*.h" "transaction*.h" "transaction/script/*.h" )
link_directories(../utils)
set(depSources
        ../utils/crypto/aes.h
        ../utils/crypto/aes.cpp
        )
add_library(sbtccore ${sources} ${headers} ${depSources})
