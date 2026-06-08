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

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <variant>

namespace xllm_service {
namespace {

xllm::proto::AnthropicMessagesRequest parse_request(
    const std::string& json) {
  xllm::proto::AnthropicMessagesRequest request;
  auto result = parse_anthropic_json(json, &request);
  EXPECT_TRUE(result.ok) << result.error;
  return request;
}

const std::string& text_content(const Message& message) {
  return std::get<std::string>(message.content);
}

TEST(AnthropicAdapterTest, MapsStringSystemMessagesAndParams) {
  auto request = parse_request(R"({
    "model": "test-model",
    "system": "You are concise.",
    "max_tokens": 32,
    "temperature": 0.2,
    "top_p": 0.9,
    "top_k": 20,
    "stop_sequences": ["</stop>"],
    "ignore_eos": true,
    "messages": [
      {"role": "user", "content": "hello"}
    ]
  })");

  xllm::proto::ChatRequest chat_request;
  ChatMessages messages;
  auto result = fill_chat_req(request, &chat_request, &messages);
  ASSERT_TRUE(result.ok) << result.error;

  EXPECT_EQ(chat_request.model(), "test-model");
  ASSERT_TRUE(chat_request.has_max_tokens());
  EXPECT_EQ(chat_request.max_tokens(), 32);
  ASSERT_TRUE(chat_request.has_temperature());
  EXPECT_FLOAT_EQ(chat_request.temperature(), 0.2f);
  ASSERT_TRUE(chat_request.has_top_p());
  EXPECT_FLOAT_EQ(chat_request.top_p(), 0.9f);
  ASSERT_TRUE(chat_request.has_top_k());
  EXPECT_EQ(chat_request.top_k(), 20);
  ASSERT_TRUE(chat_request.has_ignore_eos());
  EXPECT_TRUE(chat_request.ignore_eos());
  ASSERT_EQ(chat_request.stop_size(), 1);
  EXPECT_EQ(chat_request.stop(0), "</stop>");

  ASSERT_EQ(chat_request.messages_size(), 2);
  EXPECT_EQ(chat_request.messages(0).role(), "system");
  EXPECT_EQ(chat_request.messages(0).content(), "You are concise.");
  EXPECT_EQ(chat_request.messages(1).role(), "user");
  EXPECT_EQ(chat_request.messages(1).content(), "hello");

  ASSERT_EQ(messages.size(), 2);
  EXPECT_EQ(messages[0].role, "system");
  EXPECT_EQ(text_content(messages[0]), "You are concise.");
  EXPECT_EQ(messages[1].role, "user");
  EXPECT_EQ(text_content(messages[1]), "hello");
}

TEST(AnthropicAdapterTest, MapsTextBlocksForSystemAndMessages) {
  auto request = parse_request(R"({
    "model": "test-model",
    "system": [
      {"type": "text", "text": "rule "},
      {"type": "text", "text": "style"}
    ],
    "max_tokens": 8,
    "messages": [
      {
        "role": "user",
        "content": [
          {"type": "text", "text": "hello"},
          {"type": "text", "text": "world"}
        ]
      }
    ]
  })");

  xllm::proto::ChatRequest chat_request;
  ChatMessages messages;
  auto result = fill_chat_req(request, &chat_request, &messages);
  ASSERT_TRUE(result.ok) << result.error;

  ASSERT_EQ(chat_request.messages_size(), 2);
  EXPECT_EQ(chat_request.messages(0).content(), "rule style");
  EXPECT_EQ(chat_request.messages(1).content(), "hello\nworld");

  ASSERT_EQ(messages.size(), 2);
  EXPECT_EQ(text_content(messages[0]), "rule style");
  ASSERT_TRUE(std::holds_alternative<Message::MMContentVec>(
      messages[1].content));
  const auto& content = std::get<Message::MMContentVec>(messages[1].content);
  ASSERT_EQ(content.size(), 2);
  EXPECT_EQ(content[0].type, "text");
  EXPECT_EQ(content[0].text, "hello");
  EXPECT_EQ(content[1].type, "text");
  EXPECT_EQ(content[1].text, "world");
}

TEST(AnthropicAdapterTest, RejectsNonTextBlocks) {
  auto request = parse_request(R"({
    "model": "test-model",
    "max_tokens": 8,
    "messages": [
      {
        "role": "user",
        "content": [
          {"type": "image", "source": {"type": "base64", "data": "x"}}
        ]
      }
    ]
  })");

  xllm::proto::ChatRequest chat_request;
  ChatMessages messages;
  auto result = fill_chat_req(request, &chat_request, &messages);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("Unsupported Anthropic content block type"),
            std::string::npos);
}

TEST(AnthropicAdapterTest, BuildsNonStreamAnthropicJson) {
  llm::RequestOutput output;
  output.request_id = "anthropiccmpl-test";
  output.finished = true;
  llm::SequenceOutput seq;
  seq.index = 0;
  seq.text = "answer";
  seq.finish_reason = "stop";
  output.outputs.push_back(std::move(seq));
  llm::Usage usage;
  usage.num_prompt_tokens = 3;
  usage.num_generated_tokens = 4;
  usage.num_total_tokens = 7;
  output.usage = usage;

  xllm::proto::AnthropicMessagesResponse response;
  auto result = fill_anthropic_resp("test-model", output, &response);
  ASSERT_TRUE(result.ok) << result.error;

  std::string json_str;
  std::string error;
  ASSERT_TRUE(anthropic_json(response, &json_str, &error)) << error;
  auto json = nlohmann::json::parse(json_str);

  EXPECT_EQ(json["id"], "anthropiccmpl-test");
  EXPECT_EQ(json["type"], "message");
  EXPECT_EQ(json["role"], "assistant");
  EXPECT_EQ(json["model"], "test-model");
  EXPECT_EQ(json["stop_reason"], "end_turn");
  ASSERT_EQ(json["content"].size(), 1);
  EXPECT_EQ(json["content"][0]["type"], "text");
  EXPECT_EQ(json["content"][0]["text"], "answer");
  EXPECT_EQ(json["usage"]["input_tokens"], 3);
  EXPECT_EQ(json["usage"]["output_tokens"], 4);
  EXPECT_FALSE(json["usage"].contains("total_tokens"));
}

TEST(AnthropicAdapterTest, MapsLengthStopReason) {
  llm::RequestOutput output;
  output.request_id = "anthropiccmpl-test";
  llm::SequenceOutput seq;
  seq.index = 0;
  seq.text = "";
  seq.finish_reason = "length";
  output.outputs.push_back(std::move(seq));

  xllm::proto::AnthropicMessagesResponse response;
  auto result = fill_anthropic_resp("test-model", output, &response);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(response.stop_reason(), "max_tokens");
}

}  // namespace
}  // namespace xllm_service
