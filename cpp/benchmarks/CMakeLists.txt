add_executable(bench_smoke bench_smoke.cpp)
target_link_libraries(bench_smoke PRIVATE lob_core benchmark::benchmark)
