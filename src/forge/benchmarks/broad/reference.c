// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <stdint.h>

__attribute__((noinline)) uint64_t llvm_int_mix(uint64_t seed, int64_t rounds) {
  uint64_t x = seed;
  for (int64_t i = 0; i < rounds; ++i) {
    x = x * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    x ^= x >> 13;
    x ^= x << 7;
  }
  return x;
}
__attribute__((noinline)) int64_t llvm_fib(int64_t n) {
  int64_t a=0,b=1;
  for (int64_t i=0;i<n;++i) { int64_t s=a+b; a=b; b=s; }
  return a;
}
__attribute__((noinline)) uint64_t llvm_branch_walk(uint64_t seed, int64_t rounds) {
  uint64_t x=seed;
  for (int64_t i=0;i<rounds;++i) x=(x&1)?x*3+7:(x>>1)^5;
  return x;
}
__attribute__((noinline)) int64_t llvm_reg_pressure(int64_t a,int64_t b,int64_t c,int64_t d,int64_t e,int64_t f,int64_t g,int64_t h) {
  int64_t ab=a+b, cd=c+d, ef=e+f, gh=g+h;
  int64_t x=(ab*cd)^(ef*gh);
  return x+ab+ef;
}
__attribute__((noinline)) double llvm_float_poly(double x, int64_t rounds) {
  for (int64_t i=0;i<rounds;++i) x=x*1.0000001192092896+0.00000095367431640625;
  return x;
}
__attribute__((noinline)) int64_t llvm_memory4(const int64_t *p) { return p[0]+p[1]+p[2]+p[3]; }

__attribute__((noinline)) int64_t llvm_call_leaf(int64_t x,int64_t y,int64_t z) { return ((x+y)*z)^7; }
__attribute__((noinline)) int64_t llvm_call_chain(int64_t x) {
  int64_t a=llvm_call_leaf(x,2,3); int64_t b=llvm_call_leaf(a,3,2);
  int64_t c=llvm_call_leaf(b,2,3); return llvm_call_leaf(c,3,2);
}
__attribute__((noinline)) double llvm_float_leaf(double x,double y) { return x*y+0.125; }
__attribute__((noinline)) double llvm_float_calls(double x) {
  double r0=llvm_float_leaf(x,1.5); double r1=llvm_float_leaf(r0,0.75); return llvm_float_leaf(r1,1.5);
}
__attribute__((noinline)) int64_t llvm_memory_sum(const int64_t *p) { return p[0]+p[1]+p[2]+p[3]+p[4]+p[5]+p[6]+p[7]; }
__attribute__((noinline)) int64_t llvm_memory_update4(int64_t *p, int64_t delta) {
  p[0] += delta; p[1] += delta; p[2] += delta; p[3] += delta;
  return p[0] + p[1] + p[2] + p[3];
}
__attribute__((noinline)) int64_t llvm_call_live(int64_t x) {
  int64_t called = llvm_call_leaf(x, 2, 3);
  return called + x;
}
__attribute__((noinline)) int64_t llvm_multi_recurrence(int64_t n) {
  int64_t a=1,b=2,c=3,d=1;
  for (int64_t i=0;i<n;++i) { int64_t next=(a+b)^(c+d); a=b; b=c; c=d; d=next; }
  return a+b+c+d;
}
__attribute__((noinline)) uint64_t llvm_branch_merge(uint64_t seed, int64_t rounds) {
  uint64_t value=seed;
  for (int64_t i=0;i<rounds;++i) value=(value&8)?value+5:value^3;
  return value;
}
__attribute__((noinline)) int64_t llvm_store_overwrite(int64_t *p, int64_t value) {
  *p = value;
  *p = value + 1;
  return *p;
}
__attribute__((noinline)) int64_t llvm_global_store_overwrite(int64_t *p, int64_t value) {
  *p = value;
  goto overwrite;
overwrite:
  *p = value + 1;
  goto exit;
exit:
  return *p;
}
__attribute__((noinline)) double llvm_float_dot4(const double *a,const double *b) {
  return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];
}
