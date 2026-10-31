#include <Arduino.h>
#include <ArduinoJson.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <esp_system.h>
#include <errno.h>

// ATOM Lite Grove: yellow = GPIO26 (U002 TX), white = GPIO32 (U002 RX).
constexpr uint16_t kTxPin = 26;
constexpr uint16_t kRxPin = 32;
constexpr uint16_t kButtonPin = 39;
constexpr uint16_t kRawCapacity = 1024;
constexpr size_t kLineCapacity = 14000;
constexpr uint64_t kTestCode = 0xC03F12EDULL;
IRsend sender(kTxPin);
IRrecv receiver(kRxPin, kRawCapacity, 50, true);
decode_results decoded;
char line[kLineCapacity];
size_t lineLength = 0;
bool lineOverflow = false;
uint16_t learned[kRawCapacity];
uint16_t capture[kRawCapacity];
uint16_t learnedLength = 0;
bool learnedTruncated = false;
uint32_t receivedCount = 0;
uint32_t sentCount = 0;
uint32_t testDeadline = 0;
bool testPending = false;

void emit(JsonDocument &doc) {
  serializeJson(doc, Serial);
  Serial.println();
}

void error(const char *message) {
  StaticJsonDocument<192> doc;
  doc["event"] = "error";
  doc["message"] = message;
  emit(doc);
}

