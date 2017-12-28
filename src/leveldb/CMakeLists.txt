#file(GLOB HEADERS "*.h")

include_directories ("${PROJECT_SOURCE_DIR}/src" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/include" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/include/leveldb" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/port" )
#include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/port/win" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/table" )
include_directories ("${PROJECT_SOURCE_DIR}/src/leveldb/util" )


#add_subdirectory( include )

#include_directories ("${PROJECT_SOURCE_DIR}/db" )
#include_directories ("${PROJECT_SOURCE_DIR}/include/leveldb" )
#include_directories ("${PROJECT_SOURCE_DIR}/port" )
#include_directories ("${PROJECT_SOURCE_DIR}/table" )
#include_directories ("${PROJECT_SOURCE_DIR}/util" )

set(    DIR_SUB_SRCS

        db/autocompact_test.cc
        db/builder.cc
        db/builder.h
        db/c.cc
        db/c_test.c
        db/corruption_test.cc
        db/db_bench.cc
        db/db_impl.cc
        db/db_impl.h
        db/db_iter.cc
        db/db_iter.h
        db/db_test.cc
        db/dbformat.cc
        db/dbformat.h
        db/dbformat_test.cc
        db/dumpfile.cc
        db/fault_injection_test.cc
        db/filename.cc
        db/filename.h
        db/filename_test.cc
        db/leveldbutil.cc
        db/log_format.h
        db/log_reader.cc
        db/log_reader.h
        db/log_test.cc
        db/log_writer.cc
        db/log_writer.h
        db/memtable.cc
        db/memtable.h
        db/recovery_test.cc
        db/repair.cc
        db/skiplist.h
        db/skiplist_test.cc
        db/snapshot.h
        db/table_cache.cc
        db/table_cache.h
        db/version_edit.cc
        db/version_edit.h
        db/version_edit_test.cc
        db/version_set.cc
        db/version_set.h
        db/version_set_test.cc
        db/write_batch.cc
        db/write_batch_internal.h
        db/write_batch_test.cc
#        doc/bench/db_bench_sqlite3.cc
#        doc/bench/db_bench_tree_db.cc
        helpers/memenv/memenv.cc
        helpers/memenv/memenv.h
        helpers/memenv/memenv_test.cc
        include/leveldb/c.h
        include/leveldb/cache.h
        include/leveldb/comparator.h
        include/leveldb/db.h
        include/leveldb/dumpfile.h
        include/leveldb/env.h
        include/leveldb/filter_policy.h
        include/leveldb/iterator.h
        include/leveldb/options.h
        include/leveldb/slice.h
        include/leveldb/status.h
        include/leveldb/table.h
        include/leveldb/table_builder.h
        include/leveldb/write_batch.h
        issues/issue178_test.cc
        issues/issue200_test.cc
        port/win/stdint.h
        port/atomic_pointer.h
        port/port.h
        port/port_example.h
        port/port_posix.cc
        port/port_posix.h
        port/port_posix_sse.cc
#        port/port_win.cc
#        port/port_win.h
        port/thread_annotations.h
        table/block.cc
        table/block.h
        table/block_builder.cc
        table/block_builder.h
        table/filter_block.cc
        table/filter_block.h
        table/filter_block_test.cc
        table/format.cc
        table/format.h
        table/iterator.cc
        table/iterator_wrapper.h
        table/merger.cc
        table/merger.h
        table/table.cc
        table/table_builder.cc
        table/table_test.cc
        table/two_level_iterator.cc
        table/two_level_iterator.h
        util/arena.cc
        util/arena.h
        util/arena_test.cc
        util/bloom.cc
        util/bloom_test.cc
        util/cache.cc
        util/cache_test.cc
        util/coding.cc
        util/coding.h
        util/coding_test.cc
        util/comparator.cc
        util/crc32c.cc
        util/crc32c.h
        util/crc32c_test.cc
        util/env.cc
        util/env_posix.cc
        util/env_posix_test.cc
        util/env_posix_test_helper.h
        util/env_test.cc
        util/env_win.cc
        util/filter_policy.cc
        util/hash.cc
        util/hash.h
        util/hash_test.cc
        util/histogram.cc
        util/histogram.h
        util/logging.cc
        util/logging.h
        util/mutexlock.h
        util/options.cc
        util/posix_logger.h
        util/random.h
        util/status.cc
        util/testharness.cc
        util/testharness.h
        util/testutil.cc
        util/testutil.h
)


#aux_source_directory(. DIR_SUB_SRCS)

add_library(leveldb ${DIR_SUB_SRCS} )
#target_link_libraries( account_history_api_plugin account_history_plugin chain_plugin http_plugin appbase )
#target_include_directories( account_history_api_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

## 指定 bench 库的安装路径
#install( TARGETS
#        leveldb
#        RUNTIME DESTINATION bin
#        LIBRARY DESTINATION lib
#        ARCHIVE DESTINATION lib
#        )
#install( FILES ${HEADERS} DESTINATION include )