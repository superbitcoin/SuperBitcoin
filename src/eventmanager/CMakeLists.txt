file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

add_library(contract-api ${sources} ${headers})
