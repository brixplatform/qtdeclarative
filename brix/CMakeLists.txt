# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.25)
project(BrixQmlls VERSION 6.12.0 LANGUAGES CXX)

# A focused rebuild against the matching Qt SDK. The installed QmlLS archive
# supplies the unchanged server modules and their moc code; these complete
# translation units replace its completion engine and request handler. No
# private class layout or exported ABI is changed by this patch set.
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core QmlLSPrivate)
get_filename_component(qt_source "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
add_library(qmlls_completion OBJECT
    "${qt_source}/src/qmlls/qqmllscompletion.cpp"
    "${qt_source}/src/qmlls/qqmllsvaluecompletion.cpp"
    "${qt_source}/src/qmlls/qqmllsutils.cpp"
    "${qt_source}/src/qmlls/qqmlhover.cpp"
    "${qt_source}/src/qmlls/qqmllstypedocumentation.cpp"
    "${qt_source}/src/qmlls/qqmlcompletionsupport.cpp")
target_compile_features(qmlls_completion PRIVATE cxx_std_20)
target_compile_definitions(qmlls_completion PRIVATE QT_BUILDING_QT)
target_link_libraries(qmlls_completion PUBLIC Qt6::QmlLSPrivate)
add_executable(qmlls "${qt_source}/tools/qmlls/qmllanguageservertool.cpp")
target_link_libraries(qmlls PRIVATE qmlls_completion)
install(TARGETS qmlls RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_package(Qt6 REQUIRED COMPONENTS Test QuickTestUtilsPrivate)
    add_executable(tst_qmlls_utils
        "${qt_source}/tests/auto/qmlls/utils/tst_qmlls_utils.cpp"
        "${qt_source}/tests/auto/qmlls/utils/tst_qmlls_utils.h")
    set_target_properties(tst_qmlls_utils PROPERTIES AUTOMOC ON)
    target_compile_definitions(tst_qmlls_utils PRIVATE QT_BUILDING_QT
        QT_QMLLS_UTILS_DATADIR="${qt_source}/tests/auto/qmlls/utils/data"
        QT_QMLLS_BIG_FOLDER="${qt_source}")
    target_link_libraries(tst_qmlls_utils PRIVATE qmlls_completion Qt6::Test Qt6::QuickTestUtilsPrivate)
    add_test(NAME upstream-completions COMMAND tst_qmlls_utils completions)
    set_tests_properties(upstream-completions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME lsp-values COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/completions.py" "$<TARGET_FILE:qmlls>" --test)
    set_tests_properties(lsp-values PROPERTIES TIMEOUT 60)
endif()