void status(const char *event = "status") {
  StaticJsonDocument<768> doc;
  doc["event"] = event;
  doc["firmware"] = "atom-u002-usb-ir";
  doc["version"] = "1.0.0";
  doc["uptime_ms"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["flash_bytes"] = ESP.getFlashChipSize();
  doc["reset_reason"] = static_cast<int>(esp_reset_reason());
  doc["tx_pin"] = kTxPin;
  doc["rx_pin"] = kRxPin;
  doc["rx_level"] = digitalRead(kRxPin);
  doc["button_pressed"] = digitalRead(kButtonPin) == LOW;
  doc["received"] = receivedCount;
  doc["sent"] = sentCount;
  doc["learned_pulses"] = learnedLength;
  doc["learned_truncated"] = learnedTruncated;
  doc["selftest_pending"] = testPending;
  emit(doc);
}

bool integer(JsonVariantConst value, uint32_t low, uint32_t high) {
  return value.is<uint32_t>() && value.as<uint32_t>() >= low &&
         value.as<uint32_t>() <= high;
}

void sent(const char *kind) {
  sentCount++;
  StaticJsonDocument<192> doc;
  doc["event"] = "sent";
  doc["kind"] = kind;
  doc["sent"] = sentCount;
  emit(doc);
}

void command(const char *input) {
  DynamicJsonDocument doc(24576);
  if (deserializeJson(doc, input)) {
    error("Invalid JSON");
    return;
  }
  const char *cmd = doc["cmd"] | "";
  if (!strcmp(cmd, "ping")) {
    StaticJsonDocument<64> reply;
    reply["event"] = "pong";
    emit(reply);
  } else if (!strcmp(cmd, "status")) {
    status();
  } else if (!strcmp(cmd, "help")) {
    StaticJsonDocument<256> reply;
    reply["event"] = "help";
    reply["commands"] = "ping,status,send,raw,replay,selftest,help";
    emit(reply);
  } else if (!strcmp(cmd, "selftest")) {
    if (testPending) { error("Self-test already running"); return; }
    // Keep reception enabled so optical feedback can confirm both paths.
    receiver.resume();
    testPending = true;
    testDeadline = millis() + 700;
    sender.sendNEC(kTestCode, 32);
    sent("selftest");
  } else if (!strcmp(cmd, "send")) {
    if (testPending) { error("Self-test running"); return; }
    const char *protocolName = doc["protocol"] | "";
    decode_type_t protocol = strToDecodeType(protocolName);
    if (protocol == UNKNOWN || hasACState(protocol)) {
      error("Unsupported scalar protocol; use raw for state-based A/C messages");
      return;
    }
    if (!doc["code"].is<const char *>()) {
      error("code must be a hexadecimal string"); return;
    }
    const char *codeText = doc["code"];
    size_t start = (codeText[0] == '0' && (codeText[1] == 'x' || codeText[1] == 'X')) ? 2 : 0;
    size_t digits = strlen(codeText + start);
    if (!digits || digits > 16) { error("code must contain 1 to 16 hex digits"); return; }
    for (size_t i = start; codeText[i]; i++) {
      if (!isxdigit(static_cast<unsigned char>(codeText[i]))) {
        error("Invalid hexadecimal code"); return;
      }
    }
    errno = 0;
    uint64_t code = strtoull(codeText, nullptr, 16);
    if (errno) { error("Code exceeds 64 bits"); return; }
    if (doc.containsKey("bits") && !integer(doc["bits"], 1, 64)) {
      error("bits must be an integer from 1 to 64"); return;
    }
    uint16_t bits = doc["bits"] | IRsend::defaultBits(protocol);
    if (!bits || bits > 64 || (bits < 64 && (code >> bits))) {
      error("Code does not fit the selected bit count"); return;
    }
    if (doc.containsKey("repeat") && !integer(doc["repeat"], 0, 10)) {
      error("repeat must be an integer from 0 to 10"); return;
    }
    uint16_t repeat = doc["repeat"] | 0;
    receiver.disableIRIn();
    bool ok = sender.send(protocol, code, bits, repeat);
    receiver.enableIRIn();
    if (ok) sent("protocol"); else error("Protocol sending is unavailable");
  } else if (!strcmp(cmd, "raw") || !strcmp(cmd, "replay")) {
    if (testPending) { error("Self-test running"); return; }
    uint16_t frequency = 38;
    if (doc.containsKey("khz")) {
      if (!integer(doc["khz"], 30, 60)) { error("khz must be 30 to 60"); return; }
      frequency = doc["khz"];
    }
    uint16_t pulses[kRawCapacity];
    const uint16_t *data = learned;
    uint16_t length = learnedLength;
    if (!strcmp(cmd, "raw")) {
      JsonArrayConst raw = doc["pulses"].as<JsonArrayConst>();
      if (raw.isNull() || !raw.size() || raw.size() > kRawCapacity) {
        error("pulses must contain 1 to 1024 durations in microseconds"); return;
      }
      length = raw.size();
      uint32_t total = 0;
      for (uint16_t i = 0; i < length; i++) {
        if (!integer(raw[i], 0, 65535)) { error("Each pulse must be 0 to 65535 us"); return; }
        pulses[i] = raw[i];
        total += pulses[i];
      }
      if (total > 2000000) { error("Raw transmission exceeds two seconds"); return; }
      data = pulses;
    } else if (!length || learnedTruncated) {
      error("No complete frame learned; receive a remote button first"); return;
    }
    uint32_t duration = 0;
    for (uint16_t i = 0; i < length; i++) duration += data[i];
    if (!duration || duration > 2000000) { error("Transmission must last 1 us to two seconds"); return; }
    receiver.disableIRIn();
    sender.sendRaw(data, length, frequency);
    receiver.enableIRIn();
    sent(cmd);
  } else {
    error("Unknown command; use help");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(kButtonPin, INPUT);
  // No pull-up: the U002 receiver drives this line, and a disconnected input
  // must not be presented as proof that the unit is attached.
  pinMode(kRxPin, INPUT);
  sender.begin();
  receiver.setUnknownThreshold(12);
  receiver.enableIRIn();
  delay(150);
  status("ready");
}

void loop() {
  // Bound work per iteration so sustained USB traffic cannot starve reception.
  for (uint16_t i = 0; i < 256 && Serial.available(); i++) {
    char c = Serial.read();
    if (c == '\n') {
      if (lineOverflow) error("Command exceeds 13999 bytes");
      else if (lineLength) { line[lineLength] = '\0'; command(line); }
      lineLength = 0;
      lineOverflow = false;
    } else if (c != '\r') {
      if (lineLength < kLineCapacity - 1) line[lineLength++] = c;
      else lineOverflow = true;
    }
  }
  if (receiver.decode(&decoded)) {
    receivedCount++;
    bool truncated = decoded.overflow;
    uint16_t captureLength = 0;
    auto appendPulse = [&](uint16_t pulse) {
      if (captureLength >= kRawCapacity) { truncated = true; return false; }
      capture[captureLength++] = pulse;
      return true;
    };
    // Exclude the initial idle gap. Split long intervals with a zero-duration
    // opposite phase, preserving alternating mark/space semantics for sendRaw.
    for (uint16_t i = 1; i < decoded.rawlen; i++) {
      uint32_t micros = decoded.rawbuf[i] * kRawTick;
      bool room = true;
      while (micros > UINT16_MAX && room) {
        room = appendPulse(UINT16_MAX) && appendPulse(0);
        micros -= UINT16_MAX;
      }
      if (!room || !appendPulse(micros)) break;
    }
    if (!decoded.repeat) {
      learnedTruncated = truncated;
      learnedLength = captureLength;
      memcpy(learned, capture, learnedLength * sizeof(uint16_t));
    }
    DynamicJsonDocument event(24576);
    event["event"] = "received";
    event["protocol"] = typeToString(decoded.decode_type);
    event["code"] = uint64ToString(decoded.value, 16);
    event["bits"] = decoded.bits;
    event["repeat"] = decoded.repeat;
    event["overflow"] = decoded.overflow;
    event["truncated"] = truncated;
    event["khz"] = 38;
    JsonArray pulses = event.createNestedArray("pulses");
    for (uint16_t i = 0; i < captureLength; i++) pulses.add(capture[i]);
    bool matched = testPending && decoded.decode_type == NEC && decoded.value == kTestCode;
    receiver.resume();
    emit(event);
    if (matched) {
      testPending = false;
      StaticJsonDocument<160> result;
      result["event"] = "selftest";
      result["optical_loopback"] = true;
      emit(result);
    }
  }
  if (testPending && static_cast<int32_t>(millis() - testDeadline) >= 0) {
    testPending = false;
    StaticJsonDocument<192> result;
    result["event"] = "selftest";
    result["optical_loopback"] = false;
    result["message"] = "No matching optical feedback; test with a remote or reflective surface";
    emit(result);
  }
  delay(1);
}
