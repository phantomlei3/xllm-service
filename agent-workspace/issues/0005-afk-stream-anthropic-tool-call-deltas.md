---
id: 0005
title: Stream Anthropic tool call deltas
type: AFK
status: completed
blocked_by: ["0003", "0004"]
source_spec: agent-workspace/anthropic.md
---

## Context

After tools and basic streaming exist, the remaining protocol-critical behavior is streaming tool use. xllm already has public helpers for tool argument deltas and unstreamed tool args; this slice wires those helpers into the xllm-service Anthropic response path.

## What to Build

Extend the Anthropic stream response handler to detect tool calls, open Anthropic `tool_use` content blocks, stream `input_json_delta` events, and close blocks consistently with xllm current Anthropic behavior.

## Scope

- Create or reuse `StreamOutputParser` in the Anthropic scheduler/request state when tools or parser config require it.
- Use the same parser config selection style as the Chat path.
- Use public helpers:
  - `make_input_json_delta_event()`.
  - `check_for_unstreamed_tool_args()`.
  - `get_stream_stop_reason()`.
- Emit proper `content_block_start` events for tool use blocks.
- Emit `content_block_delta` events with `input_json_delta` for streamed tool args.
- Emit any missing argument payload required by `check_for_unstreamed_tool_args()`.
- Emit block stop, message delta, and message stop consistently after tool calls.
- Add tests for streamed tool args, unstreamed tool args, mixed text/tool sequencing if supported by current xllm behavior, and final stop reason.

## Non-Goals

- New function-call parsing algorithms.
- Multimodal tool inputs or tool results.
- Changes to backend ChatCompletions generation behavior.
- Broader reasoning parser refactors.

## Acceptance Criteria

- Correctness: streaming tool calls produce Anthropic `tool_use` blocks and `input_json_delta` chunks aligned with xllm current behavior.
- Correctness: unstreamed or partially streamed tool arguments are flushed before block/message stop events.
- Compatibility: tool streaming uses the public xllm helper functions available to xllm-service and avoids copying third-party anonymous namespace code where possible.
- Performance/resource usage: parser state is per request and cleaned when the request finishes.
- Stability: malformed or incomplete tool deltas do not leave open content blocks or stuck scheduler records.
- Observability/operability: trace logs show stream chunks in the same style as other streaming requests.

## Verification

- Unit or integration tests: response handler tests for streamed and unstreamed tool args, event order, and stop reason.
- Service validation: requester-owned streaming `/v1/messages` tools request.
- Benchmark/eval checks: none required.
- Build gate: run README local build/test commands and, before code delivery, `$xllm-build` / `python setup.py build --device mlu`.

## Dependencies

Blocked by:

- `0003-afk-map-anthropic-tools-nonstream.md`
- `0004-afk-emit-anthropic-streaming-text-events.md`
