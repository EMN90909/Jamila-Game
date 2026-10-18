#pragma once
#include <string>
namespace jamila {
// Pure adapters for Anthropic Messages API; never execute tools or perform network I/O.
std::string to_anthropic_messages(const std::string& openai_request);
std::string from_anthropic_messages(const std::string& anthropic_response);
}