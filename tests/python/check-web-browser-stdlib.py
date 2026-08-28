#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
checks = {
    'url module is ordered': 'std/url.rz' in (ROOT/'library/web/source-order.txt').read_text(),
    'url module is public': 'public import web::std::url;' in (ROOT/'library/web/src/lib.rz').read_text(),
    'query parameters supported': 'public fn query(string name)' in (ROOT/'library/web/std/url.rz').read_text(),
    'url component encoding supported': 'public fn encode_component' in (ROOT/'library/web/std/url.rz').read_text(),
    'location parts supported': all(x in (ROOT/'library/web/std/location.rz').read_text() for x in ['public fn origin()', 'public fn pathname()', 'public fn search()', 'public fn hash()']),
    'session storage supported': 'public fn session_set' in (ROOT/'library/web/std/storage.rz').read_text() and 'public fn session_get' in (ROOT/'library/web/std/storage.rz').read_text(),
    'interval timers supported': 'public fn set_interval' in (ROOT/'library/web/std/timers.rz').read_text() and 'public fn clear_interval' in (ROOT/'library/web/std/timers.rz').read_text(),
    'DOM value reads supported': 'public fn value(string id)' in (ROOT/'library/web/std/dom.rz').read_text(),
    'DOM text reads supported': 'public fn text(string id)' in (ROOT/'library/web/std/dom.rz').read_text(),
    'DOM attribute reads supported': 'public fn attribute(string id, string name)' in (ROOT/'library/web/std/dom.rz').read_text(),
    'component host implements browser ABI': all(x in (ROOT/'compiler/src/raz_codegen_web/src/web/codegen.rz').read_text() for x in ['dom_value_length:', 'session_storage_set:', 'location_origin_length:', 'url_query_value_length:', 'timer_set_interval:']),
    'static-page host implements browser ABI': all(x in (ROOT/'library/web/src/lib.rz').read_text() for x in ['dom_value_length:', 'session_storage_set:', 'location_origin_length:', 'url_query_value_length:', 'timer_set_interval:']),
}
failed=[k for k,v in checks.items() if not v]
for k,v in checks.items(): print(('PASS' if v else 'FAIL') + ': ' + k)
print(f'{len(checks)-len(failed)}/{len(checks)} checks passed')
sys.exit(1 if failed else 0)
