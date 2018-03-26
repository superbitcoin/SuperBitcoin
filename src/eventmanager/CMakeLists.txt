file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

add_library(eventmanager ${sources} ${headers})
