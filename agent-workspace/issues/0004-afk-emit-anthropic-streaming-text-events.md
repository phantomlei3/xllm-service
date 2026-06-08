---
id: 0004
title: Emit Anthropic streaming text events
type: AFK
status: completed
blocked_by: ["0001", "0002"]
source_spec: agent-workspace/anthropic.md
---

## Context

OpenAI streaming in xllm-service writes `data: <json>` chunks and a `[DONE]` marker. Anthropic streaming requires event-framed SSE and a distinct event sequence. This slice adds streaming for text-only Anthropic requests without reusing the OpenAI chunk writer.

## What to Build

Add Anthropic-specific stream state and write methods so `/v1/messages` with `stream: true` emits xllm-compatible Anthropic SSE text events through the existing scheduler callback lifecycle.

## Scope

- Add Anthropic-specific SSE write support to `AnthropicCallData`:
  - `event: <type>`.
  - `data: <json>`.
  - blank line separator.
- Keep streaming content type as `text/event-stream`.
- Add Anthropic stream state in the scheduler registration path.
- Add response handler methods for text streaming:
  - `message_start`.
  - `content_block_start`.
  - `content_block_delta`.
  - `content_block_stop`.
  - `message_delta`.
  - `message_stop`.
- Use public Anthropic JSON helper functions for event serialization where possible.
- Convert finish reason through xllm Anthropic helpers.
- Verify whether current xllm Anthropic behavior emits a final `[DONE]` marker and mirror that behavior exactly.
- Add tests for event order, event names, text deltas, final stop reason, and absence/presence of `[DONE]` according to xllm current behavior.

## Non-Goals

- Streaming tool-call argument deltas.
- Reasoning-specific streaming behavior unless already exposed by the basic Chat output parser path.
- Official Anthropic error JSON event compatibility.
- Any change to OpenAI streaming writer behavior.

## Acceptance Criteria

- Correctness: text streaming requests emit the Anthropic event sequence required by the PRD.
- Correctness: deltas are sent incrementally and do not wait for the full final output.
- Compatibility: event JSON and terminal behavior mirror current xllm Anthropic output behavior.
- Performance/resource usage: streaming does not buffer the full assistant text before sending deltas.
- Stability: stream disconnect and callback failure finish the call through the existing cleanup path.
- Observability/operability: stream chunks participate in current request trace behavior when tracing is enabled.

## Verification

- Unit or integration tests: stream handler tests asserting ordered event names and serialized event payloads.
- Service validation: requester-owned streaming `/v1/messages` text request and SSE inspection.
- Benchmark/eval checks: none required.
- Build gate: run README local build/test commands and, before code delivery, `$xllm-build` / `python setup.py build --device mlu`.

## Dependencies

Blocked by:

- `0001-afk-add-anthropic-text-message-path.md`
- `0002-afk-enforce-anthropic-content-block-scope.md`
