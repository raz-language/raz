# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Network/concurrency runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "network_concurrency_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import alloc::string;
import core::result;
import std::net;
import std::net::resolve;
import std::net::vectored;
import std::net::poll_set;
import std::thread::channel;
import std::thread::spsc;
import std::thread::mpmc;
import std::thread::cancellation;
import std::thread::future;

fn put(usize out, i64 index, i64 value) { raz_rt_store_u8(out + index, value); }

fn main() -> i64 {
    ChannelI64 channel = std::thread::channel::create(3);
    if (!std::thread::channel::valid(&channel) || std::thread::channel::capacity(&channel) != 4) { return 1; }
    if (!std::thread::channel::try_send(&mut channel, 11) || !std::thread::channel::try_send(&mut channel, 22)) { return 2; }
    ReceiveI64 first = std::thread::channel::try_receive(&mut channel);
    ReceiveI64 second = std::thread::channel::try_receive(&mut channel);
    if (!first.received || first.value != 11 || !second.received || second.value != 22) { return 3; }
    ReceiveI64 timed = std::thread::channel::receive_millis(&mut channel, 1);
    if (timed.received) { return 4; }
    std::thread::channel::close(&mut channel);
    if (!std::thread::channel::is_closed(&channel) || std::thread::channel::try_send(&mut channel, 33)) { return 5; }

    ChannelI64 cancellable = std::thread::channel::create(1);
    CancellationToken cancel_token = std::thread::cancellation::create();
    if (!std::thread::channel::try_send(&mut cancellable, 44)) { return 31; }
    std::thread::cancellation::request(&mut cancel_token);
    if (std::thread::channel::send_cancellable(&mut cancellable, 55, &cancel_token) != -1) { return 32; }
    ReceiveI64 preserved = std::thread::channel::receive(&mut cancellable);
    if (!preserved.received || preserved.value != 44) { return 33; }
    CancellableReceiveI64 cancelled_receive = std::thread::channel::receive_cancellable(&mut cancellable, &cancel_token);
    if (cancelled_receive.status != -1) { return 34; }

    SpscI64 fast = std::thread::spsc::create(3);
    if (!std::thread::spsc::valid(&fast) || std::thread::spsc::capacity(&fast) != 4) { return 35; }
    usize fast_input = raz_rt_alloc(40); usize fast_output = raz_rt_alloc(40);
    unsafe {
      *(fast_input as i64*mut) = 101; *((fast_input + 8)as i64*mut) = 102; *((fast_input + 16)as i64*mut) = 103;
      *((fast_input + 24)as i64*mut) = 104; *((fast_input + 32)as i64*mut) = 105;
    }
    if (std::thread::spsc::try_send_many(&mut fast, fast_input, 3) != 3) { return 36; }
    SpscReceiveI64 fast_first = std::thread::spsc::try_receive(&mut fast);
    SpscReceiveI64 fast_second = std::thread::spsc::try_receive(&mut fast);
    if (!fast_first.received || fast_first.value != 101 || !fast_second.received || fast_second.value != 102) { return 37; }
    if (std::thread::spsc::try_send_many(&mut fast, fast_input + 16, 3) != 3) { return 38; }
    if (std::thread::spsc::try_receive_many(&mut fast, fast_output, 4) != 4) { return 39; }
    unsafe {
      if (*(fast_output as i64*const) != 103 || *((fast_output + 8)as i64*const) != 103 ||
          *((fast_output + 16)as i64*const) != 104 || *((fast_output + 24)as i64*const) != 105) { return 40; }
    }
    std::thread::spsc::destroy(&mut fast); raz_rt_dealloc(fast_output); raz_rt_dealloc(fast_input);

    MpmcI64 shared = std::thread::mpmc::create(3);
    if (!std::thread::mpmc::valid(&shared) || std::thread::mpmc::capacity(&shared) != 4) { return 44; }
    if (!std::thread::mpmc::try_send(&mut shared, 201) || !std::thread::mpmc::try_send(&mut shared, 202) ||
        !std::thread::mpmc::try_send(&mut shared, 203) || !std::thread::mpmc::try_send(&mut shared, 204) ||
        std::thread::mpmc::try_send(&mut shared, 205)) { return 45; }
    MpmcReceiveI64 shared_first = std::thread::mpmc::try_receive(&mut shared);
    MpmcReceiveI64 shared_second = std::thread::mpmc::try_receive(&mut shared);
    if (!shared_first.received || shared_first.value != 201 || !shared_second.received || shared_second.value != 202) { return 46; }
    if (!std::thread::mpmc::try_send(&mut shared, 205) || !std::thread::mpmc::try_send(&mut shared, 206)) { return 47; }
    i64 shared_expected[4] = [203,204,205,206];
    i64 shared_index = 0;
    while (shared_index < 4) {
        MpmcReceiveI64 item = std::thread::mpmc::try_receive(&mut shared);
        if (!item.received || item.value != shared_expected[shared_index]) { return 48; }
        shared_index += 1;
    }
    if (!std::thread::mpmc::is_empty(&shared) || std::thread::mpmc::try_receive(&mut shared).received) { return 49; }
    std::thread::mpmc::destroy(&mut shared);

    FutureI64 completed = std::thread::future::create();
    FutureResultI64 pending = std::thread::future::wait_millis(&mut completed, 0);
    if (pending.state != 0) { return 6; }
    if (!std::thread::future::complete(&mut completed, 99)) { return 7; }
    FutureResultI64 value = std::thread::future::wait_millis(&mut completed, 5);
    if (value.state != 1 || value.value != 99 || std::thread::future::complete(&mut completed, 100)) { return 8; }
    FutureI64 cancelled = std::thread::future::create();
    if (!std::thread::future::cancel(&mut cancelled)) { return 9; }
    FutureResultI64 cancelled_value = std::thread::future::try_result(&mut cancelled);
    if (cancelled_value.state != -1) { return 10; }

    usize text = raz_rt_alloc(11);
    i64 bytes[11] = [50,48,48,49,58,100,98,56,58,58,49];
    i64 i = 0; while (i < 11) { put(text, i, bytes[i]); i += 1; }
    Result<IpAddress, ResolveError> parsed = std::net::resolve::parse(text, 11);
    match parsed {
      Result<IpAddress, ResolveError>::Error(_) => { return 11; }
      Result<IpAddress, ResolveError>::Ok(parsed_address) => {
        IpAddress address = parsed_address;
        String canonical = std::net::resolve::to_string(&address);
        if (!alloc::string::equals_bytes(&canonical, text, 11)) { return 12; }
        Result<IpEndpoint, ResolveError> ep = std::net::resolve::endpoint(address, 443);
        match ep {
          Result<IpEndpoint, ResolveError>::Error(_) => { return 13; }
          Result<IpEndpoint, ResolveError>::Ok(endpoint_value) => {
            IpEndpoint endpoint = endpoint_value;
            String endpoint_text = std::net::resolve::endpoint_to_string(&endpoint);
            if (alloc::string::len(&endpoint_text) != 17) { return 14; }
          }
        }
      }
    }
    raz_rt_dealloc(text);

    usize host = raz_rt_alloc(9);
    i64 host_bytes[9] = [108,111,99,97,108,104,111,115,116];
    i = 0; while (i < 9) { put(host, i, host_bytes[i]); i += 1; }
    Result<ResolvedAddresses, ResolveError> resolved = std::net::resolve::resolve(host, 9);
    match resolved {
      Result<ResolvedAddresses, ResolveError>::Error(_) => { return 15; }
      Result<ResolvedAddresses, ResolveError>::Ok(list_value) => {
        ResolvedAddresses list = move list_value;
        if (std::net::resolve::len(&list) <= 0) { return 16; }
        i = 0;
        while (i < std::net::resolve::len(&list)) {
          IpAddress entry = std::net::resolve::get(&list, i);
          if (entry.family != 4 && entry.family != 6) { return 17; }
          i += 1;
        }
      }
    }
    raz_rt_dealloc(host);

    i64 listener = std::net::listen_any(0, 16);
    if (listener < 0 || std::net::local_port(listener) <= 0) { return 18; }
    i64 family = std::net::socket_family(listener);
    if (family != 4 && family != 6) { return 19; }
    std::net::close(listener);

    i64 udp = std::net::udp_bind_any(0);
    if (udp < 0 || std::net::local_port(udp) <= 0) { return 20; }
    family = std::net::socket_family(udp);
    if (family != 4 && family != 6) { return 21; }
    std::net::close(udp);

    usize loopback = raz_rt_alloc(9);
    i64 loopback_bytes[9] = [49,50,55,46,48,46,48,46,49];
    i = 0; while (i < 9) { put(loopback, i, loopback_bytes[i]); i += 1; }
    i64 receiver = std::net::udp_bind_host(loopback, 9, 0);
    if (receiver < 0) { return 22; }
    i64 sender = std::net::udp_connect(loopback, 9, std::net::local_port(receiver));
    if (sender < 0) { return 23; }
    usize left = raz_rt_alloc(2); put(left,0,65); put(left,1,66);
    usize right = raz_rt_alloc(3); put(right,0,67); put(right,1,68); put(right,2,69);
    IoVector sendv = std::net::vectored::create(2);
    if (!std::net::vectored::push(&mut sendv,left,2) || !std::net::vectored::push(&mut sendv,right,3)) { return 24; }
    if (std::net::vectored::send(sender,&sendv) != 5) { return 25; }
    PollSet scalable_pollset = std::net::poll_set::create(512);
    if (!std::net::poll_set::valid(&scalable_pollset) || std::net::poll_set::capacity(&scalable_pollset) != 512 ||
        !std::net::poll_set::reserve(&mut scalable_pollset, 1024) || std::net::poll_set::capacity(&scalable_pollset) < 1024) { return 43; }
    std::net::poll_set::destroy(&mut scalable_pollset);
    PollSet pollset = std::net::poll_set::create(2);
    if (!std::net::poll_set::push(&mut pollset, receiver, std::net::poll_set::readable) ||
        !std::net::poll_set::push(&mut pollset, sender, std::net::poll_set::writable)) { return 41; }
    if (std::net::poll_set::wait(&mut pollset, 1000) < 1 ||
        (std::net::poll_set::events(&pollset, 0) & std::net::poll_set::readable) == 0 ||
        (std::net::poll_set::events(&pollset, 1) & std::net::poll_set::writable) == 0) { return 42; }
    std::net::poll_set::destroy(&mut pollset);
    usize received = raz_rt_alloc(5);
    if (std::net::receive(receiver,received,5) != 5 || raz_rt_load_u8(received) != 65 || raz_rt_load_u8(received+4) != 69) { return 26; }

    usize source = raz_rt_alloc(4); put(source,0,70); put(source,1,71); put(source,2,72); put(source,3,73);
    if (std::net::send(sender,source,4) != 4) { return 27; }
    usize out1 = raz_rt_alloc(1); usize out2 = raz_rt_alloc(3);
    IoVector recvv = std::net::vectored::create(2);
    if (!std::net::vectored::push(&mut recvv,out1,1) || !std::net::vectored::push(&mut recvv,out2,3)) { return 28; }
    if (std::net::vectored::receive(receiver,&recvv) != 4) { return 29; }
    if (raz_rt_load_u8(out1) != 70 || raz_rt_load_u8(out2) != 71 || raz_rt_load_u8(out2+2) != 73) { return 30; }
    std::net::close(sender); std::net::close(receiver);
    raz_rt_dealloc(out2); raz_rt_dealloc(out1); raz_rt_dealloc(source); raz_rt_dealloc(received);
    raz_rt_dealloc(right); raz_rt_dealloc(left); raz_rt_dealloc(loopback);
    return 0;
}
]=])
raz_copy_stdlib_closure()

execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Network/concurrency build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/network_concurrency_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/network_concurrency_runtime_fixture")
endif()
execute_process(COMMAND "${runtime_exe}" WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Network/concurrency runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
