file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

add_library(framwork ${sources} ${headers})
