# ===========================================================================
# tests/CMakeLists.txt — Catch2 test targets
# ===========================================================================
include(Catch)

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
add_executable(canmatik_unit_tests
    unit/test_can_frame.cpp
    unit/test_timestamp.cpp
    unit/test_session_status.cpp
    unit/test_config.cpp
    unit/test_filter.cpp
    unit/test_asc_writer.cpp
    unit/test_asc_reader.cpp
    unit/test_jsonl_writer.cpp
    unit/test_jsonl_reader.cpp
    unit/test_label_store.cpp
    unit/test_interval_spec.cpp
    unit/test_obd_config.cpp
    unit/test_pid_table.cpp
    unit/test_pid_decoder.cpp
    unit/test_dtc_decoder.cpp
    unit/test_obd_request.cpp
    unit/test_obd_response.cpp
    unit/test_query_scheduler.cpp
    unit/test_gui_settings.cpp
    unit/test_frame_collector.cpp
    unit/test_replay_controller.cpp
    # GUI sources needed by GUI unit tests (no separate library)
    ${CMAKE_SOURCE_DIR}/src/gui/gui_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/frame_collector.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/controllers/replay_controller.cpp
)

target_link_libraries(canmatik_unit_tests PRIVATE
    canmatik_core
    canmatik_logging
    canmatik_obd
    canmatik_mock
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)

catch_discover_tests(canmatik_unit_tests)

# ---------------------------------------------------------------------------
# Integration tests
# ---------------------------------------------------------------------------
add_executable(canmatik_integration_tests
    integration/test_mock_capture.cpp
    integration/test_record_replay.cpp
    integration/test_cli_commands.cpp
    integration/test_edge_cases.cpp
    integration/test_replay_performance.cpp
    integration/test_capture_performance.cpp
    integration/test_obd_session.cpp
)

target_link_libraries(canmatik_integration_tests PRIVATE
    canmatik_core
    canmatik_mock
    canmatik_services
    canmatik_logging
    canmatik_obd
    Catch2::Catch2WithMain
)

catch_discover_tests(canmatik_integration_tests)
