add_executable(bench_smoke bench_smoke.cpp)
target_link_libraries(bench_smoke PRIVATE lob_core benchmark::benchmark)

add_executable(bench_parser bench_parser.cpp)
target_link_libraries(bench_parser PRIVATE lob_core benchmark::benchmark)

add_executable(bench_reconstructor bench_reconstructor.cpp)
target_link_libraries(bench_reconstructor PRIVATE lob_core benchmark::benchmark)

add_executable(bench_feature_engine bench_feature_engine.cpp)
target_link_libraries(bench_feature_engine PRIVATE lob_core benchmark::benchmark)

add_executable(bench_pipeline bench_pipeline.cpp)
target_link_libraries(bench_pipeline PRIVATE lob_core benchmark::benchmark)

add_executable(bench_itch_parser bench_itch_parser.cpp)
target_link_libraries(bench_itch_parser PRIVATE lob_core benchmark::benchmark)
