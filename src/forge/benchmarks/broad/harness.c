// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint64_t (*u64_2_fn)(uint64_t,int64_t);
typedef int64_t (*i64_1_fn)(int64_t);
typedef int64_t (*i64_8_fn)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
typedef double (*f64_2_fn)(double,int64_t);
typedef int64_t (*mem_fn)(const int64_t*);
typedef int64_t (*call_fn)(int64_t);
typedef double (*float_call_fn)(double);
typedef int64_t (*mem_sum_fn)(const int64_t*);
typedef int64_t (*mem_update_fn)(int64_t*,int64_t);
typedef double (*dot4_fn)(const double*,const double*);
extern uint64_t forge_int_mix(uint64_t,int64_t), llvm_int_mix(uint64_t,int64_t);
extern int64_t forge_fib(int64_t), llvm_fib(int64_t);
extern uint64_t forge_branch_walk(uint64_t,int64_t), llvm_branch_walk(uint64_t,int64_t);
extern int64_t forge_reg_pressure(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
extern int64_t llvm_reg_pressure(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
extern double forge_float_poly(double,int64_t), llvm_float_poly(double,int64_t);
extern int64_t forge_memory4(const int64_t*), llvm_memory4(const int64_t*);
extern int64_t forge_call_chain(int64_t), llvm_call_chain(int64_t);
extern double forge_float_calls(double), llvm_float_calls(double);
extern int64_t forge_memory_sum(const int64_t*), llvm_memory_sum(const int64_t*);
extern int64_t forge_memory_update4(int64_t*,int64_t), llvm_memory_update4(int64_t*,int64_t);
extern int64_t forge_call_live(int64_t), llvm_call_live(int64_t);
extern int64_t forge_multi_recurrence(int64_t), llvm_multi_recurrence(int64_t);
extern uint64_t forge_branch_merge(uint64_t,int64_t), llvm_branch_merge(uint64_t,int64_t);
extern double forge_float_dot4(const double*,const double*), llvm_float_dot4(const double*,const double*);
extern int64_t forge_store_overwrite(int64_t*,int64_t), llvm_store_overwrite(int64_t*,int64_t);
extern int64_t forge_global_store_overwrite(int64_t*,int64_t), llvm_global_store_overwrite(int64_t*,int64_t);
static volatile uint64_t sink_u64; static volatile double sink_f64;
static uint64_t ns(void){struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static double bench_u64_2(u64_2_fn f,uint64_t x,int64_t n,int reps){uint64_t t=ns(),s=0;for(int i=0;i<reps;i++)s^=f(x+(uint64_t)i,n);sink_u64=s;return(double)(ns()-t)/reps;}
static double bench_i64_1(i64_1_fn f,int64_t n,int reps){uint64_t t=ns();int64_t s=0;for(int i=0;i<reps;i++)s^=f(n+(i&1));sink_u64=(uint64_t)s;return(double)(ns()-t)/reps;}
static double bench_i64_8(i64_8_fn f,int reps){uint64_t t=ns();int64_t s=0;for(int i=0;i<reps;i++)s^=f(i+1,2,3,4,5,6,7,8);sink_u64=(uint64_t)s;return(double)(ns()-t)/reps;}
static double bench_f64(f64_2_fn f,int64_t n,int reps){uint64_t t=ns();double s=0;for(int i=0;i<reps;i++)s+=f(1.25+(double)(i&7)*0.01,n);sink_f64=s;return(double)(ns()-t)/reps;}
static double bench_call(call_fn f,int reps){uint64_t t=ns();int64_t s=0;for(int i=0;i<reps;i++)s^=f(i+1);sink_u64=(uint64_t)s;return(double)(ns()-t)/reps;}
static double bench_float_call(float_call_fn f,int reps){uint64_t t=ns();double s=0;for(int i=0;i<reps;i++)s+=f(1.0+(double)(i&7)*0.01);sink_f64=s;return(double)(ns()-t)/reps;}
static double bench_mem_sum(mem_sum_fn f,int reps){int64_t p[8];for(int i=0;i<8;i++)p[i]=i+1;uint64_t t=ns();int64_t s=0;for(int i=0;i<reps;i++){p[i&7]^=i;s^=f(p);}sink_u64=(uint64_t)s;return(double)(ns()-t)/reps;}
static double bench_mem_update(mem_update_fn f,int reps){int64_t p[4]={1,2,3,4};uint64_t t=ns();int64_t s=0;for(int i=0;i<reps;i++)s^=f(p,(i&3)-1);sink_u64=(uint64_t)s;return(double)(ns()-t)/reps;}
static double bench_dot4(dot4_fn f,int reps){double a[4]={1.0,2.0,3.0,4.0},b[4]={0.5,1.5,2.5,3.5};uint64_t t=ns();double s=0;for(int i=0;i<reps;i++){a[i&3]+=(double)(i&1)*0.000001;s+=f(a,b);}sink_f64=s;return(double)(ns()-t)/reps;}
static double bench_store_overwrite(mem_update_fn f,int reps){int64_t p=0;uint64_t t=ns();int64_t s=0;for(int i=0;i<reps;i++)s^=f(&p,i);sink_u64=(uint64_t)(s^p);return(double)(ns()-t)/reps;}
static double bench_mem(mem_fn f,int reps){int64_t p[4]={1,2,3,4};uint64_t t=ns();int64_t s=0;for(int i=0;i<reps;i++){p[0]=i;s^=f(p);}sink_u64=(uint64_t)s;return(double)(ns()-t)/reps;}
static void row(const char*n,double f,double l){printf("%-18s %10.3f %10.3f %8.3f\n",n,f,l,f/l);} 
int main(void){
 uint64_t fi=forge_int_mix(7,31), li=llvm_int_mix(7,31); int64_t ff=forge_fib(50), lf=llvm_fib(50); uint64_t fb=forge_branch_walk(17,40), lb=llvm_branch_walk(17,40); int64_t fr=forge_reg_pressure(1,2,3,4,5,6,7,8), lr=llvm_reg_pressure(1,2,3,4,5,6,7,8); double fp=forge_float_poly(1.25,100), lp=llvm_float_poly(1.25,100); if(fi!=li||ff!=lf||fb!=lb||fr!=lr||fabs(fp-lp)>1e-9){fprintf(stderr,"mismatch int=%" PRIu64 "/%" PRIu64 " fib=%" PRId64 "/%" PRId64 " branch=%" PRIu64 "/%" PRIu64 " reg=%" PRId64 "/%" PRId64 " float=%.17g/%.17g\n",fi,li,ff,lf,fb,lb,fr,lr,fp,lp);return 2;} int64_t p[4]={1,2,3,4};if(forge_memory4(p)!=llvm_memory4(p)){fprintf(stderr,"memory mismatch\n");return 3;} if(forge_call_chain(11)!=llvm_call_chain(11)){fprintf(stderr,"call mismatch\n");return 4;} if(fabs(forge_float_calls(1.25)-llvm_float_calls(1.25))>1e-12){fprintf(stderr,"float call mismatch\n");return 5;} int64_t q[8];for(int i=0;i<8;i++)q[i]=i+1;if(forge_memory_sum(q)!=llvm_memory_sum(q)){fprintf(stderr,"memory sum mismatch\n");return 6;} int64_t u1[4]={1,2,3,4},u2[4]={1,2,3,4}; if(forge_memory_update4(u1,3)!=llvm_memory_update4(u2,3)||memcmp(u1,u2,sizeof(u1))!=0){fprintf(stderr,"memory update mismatch\n");return 7;} if(forge_call_live(11)!=llvm_call_live(11)){fprintf(stderr,"call live mismatch\n");return 8;} if(forge_multi_recurrence(40)!=llvm_multi_recurrence(40)){fprintf(stderr,"multi recurrence mismatch\n");return 9;} if(forge_branch_merge(17,40)!=llvm_branch_merge(17,40)){fprintf(stderr,"branch merge mismatch\n");return 10;} double da[4]={1,2,3,4},db[4]={0.5,1.5,2.5,3.5}; if(fabs(forge_float_dot4(da,db)-llvm_float_dot4(da,db))>1e-12){fprintf(stderr,"dot4 mismatch\n");return 11;} int64_t so1=0,so2=0; if(forge_store_overwrite(&so1,41)!=llvm_store_overwrite(&so2,41)||so1!=so2){fprintf(stderr,"store overwrite mismatch\n");return 12;} int64_t gso1=0,gso2=0; if(forge_global_store_overwrite(&gso1,41)!=llvm_global_store_overwrite(&gso2,41)||gso1!=gso2){fprintf(stderr,"global store overwrite mismatch\n");return 13;}
 printf("kernel              forge_ns    llvm_ns    ratio\n");
 row("int_mix_1000",bench_u64_2(forge_int_mix,7,1000,50000),bench_u64_2(llvm_int_mix,7,1000,50000));
 row("fib_100",bench_i64_1(forge_fib,100,1000000),bench_i64_1(llvm_fib,100,1000000));
 row("branch_walk_200",bench_u64_2(forge_branch_walk,17,200,200000),bench_u64_2(llvm_branch_walk,17,200,200000));
 row("reg_pressure",bench_i64_8(forge_reg_pressure,3000000),bench_i64_8(llvm_reg_pressure,3000000));
 row("float_poly_200",bench_f64(forge_float_poly,200,200000),bench_f64(llvm_float_poly,200,200000));
 row("memory4",bench_mem(forge_memory4,5000000),bench_mem(llvm_memory4,5000000));
 row("call_chain",bench_call(forge_call_chain,1500000),bench_call(llvm_call_chain,1500000));
 row("float_calls",bench_float_call(forge_float_calls,1500000),bench_float_call(llvm_float_calls,1500000));
 row("memory_sum_8",bench_mem_sum(forge_memory_sum,150000),bench_mem_sum(llvm_memory_sum,150000));
 row("memory_update4",bench_mem_update(forge_memory_update4,1000000),bench_mem_update(llvm_memory_update4,1000000));
 row("call_live",bench_call(forge_call_live,1500000),bench_call(llvm_call_live,1500000));
 row("multi_recur_100",bench_i64_1(forge_multi_recurrence,100,500000),bench_i64_1(llvm_multi_recurrence,100,500000));
 row("branch_merge_200",bench_u64_2(forge_branch_merge,17,200,200000),bench_u64_2(llvm_branch_merge,17,200,200000));
 row("float_dot4",bench_dot4(forge_float_dot4,2000000),bench_dot4(llvm_float_dot4,2000000));
 row("store_overwrite",bench_store_overwrite(forge_store_overwrite,3000000),bench_store_overwrite(llvm_store_overwrite,3000000));
 row("global_store_overwrite",bench_store_overwrite(forge_global_store_overwrite,3000000),bench_store_overwrite(llvm_global_store_overwrite,3000000));
 return 0;
}
