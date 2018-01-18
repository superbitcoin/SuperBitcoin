file(GLOB sources "*.cpp")
file(GLOB headers "*.h")
set(   depSources
        ../miner.h
        ../miner.cpp
        ../core_read.cpp
        ../core_io.h
        ../policy/rbf.cpp
        ../policy/rbf.h
        ../keystore.h
        ../keystore.cpp
        ../httpserver.cpp
        ../httpserver.h
)
message(rpcsources   ${depSources})

add_library(rpc ${sources} ${depSources} ${headers}  )

