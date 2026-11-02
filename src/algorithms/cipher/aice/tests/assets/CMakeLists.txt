# Test/benchmark oracle only; never linked into the production shared library.
foreach(batch 128 256 512)
    add_library(aice_cpu_${batch} OBJECT aice_cpu_batch.c)
    target_compile_definitions(aice_cpu_${batch} PRIVATE
        BATCH=${batch} AICE_CPU_BATCH_CRYPT=AiceCpuCrypt${batch}
        aice_encrypt_cpu=AiceCpuEncrypt${batch} aice_decrypt_cpu=AiceCpuDecrypt${batch}
        ai_stream_init=AiceCpuInit${batch} ai_stream_table_init=AiceCpuTableInit${batch}
        ai_stream_encrypt=AiceCpuStreamEncrypt${batch} ai_stream_decrypt=AiceCpuStreamDecrypt${batch})
    set_target_properties(aice_cpu_${batch} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()
add_library(aice_cpu STATIC
    $<TARGET_OBJECTS:aice_cpu_128> $<TARGET_OBJECTS:aice_cpu_256> $<TARGET_OBJECTS:aice_cpu_512>)
target_include_directories(aice_cpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(aice_cpu PROPERTIES POSITION_INDEPENDENT_CODE ON)
