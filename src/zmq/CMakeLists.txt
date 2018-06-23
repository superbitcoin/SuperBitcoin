file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

IF (ENABLE_ZMQ)
    add_library(zmq ${sources} ${headers})
ENDIF ()

