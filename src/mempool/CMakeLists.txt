file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

add_library(mempool ${sources} ${headers} )
