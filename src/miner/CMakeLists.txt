file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

add_library(miner ${sources} ${headers})
