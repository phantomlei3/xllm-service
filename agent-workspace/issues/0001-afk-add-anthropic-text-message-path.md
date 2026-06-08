---
id: 0001
title: Add the basic Anthropic text message path
type: AFK
status: completed
blocked_by: []
source_spec: agent-workspace/anthropic.md
---

## Context

xllm-service currently exposes OpenAI-compatible HTTP routes but has no `/v1/messages` entrypoint. This slice creates the smallest complete Anthropic Messages path for LLM text-only, non-streaming requests while keeping execution inside the existing xllm-service scheduler and backend `ChatCompletions` typed RPC path.

This implements the first tracer bullet of the PRD: route -> Anthropic request parse -> ChatRequest execution request -> Scheduler -> backend ChatCompletions -> Anthropic non-stream response.

## What to Build

Add `/v1/messages` as an xllm-service route and implement a text-only non-streaming path that accepts Anthropic `model`, `messages`, `system`, `max_tokens`, `temperature`, `top_p`, `top_k`, `stop_sequences`, `stream`, and trace/routing fields already used by the service.

The handler should use `xllm::ChatJsonParser::anthropic()` and parse into `xllm::proto::AnthropicMessagesRequest`, then map to `xllm::proto::ChatRequest` for execution. Add an independent `AnthropicCallData` type and a scheduler registration path, but keep the response behavior initially limited to final non-stream text responses.

## Scope

- Register `"/v1/messages => AnthropicMessages"` in `xllm_service::Master::setup_http_server()`.
- Add `AnthropicMessages(HttpRequest) returns (HttpResponse)` to `xllm_service/proto/xllm_http_service.proto`.
- Add the `XllmHttpServiceImpl::AnthropicMessages` method and declaration.
- Parse Anthropic JSON through the public xllm Anthropic parser, with `ignore_unknown_fields = true`.
- Add request mapping for:
  - `system` string or text block system content.
  - message content string and text content blocks.
  - generation params listed in the PRD.
  - `stop_sequences` -> Chat `stop`.
- Add `AnthropicCallData` independent from `ChatCallData`.
- Add `Scheduler::record_new_request` overload for `AnthropicCallData`.
- Dispatch `AnthropicCallData` through backend `XllmAPIService_Stub::ChatCompletions`.
- Add a non-stream response builder that returns Anthropic `message` JSON using public Anthropic JSON conversion helpers where available.
- Add focused adapter/unit tests for text request mapping and final text response conversion.

## Non-Goals

- Tools, `tool_use`, and `tool_result`.
- Streaming SSE.
- Image or unknown content block support.
- Official Anthropic error JSON compatibility.
- Any change under `third_party/xllm`.

## Acceptance Criteria

- Correctness: `/v1/messages` reaches xllm-service code, constructs a `ChatRequest`, enters `Scheduler::schedule()` and `record_new_request()`, and executes backend `ChatCompletions`.
- Correctness: a non-streaming text request returns an Anthropic `message` object with `type = "message"`, `role = "assistant"`, model, content text block, stop reason, and usage.
- Compatibility: existing `/v1/chat/completions` and `/v1/completions` behavior is unchanged.
- Performance/resource usage: the path does not add a second model execution or additional tokenization beyond the current schedule path.
- Stability: non-stream prefill-only output is not returned as a final response.
- Observability/operability: request ids and scheduler records are created and cleaned through the existing request lifecycle.

## Verification

- Unit or integration tests: adapter tests for system string, text messages, generation params, `stop_sequences`, and non-stream response JSON.
- Service validation: requester-owned end-to-end non-stream `/v1/messages` text request.
- Benchmark/eval checks: none required for this slice; no runtime/kernel behavior changes.
- Build gate: run README local build/test commands and, before code delivery, `$xllm-build` / `python setup.py build --device mlu`.

## Dependencies

None - can start immediately.
