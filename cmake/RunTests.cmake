get_filename_component(BUSTED_DIR ${BUSTED_PRG} PATH)
set(ENV{PATH} "${BUSTED_DIR}:$ENV{PATH}")

set(ENV{VIMRUNTIME} ${WORKING_DIR}/runtime)

if(NVIM_PRG)
  set(ENV{NVIM_PROG} "${NVIM_PRG}")
endif()

if(DEFINED ENV{TEST_FILE})
  set(TEST_PATH "$ENV{TEST_FILE}")
else()
  set(TEST_PATH "${TEST_DIR}/${TEST_TYPE}")
endif()

if(DEFINED ENV{TEST_TAG})
  set(TEST_TAG "--tags=$ENV{TEST_TAG}")
endif()

execute_process(
  COMMAND ${BUSTED_PRG} ${TEST_TAG} -v -o ${BUSTED_OUTPUT_TYPE}
    --helper=${TEST_DIR}/${TEST_TYPE}/preload.lua
    --lpath=${BUILD_DIR}/?.lua ${TEST_PATH}
  WORKING_DIRECTORY ${WORKING_DIR}
  ERROR_VARIABLE err
  RESULT_VARIABLE res)

if(NOT res EQUAL 0)
  message(STATUS "Output to stderr:\n${err}")
  message(FATAL_ERROR "Running ${TEST_TYPE} tests failed with error: ${res}.")
endif()
