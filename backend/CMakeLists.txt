# gscope backend: a jetson_stats-style system monitor built on libhttp.
# Wired in from ../CMakeLists.txt.

add_library(gscope_core STATIC
    src/line_stream.cpp
    src/tegrastats_provider.cpp
    src/nvidia_smi_provider.cpp
    src/nvml_provider.cpp
    src/proc_provider.cpp
    src/process_sampler.cpp
    src/network_sampler.cpp
    src/frontend_version.cpp
    src/provider_factory.cpp
    src/json_codec.cpp
    src/metrics_service.cpp
)
target_include_directories(gscope_core PUBLIC include)
# NvmlProvider loads libnvidia-ml.so.1 via dlopen/dlsym at runtime instead of
# link-time (see include/gscope/nvml_provider.hpp) -- CMAKE_DL_LIBS is the
# portable way to pull in dlopen()/dlsym() (a no-op on glibc >= 2.34, where
# they live in libc itself; -ldl on older glibc). This never requires NVML
# to actually be present, so it doesn't affect Jetson builds.
target_link_libraries(gscope_core PUBLIC http ${CMAKE_DL_LIBS})
target_compile_options(gscope_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -O2>
)

add_executable(gscope-daemon main.cpp)
target_link_libraries(gscope-daemon PRIVATE gscope_core)
target_compile_options(gscope-daemon PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -O2>
)

# Embed public/index.html straight into the gscope-daemon binary (via
# objcopy) so the default deployment is the single executable, no sibling
# public/ directory required. --public-dir still opts back into serving
# from disk (see main.cpp). Requires GNU binutils' objcopy/objdump; falls
# back to disk-only serving (today's behavior) if either is unavailable,
# e.g. on a non-GNU toolchain.
set(GSCOPE_INDEX_HTML_SRC "${CMAKE_CURRENT_SOURCE_DIR}/public/index.html")
if(CMAKE_OBJCOPY AND CMAKE_OBJDUMP AND EXISTS "${GSCOPE_INDEX_HTML_SRC}")
    # objcopy's `-I binary` input has no format of its own for it to infer
    # an `-O`/`-B` from, unlike a real object file -- so the output target
    # (BFD format name, e.g. elf64-littleaarch64) and binary architecture
    # (e.g. aarch64) it needs are pulled from a throwaway object file this
    # same toolchain just built, rather than hardcoding a triple that would
    # only match one platform.
    set(GSCOPE_EMBED_PROBE_SRC "${CMAKE_CURRENT_BINARY_DIR}/embed_probe.cpp")
    set(GSCOPE_EMBED_PROBE_OBJ "${CMAKE_CURRENT_BINARY_DIR}/embed_probe.o")
    file(WRITE "${GSCOPE_EMBED_PROBE_SRC}" "int gscopeEmbedProbe;\n")
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -c "${GSCOPE_EMBED_PROBE_SRC}" -o "${GSCOPE_EMBED_PROBE_OBJ}"
        RESULT_VARIABLE GSCOPE_EMBED_PROBE_COMPILE_RESULT
    )
    if(GSCOPE_EMBED_PROBE_COMPILE_RESULT EQUAL 0)
        execute_process(
            COMMAND ${CMAKE_OBJDUMP} -f "${GSCOPE_EMBED_PROBE_OBJ}"
            OUTPUT_VARIABLE GSCOPE_EMBED_PROBE_INFO
        )
        string(REGEX MATCH "file format ([^ \n]+)" _ "${GSCOPE_EMBED_PROBE_INFO}")
        set(GSCOPE_OBJCOPY_OUTPUT_TARGET "${CMAKE_MATCH_1}")
        string(REGEX MATCH "architecture: ([^,]+)," _ "${GSCOPE_EMBED_PROBE_INFO}")
        set(GSCOPE_OBJCOPY_ARCH "${CMAKE_MATCH_1}")
    endif()

    if(GSCOPE_OBJCOPY_OUTPUT_TARGET AND GSCOPE_OBJCOPY_ARCH)
        set(GSCOPE_EMBED_INDEX_HTML_OBJ "${CMAKE_CURRENT_BINARY_DIR}/embedded_index_html.o")
        add_custom_command(
            OUTPUT "${GSCOPE_EMBED_INDEX_HTML_OBJ}"
            # Run from a directory containing only "index.html" so the
            # `_binary_index_html_{start,end}` symbol names objcopy derives
            # from the input filename are fixed, independent of this repo's
            # directory layout.
            COMMAND ${CMAKE_COMMAND} -E copy "${GSCOPE_INDEX_HTML_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/index.html"
            COMMAND ${CMAKE_OBJCOPY} -I binary -O ${GSCOPE_OBJCOPY_OUTPUT_TARGET} -B ${GSCOPE_OBJCOPY_ARCH}
                    index.html "${GSCOPE_EMBED_INDEX_HTML_OBJ}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS "${GSCOPE_INDEX_HTML_SRC}"
            COMMENT "Embedding public/index.html into gscope-daemon via objcopy"
            VERBATIM
        )
        set_source_files_properties("${GSCOPE_EMBED_INDEX_HTML_OBJ}" PROPERTIES
            EXTERNAL_OBJECT TRUE GENERATED TRUE)
        target_sources(gscope-daemon PRIVATE "${GSCOPE_EMBED_INDEX_HTML_OBJ}")
        target_compile_definitions(gscope-daemon PRIVATE GSCOPE_EMBEDDED_INDEX_HTML=1)
    else()
        message(WARNING "gscope-daemon: could not determine objcopy's output target/architecture "
                        "for this toolchain -- index.html will not be embedded; "
                        "--public-dir remains required")
    endif()
