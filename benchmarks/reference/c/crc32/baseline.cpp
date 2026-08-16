// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
constexpr std::array<uint32_t,256> make_table(){ std::array<uint32_t,256> t{}; for(uint32_t i=0;i<256;i++){uint32_t v=i; for(int b=0;b<8;b++) v=(v>>1)^((v&1)?0xedb88320U:0U); t[i]=v;} return t; }
constexpr auto table=make_table();
__attribute__((noinline)) uint32_t crc(uint32_t state,const uint8_t* p,int64_t n){int64_t i=0;while(i+8<=n){for(int j=0;j<8;j++)state=(state>>8)^table[(state^p[i+j])&255];i+=8;}while(i<n){state=(state>>8)^table[(state^p[i])&255];i++;}return state;}
static uint64_t ns(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000000ull+t.tv_nsec;}
int main(){const size_t n=67108864;auto*p=(uint8_t*)malloc(n);memset(p,0,n);uint64_t a=ns();uint32_t v=0;v=crc(0xffffffffU,p,n)^0xffffffffU;uint64_t b=ns();printf("%llu %u\n",(unsigned long long)(b-a),v);free(p);}
