#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

root = Path(__file__).resolve().parents[2]
ui = (root / 'library/web/ui/ui.rz').read_text()
fetch = (root / 'library/web/std/fetch.rz').read_text()
encoding = (root / 'library/web/std/encoding.rz').read_text()
codegen = (root / 'compiler/src/raz_codegen_web/src/web/codegen.rz').read_text()
lib = (root / 'library/web/src/lib.rz').read_text()
source_order = (root / 'library/web/source-order.txt').read_text()

checks = [
    ('fetch module exported', 'public import web::std::fetch;' in lib),
    ('encoding module exported', 'public import web::std::encoding;' in lib),
    ('fetch in source order', 'std/fetch.rz' in source_order),
    ('GET facade', 'public fn get(string url)' in fetch),
    ('POST text facade', 'public fn post_text(string url, string body)' in fetch),
    ('PUT text facade', 'public fn put_text(string url, string body)' in fetch),
    ('PATCH text facade', 'public fn patch_text(string url, string body)' in fetch),
    ('DELETE facade', 'public fn delete(string url)' in fetch),
    ('response text helper', 'public fn text(web::ui::HttpRequest& response)' in fetch),
    ('resource facade', 'public fn resource<T>' in fetch),
    ('fetch error classification', 'public enum ErrorKind' in fetch and 'Network' in fetch and 'Server' in fetch),
    ('transport verbs', 'WEB_HTTP_PUT = 3' in ui and 'WEB_HTTP_PATCH = 4' in ui and 'WEB_HTTP_DELETE = 5' in ui),
    ('browser host maps verbs', "'GET', 'POST', 'PUT', 'PATCH', 'DELETE'" in codegen),
    ('UTF-8 helper', 'public fn utf8_length(string value)' in encoding),
    ('URI encoding helpers', 'uri_component' in encoding and 'decode_uri_component' in encoding),
]

failed = [name for name, ok in checks if not ok]
for name, ok in checks:
    print(('PASS' if ok else 'FAIL') + ': ' + name)
print(f'{len(checks)-len(failed)}/{len(checks)} checks passed')
raise SystemExit(1 if failed else 0)
