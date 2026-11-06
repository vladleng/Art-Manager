cmake_minimum_required(VERSION 3.22)
project(ArrangerManager VERSION 0.3.0 LANGUAGES C CXX)
set(ARRANGER_MANAGER_VERSION_SUFFIX "-Upd1-fix1")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.2 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
# Studio Pro Hub VST3 and ARA Inspector are retired from the build (Issue #7).
# Historical parser/catalog code remains only for importing/preserving legacy data.

juce_add_gui_app(ArrangerManagerDesktop
    COMPANY_NAME "Moon River Studio"
    BUNDLE_ID "com.moonriverstudio.arrangermanager"
    PRODUCT_NAME "Arranger Manager")
juce_generate_juce_header(ArrangerManagerDesktop)
target_sources(ArrangerManagerDesktop PRIVATE
    src/ArrangementTag.h src/SongCatalog.h src/SongNoteKeys.h src/HubApplication.cpp
    src/WorkspaceModel.h src/WorkspaceStore.h src/WorkspacePortable.h
    src/TaskPlanning.h src/TodayPlanning.h src/TaskPlanEditor.h
    src/WorkspaceQueries.h src/WorkspaceCommands.h src/WorkspaceController.h src/WorkspaceViewRouting.h src/WorkspaceTaskViews.h
    src/WorkspaceShell.h src/WorkspaceShell.cpp
    src/BlockEditorSession.h src/BlockEditor.h src/BlockEditor.cpp
    src/TaskDetails.h src/TaskDetails.cpp
    src/TaskTreeRows.h src/TaskTree.h src/TaskTree.cpp src/TaskQuickProperties.h)
target_compile_definitions(ArrangerManagerDesktop PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 ARRANGER_MANAGER_VERSION=\"${PROJECT_VERSION}${ARRANGER_MANAGER_VERSION_SUFFIX}\")
if(MSVC)
    target_compile_options(ArrangerManagerDesktop PRIVATE /utf-8)
endif()
target_link_libraries(ArrangerManagerDesktop PRIVATE juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

include(CTest)
add_executable(ArrangementTagTests tests/ArrangementTagTests.cpp)
target_include_directories(ArrangementTagTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(ArrangementTagTests PRIVATE cxx_std_20)
add_test(NAME ArrangementTagTests COMMAND ArrangementTagTests)

add_executable(SongSnapshotTests tests/SongSnapshotTests.cpp src/SongSnapshot.cpp)
target_include_directories(SongSnapshotTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(SongSnapshotTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(SongSnapshotTests PRIVATE /utf-8)
endif()
target_link_libraries(SongSnapshotTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME SongSnapshotTests COMMAND SongSnapshotTests)

add_executable(SongCatalogTests tests/SongCatalogTests.cpp)
target_include_directories(SongCatalogTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(SongCatalogTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(SongCatalogTests PRIVATE /utf-8)
endif()
target_link_libraries(SongCatalogTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME SongCatalogTests COMMAND SongCatalogTests)

add_executable(TodayPlanningTests tests/TodayPlanningTests.cpp)
target_include_directories(TodayPlanningTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(TodayPlanningTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(TodayPlanningTests PRIVATE /utf-8)
endif()
target_link_libraries(TodayPlanningTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME TodayPlanningTests COMMAND TodayPlanningTests)

add_executable(WorkspacePortableTests tests/WorkspacePortableTests.cpp)
target_include_directories(WorkspacePortableTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(WorkspacePortableTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(WorkspacePortableTests PRIVATE /utf-8)
endif()
target_link_libraries(WorkspacePortableTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME WorkspacePortableTests COMMAND WorkspacePortableTests)

add_executable(WorkspaceTests tests/WorkspaceTests.cpp)
target_include_directories(WorkspaceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(WorkspaceTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(WorkspaceTests PRIVATE /utf-8)
endif()
target_link_libraries(WorkspaceTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME WorkspaceTests COMMAND WorkspaceTests)

add_executable(WorkspaceNavigationTests tests/WorkspaceNavigationTests.cpp)
target_include_directories(WorkspaceNavigationTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(WorkspaceNavigationTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(WorkspaceNavigationTests PRIVATE /utf-8)
endif()
target_link_libraries(WorkspaceNavigationTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME WorkspaceNavigationTests COMMAND WorkspaceNavigationTests)

add_executable(BlockEditorTests tests/BlockEditorTests.cpp)
target_include_directories(BlockEditorTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(BlockEditorTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(BlockEditorTests PRIVATE /utf-8)
endif()
target_link_libraries(BlockEditorTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME BlockEditorTests COMMAND BlockEditorTests)

add_executable(GeneralTaskTests tests/GeneralTaskTests.cpp)
target_include_directories(GeneralTaskTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(GeneralTaskTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(GeneralTaskTests PRIVATE /utf-8)
endif()
target_link_libraries(GeneralTaskTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME GeneralTaskTests COMMAND GeneralTaskTests)


add_executable(TaskDomainTests tests/TaskDomainTests.cpp)
target_include_directories(TaskDomainTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(TaskDomainTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(TaskDomainTests PRIVATE /utf-8)
endif()
target_link_libraries(TaskDomainTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME TaskDomainTests COMMAND TaskDomainTests)

add_executable(WorkspaceTaskViewsTests tests/WorkspaceTaskViewsTests.cpp)
target_include_directories(WorkspaceTaskViewsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(WorkspaceTaskViewsTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(WorkspaceTaskViewsTests PRIVATE /utf-8)
endif()
target_link_libraries(WorkspaceTaskViewsTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME WorkspaceTaskViewsTests COMMAND WorkspaceTaskViewsTests)

juce_add_console_app(TaskTreeUiTests PRODUCT_NAME "TaskTreeUiTests")
juce_generate_juce_header(TaskTreeUiTests)
target_sources(TaskTreeUiTests PRIVATE tests/TaskTreeUiTests.cpp src/TaskTree.cpp src/BlockEditor.cpp)
target_include_directories(TaskTreeUiTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(TaskTreeUiTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
if(MSVC)
    target_compile_options(TaskTreeUiTests PRIVATE /utf-8)
endif()
target_link_libraries(TaskTreeUiTests PRIVATE juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME TaskTreeUiTests COMMAND TaskTreeUiTests)

add_executable(WorkspaceTemplateTests tests/WorkspaceTemplateTests.cpp)
target_include_directories(WorkspaceTemplateTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(WorkspaceTemplateTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(WorkspaceTemplateTests PRIVATE /utf-8)
endif()
target_link_libraries(WorkspaceTemplateTests PRIVATE juce::juce_core
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME WorkspaceTemplateTests COMMAND WorkspaceTemplateTests)
