# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED RAZ_EXE OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED WORK_ROOT)
  message(FATAL_ERROR "Application stack runtime requires RAZ_EXE, SOURCE_ROOT, WORK_ROOT")
endif()
include("${SOURCE_ROOT}/tests/cmake/copy_stdlib_closure.cmake")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/raz.toml" [=[
[package]
name = "application_stack_runtime_fixture"
version = "1.0.0"
kind = "executable"
entry = "src/main.rz"
]=])
file(WRITE "${WORK_ROOT}/src/main.rz" [=[
import alloc::string;
import core::bytes;
import core::result;
import std::encoding::json;
import std::net::http;
import std::net::tls;
import std::net::url;
import std::net::url::form;
import std::net::url::query;

fn push_ascii(String&mut value, i64 byte) -> bool { return alloc::string::push_byte(value, byte); }

fn main() -> i64 {
    // JSON writer -> validator -> token conversion.
    String json = alloc::string::with_capacity(32);
    push_ascii(&mut json, 123);
    String key = alloc::string::with_capacity(1); push_ascii(&mut key, 110);
    if (!std::encoding::json::append_escaped_string(&mut json, alloc::string::data_ptr(&key), 1)) { return 1; }
    push_ascii(&mut json, 58);
    if (!std::encoding::json::append_i64(&mut json, -42)) { return 2; }
    push_ascii(&mut json, 125);
    if (std::encoding::json::validate(alloc::string::data_ptr(&json), alloc::string::len(&json), 64) != JsonError::None) { return 3; }
    JsonLexer lexer = std::encoding::json::lexer(alloc::string::data_ptr(&json), alloc::string::len(&json));
    JsonToken token = std::encoding::json::next(&mut lexer); if (token.kind != JsonTokenKind::LeftBrace) { return 4; }
    token = std::encoding::json::next(&mut lexer); if (token.kind != JsonTokenKind::String) { return 5; }
    token = std::encoding::json::next(&mut lexer); if (token.kind != JsonTokenKind::Colon) { return 6; }
    token = std::encoding::json::next(&mut lexer); i64 integer = 0;
    if (!std::encoding::json::token_i64(&token, &mut integer) || integer != -42) { return 7; }

    // HTTP writer -> zero-copy parser + header analysis.
    String method = alloc::string::with_capacity(3); push_ascii(&mut method,71); push_ascii(&mut method,69); push_ascii(&mut method,84);
    String target = alloc::string::with_capacity(1); push_ascii(&mut target,47);
    String content_name = alloc::string::with_capacity(14);
    i64 cn[14] = [67,111,110,116,101,110,116,45,76,101,110,103,116,104];
    i64 i=0; while(i<14){push_ascii(&mut content_name,cn[i]);i+=1;}
    String zero = alloc::string::with_capacity(1); push_ascii(&mut zero,48);
    String close_name = alloc::string::with_capacity(10);
    i64 xn[10] = [67,111,110,110,101,99,116,105,111,110]; i=0; while(i<10){push_ascii(&mut close_name,xn[i]);i+=1;}
    String close_value = alloc::string::with_capacity(5); i64 xv[5]=[99,108,111,115,101]; i=0; while(i<5){push_ascii(&mut close_value,xv[i]);i+=1;}
    String request = alloc::string::with_capacity(96);
    if (!std::net::http::append_request_line(&mut request, alloc::string::data_ptr(&method),3,alloc::string::data_ptr(&target),1)) { return 8; }
    if (!std::net::http::append_header(&mut request,alloc::string::data_ptr(&content_name),14,alloc::string::data_ptr(&zero),1)) { return 9; }
    if (!std::net::http::append_header(&mut request,alloc::string::data_ptr(&close_name),10,alloc::string::data_ptr(&close_value),5)) { return 10; }
    if (!std::net::http::end_headers(&mut request)) { return 11; }
    HttpRequestView request_view = HttpRequestView { source: BytesView { data: 0, length: 0 }, method: BytesView { data: 0, length: 0 }, target: BytesView { data: 0, length: 0 }, version: BytesView { data: 0, length: 0 }, header_block: BytesView { data: 0, length: 0 }, body: BytesView { data: 0, length: 0 }, content_length: -1, chunked: false, connection_close: false };
    if (std::net::http::parse_request(alloc::string::data_ptr(&request),alloc::string::len(&request),16384,&mut request_view) != HttpError::None) { return 12; }
    if (request_view.content_length != 0 || !request_view.connection_close || request_view.method.length != 3) { return 13; }

    // Header injection and ambiguous framing are rejected.
    String bad_value = alloc::string::with_capacity(3); push_ascii(&mut bad_value,97); push_ascii(&mut bad_value,13); push_ascii(&mut bad_value,10);
    if (std::net::http::append_header(&mut request,alloc::string::data_ptr(&content_name),14,alloc::string::data_ptr(&bad_value),3)) { return 14; }

    // Chunk framing.
    String chunked = alloc::string::with_capacity(20);
    i64 ch[20]=[49,13,10,97,13,10,48,13,10,88,58,32,49,13,10,13,10,0,0,0]; i=0; while(i<17){push_ascii(&mut chunked,ch[i]);i+=1;}
    ChunkCursor cursor = std::net::http::chunks(BytesView { data: alloc::string::data_ptr(&chunked), length: alloc::string::len(&chunked) });
    ChunkView chunk = ChunkView { data: BytesView { data: 0, length: 0 }, trailers: BytesView { data: 0, length: 0 }, final_chunk: false };
    if (std::net::http::next_chunk(&mut cursor,&mut chunk) != HttpError::None || chunk.data.length != 1) { return 15; }
    if (std::net::http::next_chunk(&mut cursor,&mut chunk) != HttpError::None || !chunk.final_chunk || chunk.trailers.length != 6) { return 16; }

    // Form/query codec stays allocation-free until decoding is requested.
    String form_input = alloc::string::with_capacity(5); i64 fv[5]=[97,32,98,38,99];i=0;while(i<5){push_ascii(&mut form_input,fv[i]);i+=1;}
    String encoded = alloc::string::with_capacity(16);
    if (!std::net::url::form::form_encode(&mut encoded,alloc::string::data_ptr(&form_input),5)) { return 17; }
    String decoded = alloc::string::with_capacity(8);
    if (!std::net::url::form::decode(&mut decoded,alloc::string::data_ptr(&encoded),alloc::string::len(&encoded),true) || !alloc::string::equals(&decoded,&form_input)) { return 18; }

    String query_text=alloc::string::with_capacity(7); i64 qv[7]=[97,61,49,38,98,61,50];i=0;while(i<7){push_ascii(&mut query_text,qv[i]);i+=1;}
    QueryIterator qi=std::net::url::query::pairs(BytesView { data: alloc::string::data_ptr(&query_text), length: 7 }); QueryPair qp=QueryPair { key: BytesView { data: 0, length: 0 }, value: BytesView { data: 0, length: 0 }, has_value: false };
    if(!std::net::url::query::next(&mut qi,&mut qp)||qp.key.length!=1||qp.value.length!=1){return 19;}
    if(!std::net::url::query::next(&mut qi,&mut qp)||qp.key.length!=1||qp.value.length!=1){return 20;}

    // RFC 3986 bracketed IPv6 host parsing.
    String url=alloc::string::with_capacity(24); i64 uv[24]=[104,116,116,112,58,47,47,91,58,58,49,93,58,56,48,56,48,47,120,63,113,61,49,0];i=0;while(i<23){push_ascii(&mut url,uv[i]);i+=1;}
    auto parsed_url=std::net::url::parse(alloc::string::data_ptr(&url),alloc::string::len(&url));
    match parsed_url { Result<UrlView,UrlError>::Error(_)=>{return 21;} Result<UrlView,UrlError>::Ok(view)=>{if(view.host.length!=3||view.port!=8080){return 22;}} }

    // Optional TLS engine: start a real client handshake and verify encrypted
    // ClientHello bytes are produced through the memory BIO boundary.
    if (std::net::tls::available()) {
        String host=alloc::string::with_capacity(11); i64 hv[11]=[101,120,97,109,112,108,101,46,99,111,109];i=0;while(i<11){push_ascii(&mut host,hv[i]);i+=1;}
        TlsEngine engine=std::net::tls::engine_create_client(alloc::string::data_ptr(&host),11);
        if(!std::net::tls::engine_valid(&engine)){return 23;}
        if(std::net::tls::engine_handshake(&mut engine)!=0){return 24;}
        i64 pending=std::net::tls::engine_pending_encrypted(&engine); if(pending<=0){return 25;}
        String encrypted=alloc::string::with_capacity(pending);
        if(std::net::tls::engine_drain(&mut engine,alloc::string::data_ptr(&encrypted),pending)<=0){return 26;}
    }
    return 0;
}
]=])
raz_copy_stdlib_closure()

execute_process(COMMAND "${RAZ_EXE}" build "${WORK_ROOT}" --target test-host --profile debug --force
  RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Application stack build failed:\n${build_error}\n${build_output}")
endif()
if(WIN32)
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/application_stack_runtime_fixture.exe")
else()
  set(runtime_exe "${WORK_ROOT}/target/test-host/debug/application_stack_runtime_fixture")
endif()
execute_process(COMMAND "${runtime_exe}" WORKING_DIRECTORY "${WORK_ROOT}"
  RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_output ERROR_VARIABLE runtime_error)
if(NOT runtime_result EQUAL 0)
  message(FATAL_ERROR "Application stack runtime returned ${runtime_result}:\n${runtime_error}\n${runtime_output}")
endif()
