# Integration tests against the real libobs engine (need display, audio server,
# RTMP server: see scripts/linux/test-env.sh). Labelled "integration".
add_executable(tst_engine tst_engine.cpp)
target_link_libraries(tst_engine PRIVATE ms_engine Qt6::Test Qt6::Gui Qt6::Widgets)
add_test(NAME engine COMMAND tst_engine)
set_tests_properties(engine PROPERTIES LABELS integration TIMEOUT 900)
