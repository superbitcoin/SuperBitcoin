file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

add_library(mempool ${sources} ${headers} orphantx.h orphantx.cpp txmempoolentry.h txmempoolentry.cpp)
