file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

add_library(primitives ${sources} ${headers})