endif()

# The default --public-dir is "./public" (relative to the binary's cwd), so
# the assets have to sit next to it in the build tree to work when run from
# there. Overwritten by an actual Angular build (see frontend/README.md).
# Kept even when index.html is embedded above: a --public-dir override still
# needs somewhere to point, and this is the same directory --public-dir
# defaults to.
add_custom_command(TARGET gscope-daemon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/public"
            "$<TARGET_FILE_DIR:gscope-daemon>/public"
    COMMENT "Copying public/ next to gscope-daemon")

if(GSCOPE_BUILD_TESTS AND BUILD_TESTING)
    add_executable(gscope_test_tegrastats_parser tests/test_tegrastats_parser.cpp)
    target_link_libraries(gscope_test_tegrastats_parser PRIVATE gscope_core)
    add_test(NAME gscope_tegrastats_parser COMMAND gscope_test_tegrastats_parser)

    add_executable(gscope_test_tegrastats_provider_stream tests/test_tegrastats_provider_stream.cpp)
    target_link_libraries(gscope_test_tegrastats_provider_stream PRIVATE gscope_core)
    add_test(NAME gscope_tegrastats_provider_stream COMMAND gscope_test_tegrastats_provider_stream)

    add_executable(gscope_test_nvidia_smi_parser tests/test_nvidia_smi_parser.cpp)
    target_link_libraries(gscope_test_nvidia_smi_parser PRIVATE gscope_core)
    add_test(NAME gscope_nvidia_smi_parser COMMAND gscope_test_nvidia_smi_parser)

    add_executable(gscope_test_nvidia_smi_provider_stream tests/test_nvidia_smi_provider_stream.cpp)
    target_link_libraries(gscope_test_nvidia_smi_provider_stream PRIVATE gscope_core)
    add_test(NAME gscope_nvidia_smi_provider_stream COMMAND gscope_test_nvidia_smi_provider_stream)

    add_executable(gscope_test_nvml_provider tests/test_nvml_provider.cpp)
    target_link_libraries(gscope_test_nvml_provider PRIVATE gscope_core)
    add_test(NAME gscope_nvml_provider COMMAND gscope_test_nvml_provider)

    add_executable(gscope_test_proc_provider tests/test_proc_provider.cpp)
    target_link_libraries(gscope_test_proc_provider PRIVATE gscope_core)
    add_test(NAME gscope_proc_provider COMMAND gscope_test_proc_provider)

    add_executable(gscope_test_process_sampler tests/test_process_sampler.cpp)
    target_link_libraries(gscope_test_process_sampler PRIVATE gscope_core)
    add_test(NAME gscope_process_sampler COMMAND gscope_test_process_sampler)

    add_executable(gscope_test_network_sampler tests/test_network_sampler.cpp)
    target_link_libraries(gscope_test_network_sampler PRIVATE gscope_core)
    add_test(NAME gscope_network_sampler COMMAND gscope_test_network_sampler)

    add_executable(gscope_test_frontend_version tests/test_frontend_version.cpp)
    target_link_libraries(gscope_test_frontend_version PRIVATE gscope_core)
    add_test(NAME gscope_frontend_version COMMAND gscope_test_frontend_version)

    add_executable(gscope_test_history_buffer tests/test_history_buffer.cpp)
    target_link_libraries(gscope_test_history_buffer PRIVATE gscope_core)
    add_test(NAME gscope_history_buffer COMMAND gscope_test_history_buffer)

    add_executable(gscope_test_metrics_service tests/test_metrics_service.cpp)
    target_link_libraries(gscope_test_metrics_service PRIVATE gscope_core)
    add_test(NAME gscope_metrics_service COMMAND gscope_test_metrics_service)
endif()
