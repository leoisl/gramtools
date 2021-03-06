include(ExternalProject)
ExternalProject_Add(gtest
        URL https://github.com/google/googletest/archive/release-1.8.0.zip
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest
        INSTALL_COMMAND "")

# Get GTest source and binary directories from CMake project
ExternalProject_Get_Property(gtest source_dir binary_dir)

# Create a libgtest target to be used as a dependency by test programs
add_library(libgtest IMPORTED STATIC GLOBAL)
add_dependencies(libgtest gtest)

# Set libgtest properties
set_target_properties(libgtest PROPERTIES
        "IMPORTED_LOCATION" "${binary_dir}/googlemock/gtest/libgtest.a"
        "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}")

# Create a libgmock target to be used as a dependency by test programs
add_library(libgmock IMPORTED STATIC GLOBAL)
add_dependencies(libgmock gtest)

# Set libgmock properties
set_target_properties(libgmock PROPERTIES
        "IMPORTED_LOCATION" "${binary_dir}/googlemock/libgmock.a"
        "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}")

set(INCLUDE
        ${PROJECT_SOURCE_DIR}/libgramtools/include)

add_executable(test_main
        main.cpp

        test_search.cpp
        test_utils.cpp

        quasimap/coverage/test_common.cpp
        quasimap/coverage/test_allele_sum.cpp
        quasimap/coverage/test_allele_base.cpp
        quasimap/coverage/test_grouped_allele_counts.cpp
        quasimap/test_quasimap.cpp

        kmer_index/test_kmers.cpp
        kmer_index/test_build.cpp
        kmer_index/test_load.cpp
        kmer_index/test_dump.cpp

        prg/test_prg.cpp
        prg/test_masks.cpp)
target_link_libraries(test_main
        gramtools
        libgmock
        -lpthread
        -lm)
target_include_directories(test_main PUBLIC
        ${INCLUDE}
        ${source_dir}/googletest/include
        ${source_dir}/googlemock/include)
set_target_properties(test_main
        PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)

add_test(NAME test_main COMMAND test_main)
