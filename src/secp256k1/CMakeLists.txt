

set(    DIR_SUB_SRCS
        contrib/lax_der_parsing.c
        contrib/lax_der_parsing.h
        contrib/lax_der_privatekey_parsing.c
        contrib/lax_der_privatekey_parsing.h
        include/secp256k1.h
        include/secp256k1_ecdh.h
        include/secp256k1_recovery.h
        src/modules/ecdh/main_impl.h
        src/modules/ecdh/tests_impl.h
        src/modules/recovery/main_impl.h
        src/modules/recovery/tests_impl.h
        src/basic-config.h
        src/bench.h
        src/bench_ecdh.c
#        src/bench_internal.c
        src/bench_recover.c
        src/bench_sign.c
        src/bench_verify.c
        src/ecdsa.h
        src/ecdsa_impl.h
        src/eckey.h
        src/eckey_impl.h
        src/ecmult.h
        src/ecmult_const.h
        src/ecmult_const_impl.h
        src/ecmult_gen.h
        src/ecmult_gen_impl.h
        src/ecmult_impl.h
        #                src/ecmult_static_context.h
        src/field.h
        src/field_10x26.h
        src/field_10x26_impl.h
        src/field_5x52.h
        src/field_5x52_asm_impl.h
        src/field_5x52_impl.h
        src/field_5x52_int128_impl.h
        src/field_impl.h
        src/gen_context.c
        src/group.h
        src/group_impl.h
        src/hash.h
        src/hash_impl.h
        src/libsecp256k1-config.h
        src/num.h
        src/num_gmp.h
        src/num_gmp_impl.h
        src/num_impl.h
        src/scalar.h
        src/scalar_4x64.h
        src/scalar_4x64_impl.h
        src/scalar_8x32.h
        src/scalar_8x32_impl.h
        src/scalar_impl.h
        src/scalar_low.h
        src/scalar_low_impl.h
        src/secp256k1.c
#        src/testrand.h
#        src/testrand_impl.h
#        src/tests.c
#        src/tests_exhaustive.c
        src/util.h
        )




add_library(secp256k1  ${DIR_SUB_SRCS} )

#add_library(secp256k1 ${sources} ${headers})



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
