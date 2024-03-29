# Copyright 2017-2018, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.6)
project(pmemkv)

set(CMAKE_CXX_STANDARD 11)
set(SOURCE_FILES src/pmemkv.cc src/pmemkv.h
    src/engines/blackhole.h src/engines/blackhole.cc
    src/engines/kvtree.h src/engines/kvtree.cc
    src/engines/kvtree2.h src/engines/kvtree2.cc
    src/engines/versioned_b_tree.h src/engines/versioned_b_tree.cc
    src/engines/versioned_b_tree/persistent_b_tree.h src/engines/versioned_b_tree/pstring.h
)
set(3RDPARTY ${PROJECT_SOURCE_DIR}/3rdparty)
set(GTEST_VERSION 1.7.0)

find_package(PkgConfig QUIET)
include(ExternalProject)
include(FindThreads)

set(GTEST_URL ${CMAKE_SOURCE_DIR}/googletest-${GTEST_VERSION}.zip)
if(EXISTS ${GTEST_URL})
# do nothing
else()
execute_process(COMMAND wget -O ${GTEST_URL} https://github.com/google/googletest/archive/release-${GTEST_VERSION}.zip)
endif()
ExternalProject_Add(gtest
    URL ${GTEST_URL}
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest
    INSTALL_COMMAND ""
    CMAKE_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
)
ExternalProject_Get_Property(gtest source_dir binary_dir)
add_library(libgtest IMPORTED STATIC GLOBAL)
add_dependencies(libgtest gtest)
set_target_properties(libgtest PROPERTIES
    "IMPORTED_LOCATION" "${binary_dir}/libgtest.a"
    "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
)
include_directories("${source_dir}/include")

if(PKG_CONFIG_FOUND)
    pkg_check_modules(PMEMOBJ++ REQUIRED libpmemobj++)
else()
    find_package(PMEMOBJ++ REQUIRED)
endif()

include_directories(${PMEMOBJ++_INCLUDE_DIRS})
link_directories(${PMEMOBJ++_LIBRARY_DIRS})

add_library(pmemkv SHARED ${SOURCE_FILES})
target_link_libraries(pmemkv ${PMEMOBJ++_LIBRARIES})

add_executable(pmemkv_example src/pmemkv_example.cc)
target_link_libraries(pmemkv_example pmemkv)

add_executable(pmemkv_throughput src/pmemkv_throughput.cc)
target_link_libraries(pmemkv_throughput pmemkv pthread)

add_executable(pmemkv_test tests/pmemkv_test.cc tests/mock_tx_alloc.cc
               tests/engines/blackhole_test.cc
               tests/engines/kvtree_test.cc
               tests/engines/versioned_b_tree_test.cc
)
target_link_libraries(pmemkv_test pmemkv libgtest ${CMAKE_DL_LIBS})

include_directories(leveldb leveldb/include leveldb/port leveldb/util)
add_executable(pmemkv_bench
    src/pmemkv_bench.cc
    leveldb/include/leveldb/env.h
    leveldb/include/leveldb/slice.h
    leveldb/include/leveldb/status.h
    leveldb/port/atomic_pointer.h
    leveldb/port/port_posix.h leveldb/port/port_posix.cc
    leveldb/port/thread_annotations.h
    leveldb/util/env.cc
    leveldb/util/env_posix.cc
    leveldb/util/env_posix_test_helper.h
    leveldb/util/histogram.h leveldb/util/histogram.cc
    leveldb/util/logging.h leveldb/util/logging.cc
    leveldb/util/mutexlock.h
    leveldb/util/posix_logger.h
    leveldb/util/random.h
    leveldb/util/status.cc
)
target_link_libraries(pmemkv_bench pmemkv pthread)
