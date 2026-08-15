# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED PYTHON_EXE OR NOT DEFINED FORMATTER OR NOT DEFINED RAZC_EXE OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "PYTHON_EXE, FORMATTER, RAZC_EXE, and WORK_ROOT are required")
endif()

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}")
set(source "${WORK_ROOT}/scalar-parity.rz")
file(WRITE "${source}" [=[/* outer block
   /* nested block */
   still outer */
enum FormatState {
    Ready,
    Done,
}

public fn main() -> i64 {
    i64 value = 0x2A;
    value <<= 2;
    value >>= 1;
    i64 masks = 0b1010 | 0o7 ^ 1_000 & 15;
    f64 decimal = 1.5;
    f64 scientific = 1.25e2;
    char newline = '\n';
    for item in 1..=3 {
        value += item;
    }
    FormatState state = FormatState::Ready;
    match state {
        FormatState::Ready => { value += 1; },
        FormatState::Done => { value += 2; },
    }
    if (decimal < scientific && newline == 10) {
        return value + masks;
    }
    return 0;
}
]=])

execute_process(
  COMMAND "${PYTHON_EXE}" "${FORMATTER}" "${source}"
  RESULT_VARIABLE format_result
  OUTPUT_VARIABLE format_output
  ERROR_VARIABLE format_error)
if(NOT format_result EQUAL 0)
  message(FATAL_ERROR "formatter failed scalar parity fixture:\n${format_output}\n${format_error}")
endif()

file(READ "${source}" formatted)
foreach(required IN ITEMS "/* nested block */" "0x2A" "0b1010" "0o7" "1_000" "<<=" ">>=" "|" "1.5" "1.25e2" "char newline" "1..=3" "FormatState::Ready" "=>")
  string(FIND "${formatted}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "formatter lost scalar token '${required}':\n${formatted}")
  endif()
endforeach()
if(formatted MATCHES "<[ \t]+<[ \t]*=" OR formatted MATCHES ">[ \t]+>[ \t]*=")
  message(FATAL_ERROR "formatter split a shift-assignment token:\n${formatted}")
endif()

execute_process(
  COMMAND "${PYTHON_EXE}" "${FORMATTER}" --check "${source}"
  RESULT_VARIABLE idempotent_result
  OUTPUT_VARIABLE idempotent_output
  ERROR_VARIABLE idempotent_error)
if(NOT idempotent_result EQUAL 0)
  message(FATAL_ERROR "formatter is not idempotent on scalar parity fixture:\n${idempotent_output}\n${idempotent_error}")
endif()

execute_process(
  COMMAND "${RAZC_EXE}" --check "${source}"
  RESULT_VARIABLE check_result
  OUTPUT_VARIABLE check_output
  ERROR_VARIABLE check_error)
if(NOT check_result EQUAL 0)
  message(FATAL_ERROR "formatted scalar parity fixture is not valid Raz:\n${check_output}\n${check_error}\n${formatted}")
endif()
