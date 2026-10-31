#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

struct Request {
  uint32_t id = 0;
  bool hasId = false;
  const char *command = nullptr;
};

// Owns framing only; parseRequest's command points into the supplied document.
class LineReader {
 public:
  static constexpr size_t kCapacity = 256;
  bool push(char value);
  const char *line() const { return line_; }
  bool overflowed() const { return finishedOverflow_; }

 private:
  char line_[kCapacity + 1] = {};
  size_t length_ = 0;
  bool overflow_ = false;
  bool finishedOverflow_ = false;
};

bool parseRequest(const char *line, JsonDocument &document, Request &request);
