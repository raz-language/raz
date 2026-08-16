# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from dataclasses import dataclass
from random import Random

ALIGN = 16
HEADER = 12
MIN_SPLIT = 24
HEAP_BASE = 1024


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


@dataclass
class Block:
    start: int
    size: int
    user: int


class Heap:
    def __init__(self):
        self.bump = HEAP_BASE
        self.free: list[tuple[int, int]] = []
        self.live: dict[int, Block] = {}

    def _insert_free(self, start: int, size: int):
        items = self.free + [(start, size)]
        items.sort()
        merged = []
        for s, n in items:
            assert n >= MIN_SPLIT
            if merged and merged[-1][0] + merged[-1][1] == s:
                ps, pn = merged[-1]
                merged[-1] = (ps, pn + n)
            else:
                if merged:
                    assert merged[-1][0] + merged[-1][1] < s
                merged.append((s, n))
        while merged and merged[-1][0] + merged[-1][1] == self.bump:
            self.bump = merged[-1][0]
            merged.pop()
        self.free = merged

    def alloc(self, size: int, alignment: int = ALIGN) -> int:
        if size <= 0 or alignment <= 0 or alignment > 4096 or alignment & (alignment - 1):
            return 0
        alignment = max(alignment, ALIGN)
        for i, (start, total) in enumerate(self.free):
            user = align_up(start + HEADER, alignment)
            need = align_up(user - start + size, 8)
            if need <= total:
                rem = total - need
                if rem >= MIN_SPLIT:
                    self.free[i] = (start + need, rem)
                    total = need
                else:
                    self.free.pop(i)
                self.live[user] = Block(start, total, user)
                return user
        start = self.bump
        user = align_up(start + HEADER, alignment)
        total = align_up(user - start + size, 8)
        self.bump += total
        self.live[user] = Block(start, total, user)
        return user

    def free_ptr(self, user: int):
        if user == 0:
            return
        block = self.live.pop(user)
        self._insert_free(block.start, block.size)

    def realloc(self, user: int, size: int) -> int:
        if user == 0:
            return self.alloc(size)
        if size <= 0:
            self.free_ptr(user)
            return 0
        b = self.live[user]
        off = b.user - b.start
        new_total = align_up(off + size, 8)
        if new_total <= b.size:
            rem = b.size - new_total
            if rem >= MIN_SPLIT:
                old_end = b.start + b.size
                b.size = new_total
                if old_end == self.bump:
                    self.bump = b.start + new_total
                else:
                    self._insert_free(b.start + new_total, rem)
            return user
        old_end = b.start + b.size
        if old_end == self.bump:
            b.size = new_total
            self.bump = b.start + new_total
            return user
        for i, (s, n) in enumerate(self.free):
            if s == old_end and b.size + n >= new_total:
                rem = b.size + n - new_total
                if rem >= MIN_SPLIT:
                    self.free[i] = (b.start + new_total, rem)
                    b.size = new_total
                else:
                    self.free.pop(i)
                    b.size += n
                return user
        replacement = self.alloc(size)
        self.free_ptr(user)
        return replacement

    def validate(self):
        last_end = 0
        for s, n in self.free:
            assert s >= HEAP_BASE and n >= MIN_SPLIT
            assert s >= last_end
            assert s + n <= self.bump
            last_end = s + n
        live_ranges = sorted((b.start, b.start + b.size) for b in self.live.values())
        free_ranges = [(s, s + n) for s, n in self.free]
        all_ranges = sorted((a, b, 'live') for a, b in live_ranges) + []
        all_ranges += [(a, b, 'free') for a, b in free_ranges]
        all_ranges.sort()
        for (_, end, _), (next_start, _, _) in zip(all_ranges, all_ranges[1:]):
            assert end <= next_start


# Deterministic targeted behavior.
h = Heap()
a = h.alloc(64)
b = h.alloc(64)
c = h.alloc(64)
h.free_ptr(b)
a2 = h.realloc(a, 100)  # consumes/splits adjacent b in place
assert a2 == a
h.validate()
a3 = h.realloc(a, 24)   # returns tail to free list
assert a3 == a
h.validate()
h.free_ptr(c)
h.free_ptr(a)
assert h.bump == HEAP_BASE
assert h.free == []

# Deterministic stress over split/coalesce/in-place/fallback paths.
rng = Random(0x52415A)
h = Heap()
ptrs: list[int] = []
for _ in range(4000):
    action = rng.randrange(3)
    if not ptrs or action == 0:
        p = h.alloc(rng.randrange(1, 2048), 1 << rng.randrange(3, 7))
        assert p != 0
        ptrs.append(p)
    elif action == 1:
        i = rng.randrange(len(ptrs))
        p = ptrs[i]
        q = h.realloc(p, rng.randrange(1, 4096))
        assert q != 0
        ptrs[i] = q
    else:
        i = rng.randrange(len(ptrs))
        h.free_ptr(ptrs.pop(i))
    h.validate()
for p in list(ptrs):
    h.free_ptr(p)
h.validate()
assert h.bump == HEAP_BASE
assert not h.free and not h.live
print('wasm-allocator-model: PASS (split/coalesce + in-place shrink/grow + stress)')
