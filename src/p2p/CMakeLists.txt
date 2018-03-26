file(GLOB sources "*.cpp")
file(GLOB headers "*.h")


add_library(p2p ${sources}  ${headers} )
