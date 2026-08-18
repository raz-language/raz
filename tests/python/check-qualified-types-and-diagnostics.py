#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
parser = (ROOT / 'compiler/src/frontend/parser.rz').read_text(encoding='utf-8')
project = (ROOT / 'compiler/src/driver/project.rz').read_text(encoding='utf-8')
statements = (ROOT / 'compiler/src/hir/semantic/statements.rz').read_text(encoding='utf-8')
declarations = (ROOT / 'compiler/src/hir/semantic/declarations.rz').read_text(encoding='utf-8')

checks = {
    'frontend consumes qualified type separators': 'while (parser.current.kind == TokenKind::ColonColon)' in parser,
    'qualified type requires identifier segments': 'parser_fail(parser, DiagnosticCode::ExpectedIdentifier);' in parser,
    'qualified type keeps contiguous source span': '*type_length = finish - start;' in parser,
    'qualified type enters complex source mode': 'if (qualified) {' in parser and 'parser.module.complex_source_mode = 1;' in parser,
    'compact local parser delegates full type spelling': '!parse_type_reference(' in parser.split('fn parse_local', 1)[1].split('fn ', 1)[0],
    'HIR declaration lookahead walks qualified type path': 'while (builder.current.kind == TokenKind::ColonColon)' in statements.split('bool declaration = false;', 1)[1].split('if (declaration)', 1)[0],
    'HIR qualified local probe reaches following identifier': 'A::B::Type' in statements and 'declaration = builder.current.kind == TokenKind::Identifier;' in statements,
    'normal function declaration lookahead walks qualified type path': 'while (builder.current.kind == TokenKind::ColonColon)' in declarations.split('bool declaration = false;', 1)[1].split('if (declaration)', 1)[0],
    'normal function qualified local probe reaches following identifier': 'A::B::Type' in declarations and 'declaration = builder.current.kind == TokenKind::Identifier;' in declarations,
    'legacy stage1 diagnostic filename removed': 'stage1-diagnostic.txt' not in project.replace('Legacy stage1-diagnostic.txt emission is intentionally disabled.', ''),
    'legacy diagnostic emitter performs no file write': 'raz_rt_write_ascii_i64' not in project.split('fn write_compiler_diagnostic',1)[1].split('fn path_is_check_option',1)[0],
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'FAIL: {name}')
    raise SystemExit(1)
print(f'qualified-types/diagnostic-artifact: PASS ({len(checks)} checks)')
