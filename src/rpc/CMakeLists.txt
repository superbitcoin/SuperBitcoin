file(GLOB sources "*.cpp")
file(GLOB headers "*.h")
set(   depSources
        ../miner.h
        ../miner.cpp
        ../core_read.cpp
        ../core_io.h
        ../policy/rbf.cpp
        ../policy/rbf.h
        ../keystore.h
        ../keystore.cpp
)
message(rpcsources   ${depSources})

add_library(rpc ${sources} ${depSources} ${headers}  )


#target_link_libraries( eos_chain fc chainbase eos_types Logging IR WAST WASM Runtime )
#
#target_include_directories( eos_chain
#        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include"
#        "${CMAKE_CURRENT_SOURCE_DIR}/../wasm-jit/Include"
#        )
#
#if(MSVC)
#    set_source_files_properties( db_init.cpp db_block.cpp database.cpp block_log.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
#endif(MSVC)
#
#INSTALL( TARGETS
#        fuckeddd
#
#        RUNTIME DESTINATION bin
#        LIBRARY DESTINATION lib
#        ARCHIVE DESTINATION lib
#        )
#INSTALL( FILES ${headers} DESTINATION include )
