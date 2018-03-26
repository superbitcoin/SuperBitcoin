file(GLOB sources "*.cpp")
file(GLOB headers "*.h")



add_library(chaincontrol ${sources} ${headers} )
