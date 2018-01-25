file(GLOB sources "*.cpp")
file(GLOB headers "*.h")
set(   depSources
        ../miner/miner.h
        ../miner/miner.cpp
        ../sbtccore/core_read.cpp
        ../sbtccore/core_io.h
        ../wallet/rbf.cpp
        ../wallet/rbf.h
        ../wallet/keystore.h
        ../wallet/keystore.cpp
        ../utils/net/httpserver.cpp
        ../utils/net/httpserver.h
)
message(rpcsources   ${depSources})

add_library(rpc ${sources} ${depSources} ${headers}  )

