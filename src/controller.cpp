#include "controller.h"

ButtonAction Controller::button(bool pressed, uint32_t now) {
  if (pressed != rawPressed_) {
    rawPressed_ = pressed;
    changedAt_ = now;
  }
  if (pressed_ != rawPressed_ && now - changedAt_ >= kDebounceMs) {
    pressed_ = rawPressed_;
    if (pressed_) {
      pressedAt_ = now;
      consumed_ = suppressed_;
    } else {
      bool releasedAction = !consumed_;
      suppressed_ = false;
      // Only a release can toggle; a long hold never also transmits.
      consumed_ = true;
      if (releasedAction && mode_ == Mode::Normal) return ButtonAction::Toggle;
      if (releasedAction && mode_ == Mode::Confirm) return ButtonAction::Confirm;
    }
  }
  if (!pressed_ && !rawPressed_ && now - changedAt_ >= kDebounceMs)
    suppressed_ = false;
  if (pressed_ && !consumed_ && now - pressedAt_ >= kHoldMs) {
    consumed_ = true;  // Require release before another gesture.
    if (mode_ == Mode::Normal) {
      mode_ = Mode::Learning;
      modeSince_ = now;
      return ButtonAction::BeginLearning;
    }
    finishLearning();
    return ButtonAction::Cancel;
  }
  return ButtonAction::None;
}

bool Controller::expire(uint32_t now) {
  // Unsigned subtraction also works across millis() rollover.
  if (mode_ == Mode::Normal || now - modeSince_ < kLearningTimeoutMs)
    return false;
  finishLearning();
  return true;
}

void Controller::captured(uint32_t now) {
  if (mode_ != Mode::Learning) return;
  mode_ = Mode::Confirm;
  modeSince_ = now;
  // A button already held when the frame arrives cannot confirm it on release.
  if (pressed_ || rawPressed_) consumed_ = suppressed_ = true;
}

void Controller::finishLearning() {
  mode_ = Mode::Normal;
  // Do not interpret an in-progress learning gesture as a normal toggle.
  if (pressed_ || rawPressed_) consumed_ = suppressed_ = true;
}
