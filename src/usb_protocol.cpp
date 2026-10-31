#include "usb_protocol.h"

#include <ctype.h>
#include <string.h>

namespace {
// ArduinoJson stops at the closing brace. Track its input position so a second
// object or junk suffix cannot turn a malformed line into a toggle command.
struct JsonInput {
  const char *text;
  size_t position = 0;
  explicit JsonInput(const char *value) : text(value) {}
  int read() {
    if (!text[position]) return -1;
    return static_cast<unsigned char>(text[position++]);
  }
  size_t readBytes(char *buffer, size_t count) {
    size_t readCount = 0;
    while (readCount < count && text[position])
      buffer[readCount++] = text[position++];
    return readCount;
  }
};
}  // namespace

bool LineReader::push(char value) {
  if (value == '\n') {
    line_[length_] = '\0';
    finishedOverflow_ = overflow_;
    length_ = 0;
    overflow_ = false;
    return true;
  }
  // CR counts toward the limit; JSON permits it as trailing whitespace.
  if (length_ < kCapacity) {
    // Embedded NUL would otherwise hide a suffix from the JSON parser.
    if (!value) overflow_ = true;
    line_[length_++] = value;
  } else overflow_ = true;
  return false;
}

bool parseRequest(const char *line, JsonDocument &document, Request &request) {
  request = Request{};
  JsonInput input(line);
  if (deserializeJson(document, input) || !document.is<JsonObject>())
    return false;
  JsonVariantConst id = document["id"];
  if (!id.is<uint32_t>() || id.as<uint32_t>() > 0x7FFFFFFF) return false;
  request.id = id.as<uint32_t>();
  request.hasId = true;
  while (isspace(static_cast<unsigned char>(line[input.position])))
    ++input.position;
  if (line[input.position]) return false;
  if (!document["cmd"].is<const char *>()) return false;
  JsonString command = document["cmd"].as<JsonString>();
  request.command = command.c_str();
  if (strlen(request.command) != command.size()) return false;
  return request.command[0] != '\0';
}
