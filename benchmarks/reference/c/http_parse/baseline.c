// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

typedef struct { const unsigned char *p; size_t n; } slice;
typedef struct { slice method,target,version,headers,body; long long content_length; int chunked, close; } reqview;
static inline uint64_t ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec; }
static inline int ieq(slice s,const char *lit){ size_t n=strlen(lit); if(s.n!=n)return 0; for(size_t i=0;i<n;i++) if((s.p[i]|32)!=(unsigned char)(lit[i]|32)) return 0; return 1; }
static inline int token(unsigned char c){ return c>32 && c<127 && !strchr("()<>@,;:\\\"/[]?={} \t",c); }
__attribute__((noinline)) static int parse(const unsigned char *d,size_t n,reqview *o){
 const unsigned char *end=d+n,*p=d,*e;
 e=(const unsigned char*)memchr(p,'\r',n); if(!e||e+1>=end||e[1]!='\n') return 0;
 const unsigned char *s1=(const unsigned char*)memchr(p,' ',(size_t)(e-p)); if(!s1||s1==p) return 0;
 const unsigned char *s2=(const unsigned char*)memchr(s1+1,' ',(size_t)(e-(s1+1))); if(!s2||s2==s1+1||s2+1>=e) return 0;
 o->method=(slice){p,(size_t)(s1-p)}; o->target=(slice){s1+1,(size_t)(s2-s1-1)}; o->version=(slice){s2+1,(size_t)(e-s2-1)};
 for(size_t i=0;i<o->method.n;i++) if(!token(o->method.p[i])) return 0;
 if(!((o->version.n==8 && !memcmp(o->version.p,"HTTP/1.1",8))||(o->version.n==8 && !memcmp(o->version.p,"HTTP/1.0",8)))) return 0;
 for(size_t i=0;i<o->target.n;i++) if(o->target.p[i]<=32||o->target.p[i]==127) return 0;
 const unsigned char *hs=e+2,*he=NULL,*q=hs;
 while(q+3<end){ const unsigned char *r=(const unsigned char*)memchr(q,'\r',(size_t)(end-q)); if(!r)break; if(r+3<end&&r[1]=='\n'&&r[2]=='\r'&&r[3]=='\n'){he=r;break;} q=r+1; }
 if(!he)return 0;
 o->headers=(slice){hs,(size_t)(he-hs+2)}; o->content_length=-1; o->chunked=0; o->close=0;
 q=hs;
 while(q<he){ const unsigned char *le=(const unsigned char*)memchr(q,'\r',(size_t)(he+2-q)); if(!le||le+1>=end||le[1]!='\n') return 0; const unsigned char *colon=(const unsigned char*)memchr(q,':',(size_t)(le-q)); if(!colon)return 0; slice name={q,(size_t)(colon-q)}; const unsigned char *v=colon+1; while(v<le&&(*v==' '||*v=='\t'))v++; const unsigned char *ve=le; while(ve>v&&(ve[-1]==' '||ve[-1]=='\t'))ve--; slice val={v,(size_t)(ve-v)};
   if(ieq(name,"Content-Length")){ long long x=0; if(!val.n)return 0; for(size_t i=0;i<val.n;i++){ if(val.p[i]<'0'||val.p[i]>'9')return 0; x=x*10+(val.p[i]-'0'); } o->content_length=x; }
   else if(ieq(name,"Connection")){ if(ieq(val,"close"))o->close=1; }
   else if(ieq(name,"Transfer-Encoding")){ if(ieq(val,"chunked"))o->chunked=1; }
   q=le+2;
 }
 const unsigned char *bs=he+4; size_t avail=(size_t)(end-bs); size_t bl=avail; if(!o->chunked&&o->content_length>=0&&bl>(size_t)o->content_length)bl=(size_t)o->content_length; if(!o->chunked&&o->content_length>=0&&avail<(size_t)o->content_length)return 0; o->body=(slice){bs,bl}; return 1;
}
int main(void){ static const unsigned char r[]="POST /api/v1/items?id=42 HTTP/1.1\r\nHost: example.com\r\nContent-Length: 16\r\nConnection: keep-alive\r\nX-Test: abcdefghijklmnop\r\n\r\n0123456789abcdef"; reqview v; long long sum=0; const int N=200000; uint64_t a=ns(); for(int i=0;i<N;i++){ if(!parse(r,sizeof(r)-1,&v)) return 2; sum+=(long long)v.method.n+(long long)v.target.n+(long long)v.body.n+v.content_length; } uint64_t b=ns(); printf("%llu %lld\n",(unsigned long long)(b-a),sum); return 0; }
