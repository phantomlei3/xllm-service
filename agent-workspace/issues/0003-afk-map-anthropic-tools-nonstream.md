---
id: 0003
title: Map Anthropic tools on the non-stream path
type: AFK
status: completed
blocked_by: ["0002"]
source_spec: agent-workspace/anthropic.md
---

## Context

Anthropic Messages compatibility must include LLM text + tools. The non-stream path should support historical `tool_use` and `tool_result`, request `tools`, `tool_choice`, and final tool-use content in the Anthropic response while still executing through backend `ChatCompletions`.

## What to Build

Extend the request adapter and non-stream response builder to mirror xllm current Anthropic tool behavior. Convert Anthropic tools to OpenAI-style Chat function tools, convert Anthropic `tool_choice` to Chat tool choice, preserve historical tool call context, and emit Anthropic `tool_use` blocks when the backend response contains tool calls.

## Scope

- Map Anthropic `tools` fields:
  - `name`.
  - `description`.
  - `input_schema` -> function parameters.
- Map `tool_choice` by xllm current rules:
  - absent + tools -> `auto`.
  - absent + no tools -> `none`.
  - `auto` -> `auto`.
  - `any` -> `required`.
  - specific tool -> `{"type":"function","function":{"name":"..."}}`.
  - unknown -> `auto`.
- Map historical assistant `tool_use` blocks to Chat assistant messages with `tool_calls`.
- Map user `tool_result` blocks to Chat role `tool` messages with `tool_call_id`.
- Map assistant `tool_result` blocks according to xllm current text fallback behavior.
- Convert non-stream backend tool calls into Anthropic `tool_use` content blocks.
- Add tests for all tool choice cases, tool schema mapping, historical tool use/result mapping, and non-stream tool response conversion.

## Non-Goals

- Streaming tool-call deltas.
- Multimodal tool results.
- New tool parser behavior beyond public xllm helpers already used by xllm-service.
- Modifying backend xllm tool handling.

## Acceptance Criteria

- Correctness: Anthropic tools and tool choice produce the expected Chat request fields.
- Correctness: tool history is preserved well enough for existing ChatCompletions execution to continue a tool conversation.
- Correctness: final backend tool calls are returned as Anthropic `tool_use` content blocks with stable ids, names, and input JSON.
- Compatibility: conversion follows the xllm Anthropic rules listed in the source PRD.
- Performance/resource usage: tool schema conversion avoids repeated parsing in streaming or callback paths.
- Stability: malformed unsupported tool content fails clearly rather than creating partial tool calls.
- Observability/operability: trace/routing behavior remains the same as text requests.

## Verification

- Unit or integration tests: adapter tests for `tools`, every `tool_choice` rule, `tool_use`, `tool_result`, and non-stream tool response conversion.
- Service validation: requester-owned non-stream `/v1/messages` tools request.
- Benchmark/eval checks: none required.
- Build gate: run README local build/test commands and, before code delivery, `$xllm-build` / `python setup.py build --device mlu`.

## Dependencies

Blocked by:

- `0002-afk-enforce-anthropic-content-block-scope.md`
