#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "controller.h"
#include "ir_control.h"
#include "signal_store.h"
#include "usb_protocol.h"

namespace {
constexpr uint8_t kButtonPin = 39;
constexpr const char *kFirmware = "atom-turntable-ir";
constexpr uint8_t kProtocolVersion = 2;

class NvsStorage : public StorageBackend {
 public:
  bool begin() {
    ready_ = preferences_.begin("turntable", false);
    if (ready_) {
      active_ = preferences_.getUChar("active", 0);
      if (active_ > 1) ready_ = false;
    }
    return ready_;
  }
  size_t read(uint8_t *bytes, size_t capacity) override {
    return readSlot(active_, bytes, capacity);
  }
  size_t readPending(uint8_t *bytes, size_t capacity) override {
    return readSlot(1 - active_, bytes, capacity);
  }
  bool write(const uint8_t *bytes, size_t length) override {
    // An interrupted save leaves the previously selected record untouched.
    return ready_ && preferences_.putBytes(key(1 - active_), bytes, length) == length;
  }
  bool commit() override {
    if (!ready_ || preferences_.putUChar("active", 1 - active_) != 1) return false;
    active_ = 1 - active_;
    return true;
  }

 private:
  const char *key(uint8_t slot) const { return slot ? "signal1" : "signal0"; }
  size_t readSlot(uint8_t slot, uint8_t *bytes, size_t capacity) {
    if (!ready_) return 0;
    size_t length = preferences_.getBytesLength(key(slot));
    if (!length || length > capacity) return 0;
    return preferences_.getBytes(key(slot), bytes, capacity);
  }
  Preferences preferences_;
  bool ready_ = false;
  uint8_t active_ = 0;
};

Controller controller;
IrControl ir;
NvsStorage storage;
SignalStore signalStore(storage);
Signal savedSignal;
Signal candidate;
bool hasSignal = false;
bool storageReady = false;
uint32_t sentCount = 0;
LineReader input;
Adafruit_NeoPixel indicator(1, 27, NEO_GRB + NEO_KHZ800);
uint32_t feedbackSince = 0;
bool feedbackActive = false;
bool feedbackError = false;
uint32_t lastColor = UINT32_MAX;

const char *stateName() {
  switch (controller.mode()) {
    case Mode::Learning: return "learning";
    case Mode::Confirm: return "confirm";
    default: return hasSignal ? "ready" : "not_learned";
  }
}

void emit(const JsonDocument &document) {
  serializeJson(document, Serial);
  Serial.println();
}

void addStatus(JsonDocument &document) {
  document["firmware"] = kFirmware;
  document["firmware_version"] = "2.0.0";
  document["protocol_version"] = kProtocolVersion;
  document["state"] = stateName();
  document["has_signal"] = hasSignal;
  document["storage_ready"] = storageReady;
  document["pulses"] = hasSignal ? savedSignal.length : 0;
  document["carrier_khz"] = 38;
  document["sent"] = sentCount;
  document["uptime_ms"] = millis();
}

void event(const char *name, const char *reason = nullptr) {
  StaticJsonDocument<512> document;
  document["type"] = "event";
  document["event"] = name;
  if (reason) document["reason"] = reason;
  addStatus(document);
  emit(document);
}

void feedback(bool error) {
  feedbackSince = millis();
  feedbackActive = true;
  feedbackError = error;
}

const char *toggle() {
  if (controller.mode() != Mode::Normal) return "busy";
  if (!hasSignal) return "not_learned";
  ir.transmit(savedSignal);
  ++sentCount;
  feedback(false);
  return nullptr;
}

void handleLine() {
  StaticJsonDocument<512> requestDocument;
  StaticJsonDocument<768> response;
  Request request;
  response["type"] = "response";
  bool valid = !input.overflowed() &&
               parseRequest(input.line(), requestDocument, request);
  if (request.hasId) response["id"] = request.id;
  else response["id"] = nullptr;
  const char *error = nullptr;
  if (!valid) error = "invalid_request";
  else if (!strcmp(request.command, "ping")) response["event"] = "pong";
  else if (!strcmp(request.command, "status")) {
    response["event"] = "status";
    addStatus(response);
  } else if (!strcmp(request.command, "toggle")) {
    error = toggle();
    if (!error) {
      response["event"] = "sent";
      response["sent"] = sentCount;
    }
  } else error = "unknown_command";
  response["ok"] = error == nullptr;
  if (error) response["error"] = error;
  // For toggle, the response is emitted only after sendRaw has returned.
  emit(response);
}

void handleButton(ButtonAction action) {
  switch (action) {
    case ButtonAction::BeginLearning:
      candidate.length = 0;
      feedbackActive = false;
      ir.listen(true);
      event("learning_started");
      break;
    case ButtonAction::Cancel:
      ir.listen(false);
      candidate.length = 0;
      event("learning_cancelled");
      break;
    case ButtonAction::Confirm: {
      bool success = signalStore.save(candidate);
      if (success) {
        savedSignal = candidate;
        hasSignal = true;
      }
      candidate.length = 0;
      controller.finishLearning();
      feedback(!success);
      event(success ? "signal_saved" : "save_failed",
            success ? nullptr : "storage_error");
      break;
    }
    case ButtonAction::Toggle: {
      const char *error = toggle();
      if (error) feedback(true);
      event(error ? "toggle_rejected" : "sent", error);
      break;
    }
    default: break;
  }
}

void updateIndicator(uint32_t now) {
  uint32_t color;
  if (feedbackActive && now - feedbackSince >= 400) feedbackActive = false;
  if (feedbackActive) {
    color = feedbackError ? indicator.Color(24, 0, 0)
                          : ((now - feedbackSince) / 100 % 2 == 0
                                 ? indicator.Color(0, 24, 0) : 0);
  } else if (controller.mode() == Mode::Learning) {
    color = now / 400 % 2 ? indicator.Color(0, 0, 24) : 0;
  } else if (controller.mode() == Mode::Confirm) {
    color = indicator.Color(0, 0, 24);
  } else {
    color = hasSignal ? indicator.Color(0, 16, 0) : indicator.Color(20, 9, 0);
  }
  // Change only when necessary; never refresh the pixel continuously.
  if (color != lastColor) {
    indicator.setPixelColor(0, color);
    indicator.show();
    lastColor = color;
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kButtonPin, INPUT);  // GPIO39 relies on the board's external pull-up.
  indicator.begin();
  ir.begin();
  storageReady = storage.begin();
  hasSignal = storageReady && signalStore.load(savedSignal);
  updateIndicator(millis());
  event("ready", storageReady ? nullptr : "storage_error");
}

void loop() {
  uint32_t now = millis();
  if (controller.expire(now)) {
    ir.listen(false);
    candidate.length = 0;
    feedback(true);
    event("learning_timeout");
  }
  handleButton(controller.button(digitalRead(kButtonPin) == LOW, now));

  if (controller.mode() == Mode::Learning) {
    bool valid;
    if (ir.capture(candidate, valid)) {
      if (valid) {
        ir.listen(false);
        controller.captured(millis());
        event("capture_ready");
      } else event("capture_rejected", "invalid_capture");
    }
  }

  // Limit serial work to keep button timing and reception responsive.
  for (unsigned i = 0; i < 64 && Serial.available(); ++i) {
    if (input.push(static_cast<char>(Serial.read()))) handleLine();
  }
  updateIndicator(millis());
  delay(1);
}
