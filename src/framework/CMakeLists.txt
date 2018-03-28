file(GLOB sources "*.cpp")
file(GLOB headers "*.h" "*.hpp")


add_library(framework ${sources} ${headers})
