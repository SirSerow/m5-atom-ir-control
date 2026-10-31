#pragma once

#include <stdint.h>

enum class Mode { Normal, Learning, Confirm };
enum class ButtonAction { None, Toggle, BeginLearning, Confirm, Cancel };

// Pure timing logic: hardware access and side effects live in main.cpp.
class Controller {
 public:
  static constexpr uint32_t kDebounceMs = 30;
  static constexpr uint32_t kHoldMs = 10000;
  static constexpr uint32_t kLearningTimeoutMs = 30000;

  ButtonAction button(bool pressed, uint32_t now);
  bool expire(uint32_t now);
  void captured(uint32_t now);
  void finishLearning();
  Mode mode() const { return mode_; }

 private:
  Mode mode_ = Mode::Normal;
  bool rawPressed_ = false;
  bool pressed_ = false;
  bool consumed_ = false;
  bool suppressed_ = false;
  uint32_t changedAt_ = 0;
  uint32_t pressedAt_ = 0;
  uint32_t modeSince_ = 0;
};
