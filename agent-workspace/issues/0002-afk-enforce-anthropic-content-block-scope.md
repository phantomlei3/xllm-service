---
id: 0002
title: Enforce Anthropic content block scope
type: AFK
status: completed
blocked_by: ["0001"]
source_spec: agent-workspace/anthropic.md
---

## Context

The accepted scope is LLM text + tools only. Multimodal/image and unknown content blocks must fail clearly instead of becoming placeholders or being silently ignored. This slice makes the basic Anthropic path robust against unsupported request shapes and completes the text content block behavior.

## What to Build

Extend the Anthropic request adapter from issue 0001 so it mirrors xllm current rules for supported text blocks and returns explicit service errors for unsupported blocks. This includes both top-level `system` blocks and message `content` blocks.

## Scope

- Validate `messages_size() > 0`.
- Accept system as string and text content block list.
- Accept user/assistant message content as string or text content block list.
- Preserve role mapping for text-only conversation history.
- Reject image/multimodal content blocks with an explicit unsupported error.
- Reject unknown content block variants with an explicit unsupported error.
- Keep error style aligned with current xllm-service `SetFailed`/text behavior.
- Add tests for empty messages, system blocks, text blocks, image blocks, and unknown blocks.

## Non-Goals

- Tool mapping and tool output formatting.
- Streaming behavior.
- Anthropic official error JSON structure.
- Changes to third-party xllm parsers or protos.

## Acceptance Criteria

- Correctness: valid text block requests map to the same execution `ChatRequest` semantics as equivalent string content requests.
- Correctness: empty `messages` fails before scheduling.
- Correctness: image/multimodal/unknown content blocks fail before scheduling with a clear unsupported message.
- Compatibility: unknown JSON fields remain ignored by protobuf parsing when they do not map to unsupported content variants.
- Performance/resource usage: validation is linear in request content blocks and does not duplicate large content unnecessarily.
- Stability: unsupported requests do not create scheduler records or backend RPCs.
- Observability/operability: validation failures are visible through the same failure mechanism as current facade errors.

## Verification

- Unit or integration tests: request adapter validation tests for accepted text forms and rejected unsupported forms.
- Service validation: requester-owned image block request should return an explicit unsupported error.
- Benchmark/eval checks: none required.
- Build gate: run README local build/test commands and, before code delivery, `$xllm-build` / `python setup.py build --device mlu`.

## Dependencies

Blocked by:

- `0001-afk-add-anthropic-text-message-path.md`
