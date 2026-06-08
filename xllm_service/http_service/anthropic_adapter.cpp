/* Copyright 2026 The xLLM Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://github.com/jd-opensource/xllm-service/blob/main/LICENSE

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "http_service/anthropic_adapter.h"

#include <google/protobuf/util/json_util.h>
#include <json2pb/pb_to_json.h>

#include <cstdint>
#include <sstream>
#include <utility>

#include "api_service/anthropic_json.h"
#include "api_service/anthropic_stream_utils.h"
#include "api_service/chat_json_parser.h"
#include "common/xllm/uuid.h"

namespace xllm_service {
namespace {

thread_local llm::ShortUUID short_uuid;

AnthropicAdaptResult ok_result() { return AnthropicAdaptResult{}; }

AnthropicAdaptResult error_result(std::string error) {
  return AnthropicAdaptResult{false, std::move(error)};
}

std::string text_blocks(const xllm::proto::AnthropicContentBlockList& blocks,
                        Message::MMContentVec* mm_content) {
  std::string flat_text;
  bool first = true;
  for (const auto& block : blocks.blocks()) {
    if (!first) {
      flat_text += '\n';
    }
    flat_text += block.text();
    first = false;
    if (mm_content != nullptr) {
      mm_content->emplace_back("text", block.text());
    }
  }
  return flat_text;
}

std::string system_text(
    const xllm::proto::AnthropicContentBlockList& blocks) {
  std::string text;
  for (const auto& block : blocks.blocks()) {
    text += block.text();
  }
  return text;
}

AnthropicAdaptResult check_text_blocks(
    const xllm::proto::AnthropicContentBlockList& blocks) {
  for (const auto& block : blocks.blocks()) {
    if (block.type() != "text") {
      return error_result("Unsupported Anthropic content block type: " +
                          block.type());
    }
    if (!block.has_text()) {
      return error_result(
          "Missing text in Anthropic text content block.");
    }
  }
  return ok_result();
}

AnthropicAdaptResult add_system_msg(
    const xllm::proto::AnthropicMessagesRequest& request,
    xllm::proto::ChatRequest* chat_request,
    ChatMessages* messages) {
  if (request.has_system_string()) {
    auto* message = chat_request->add_messages();
    message->set_role("system");
    message->set_content(request.system_string());
    messages->emplace_back("system", request.system_string());
    return ok_result();
  }

  if (!request.has_system_blocks()) {
    return ok_result();
  }

  auto checked = check_text_blocks(request.system_blocks());
  if (!checked.ok) {
    return checked;
  }
  std::string text = system_text(request.system_blocks());
  if (text.empty()) {
    return ok_result();
  }
  auto* message = chat_request->add_messages();
  message->set_role("system");
  message->set_content(text);
  messages->emplace_back("system", std::move(text));
  return ok_result();
}

AnthropicAdaptResult add_content_msg(
    const xllm::proto::AnthropicMessage& src_message,
    xllm::proto::ChatRequest* chat_request,
    ChatMessages* messages) {
  switch (src_message.message_content_case()) {
    case xllm::proto::AnthropicMessage::kContentString: {
      auto* message = chat_request->add_messages();
      message->set_role(src_message.role());
      message->set_content(src_message.content_string());
      messages->emplace_back(src_message.role(), src_message.content_string());
      return ok_result();
    }
    case xllm::proto::AnthropicMessage::kContentBlocks: {
      auto checked = check_text_blocks(src_message.content_blocks());
      if (!checked.ok) {
        return checked;
      }

      Message::MMContentVec mm_content;
      std::string flat_text = text_blocks(src_message.content_blocks(),
                                          &mm_content);
      auto* message = chat_request->add_messages();
      message->set_role(src_message.role());
      message->set_content(flat_text);
      if (mm_content.size() > 1) {
        messages->emplace_back(src_message.role(), std::move(mm_content));
      } else {
        messages->emplace_back(src_message.role(), std::move(flat_text));
      }
      return ok_result();
    }
    case xllm::proto::AnthropicMessage::MESSAGE_CONTENT_NOT_SET:
    default:
      return error_result("Anthropic message content is required.");
  }
}

void fill_generation_params(
    const xllm::proto::AnthropicMessagesRequest& anthropic_request,
    xllm::proto::ChatRequest* chat_request) {
  chat_request->set_model(anthropic_request.model());
  chat_request->set_max_tokens(
      static_cast<uint32_t>(anthropic_request.max_tokens()));
  if (anthropic_request.has_stream()) {
    chat_request->set_stream(anthropic_request.stream());
  }
  if (anthropic_request.has_temperature()) {
    chat_request->set_temperature(anthropic_request.temperature());
  }
  if (anthropic_request.has_top_p()) {
    chat_request->set_top_p(anthropic_request.top_p());
  }
  if (anthropic_request.has_top_k()) {
    chat_request->set_top_k(anthropic_request.top_k());
  }
  if (anthropic_request.has_ignore_eos()) {
    chat_request->set_ignore_eos(anthropic_request.ignore_eos());
  }
  for (const auto& stop : anthropic_request.stop_sequences()) {
    chat_request->add_stop(stop);
  }
}

void fill_usage(const llm::RequestOutput& request_output,
                xllm::proto::AnthropicMessagesResponse* response) {
  if (!request_output.usage.has_value()) {
    return;
  }
  const auto& usage = request_output.usage.value();
  auto* proto_usage = response->mutable_usage();
  proto_usage->set_input_tokens(
      static_cast<int32_t>(usage.num_prompt_tokens));
  proto_usage->set_output_tokens(
      static_cast<int32_t>(usage.num_generated_tokens));
}

}  // namespace

std::string new_anthropic_id() {
  std::stringstream ss;
  ss << "anthropiccmpl-" << short_uuid.random();
  return ss.str();
}

AnthropicAdaptResult parse_anthropic_json(
    std::string json,
    xllm::proto::AnthropicMessagesRequest* request) {
  auto [preprocess_status, processed_json] =
      xllm::ChatJsonParser::anthropic().preprocess(std::move(json));
  if (!preprocess_status.ok()) {
    return error_result(preprocess_status.message());
  }

  google::protobuf::util::JsonParseOptions options;
  options.ignore_unknown_fields = true;
  auto status = google::protobuf::util::JsonStringToMessage(
      processed_json, request, options);
  if (!status.ok()) {
    return error_result(status.ToString());
  }
  return ok_result();
}

AnthropicAdaptResult fill_chat_req(
    const xllm::proto::AnthropicMessagesRequest& anthropic_request,
    xllm::proto::ChatRequest* chat_request,
    ChatMessages* messages) {
  if (anthropic_request.has_stream() && anthropic_request.stream()) {
    return error_result("Anthropic streaming is not supported yet.");
  }
  if (anthropic_request.tools_size() > 0 ||
      anthropic_request.has_tool_choice()) {
    return error_result("Anthropic tools are not supported yet.");
  }
  if (anthropic_request.max_tokens() < 0) {
    return error_result("Anthropic max_tokens must be non-negative.");
  }

  chat_request->Clear();
  messages->clear();
  fill_generation_params(anthropic_request, chat_request);

  auto system_result = add_system_msg(anthropic_request, chat_request, messages);
  if (!system_result.ok) {
    return system_result;
  }
  for (const auto& message : anthropic_request.messages()) {
    auto content_result = add_content_msg(message, chat_request, messages);
    if (!content_result.ok) {
      return content_result;
    }
  }
  return ok_result();
}

AnthropicAdaptResult fill_anthropic_resp(
    const std::string& model,
    const llm::RequestOutput& request_output,
    xllm::proto::AnthropicMessagesResponse* response) {
  response->Clear();
  response->set_id(request_output.request_id);
  response->set_type("message");
  response->set_role("assistant");
  response->set_model(model);
  fill_usage(request_output, response);

  if (request_output.outputs.empty()) {
    return ok_result();
  }

  const auto& output = request_output.outputs.front();
  if (output.finish_reason.has_value()) {
    response->set_stop_reason(
        xllm::api_service::convert_finish_reason_to_anthropic(
            output.finish_reason.value()));
  }

  auto* text_block = response->add_content();
  text_block->set_type("text");
  text_block->set_text(output.text);
  return ok_result();
}

bool anthropic_json(const xllm::proto::AnthropicMessagesResponse& response,
                    std::string* json,
                    std::string* error) {
  json2pb::Pb2JsonOptions options;
  options.bytes_to_base64 = false;
  options.jsonify_empty_array = true;
  return xllm::api_service::proto_to_anthropic_json(
      response, options, json, error);
}

}  // namespace xllm_service
