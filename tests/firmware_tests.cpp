#include <assert.h>
#include <string.h>

#include <iostream>
#include <vector>

#include "controller.h"
#include "ir_signal.h"
#include "signal_store.h"
#include "usb_protocol.h"

namespace {
ButtonAction press(Controller &controller, uint32_t now) {
  assert(controller.button(true, now) == ButtonAction::None);
  return controller.button(true, now + 30);
}
ButtonAction release(Controller &controller, uint32_t now) {
  assert(controller.button(false, now) == ButtonAction::None);
  return controller.button(false, now + 30);
}
void enterLearning(Controller &controller, uint32_t now) {
  assert(press(controller, now) == ButtonAction::None);
  assert(controller.button(true, now + 10029) == ButtonAction::None);
  assert(controller.button(true, now + 10030) == ButtonAction::BeginLearning);
  assert(controller.mode() == Mode::Learning);
}

void testButton() {
  Controller normal;
  assert(normal.button(true, 1) == ButtonAction::None);
  assert(normal.button(false, 10) == ButtonAction::None);
  assert(normal.button(false, 50) == ButtonAction::None);  // Bounce ignored.
  assert(press(normal, 100) == ButtonAction::None);
  assert(release(normal, 200) == ButtonAction::Toggle);
  assert(normal.button(false, 500) == ButtonAction::None);

  Controller learning;
  enterLearning(learning, 1);
  assert(learning.button(true, 11000) == ButtonAction::None);
  assert(release(learning, 12000) == ButtonAction::None);
  assert(press(learning, 13000) == ButtonAction::None);
  assert(release(learning, 13100) == ButtonAction::None);  // No capture yet.
  learning.captured(14000);
  assert(learning.mode() == Mode::Confirm);
  assert(press(learning, 14100) == ButtonAction::None);
  assert(release(learning, 14200) == ButtonAction::Confirm);
  learning.finishLearning();
  assert(learning.mode() == Mode::Normal);
  assert(learning.button(false, 15000) == ButtonAction::None);

  Controller held;
  enterLearning(held, 0);
  held.captured(11000);
  assert(release(held, 11100) == ButtonAction::None);
  assert(press(held, 12000) == ButtonAction::None);
  assert(release(held, 12100) == ButtonAction::Confirm);

  Controller cancelling;
  enterLearning(cancelling, 0);
  assert(release(cancelling, 10100) == ButtonAction::None);
  assert(press(cancelling, 11000) == ButtonAction::None);
  assert(cancelling.button(true, 21030) == ButtonAction::Cancel);
  assert(cancelling.mode() == Mode::Normal);
  assert(release(cancelling, 21100) == ButtonAction::None);

  Controller confirmCancel;
  enterLearning(confirmCancel, 0);
  assert(release(confirmCancel, 10100) == ButtonAction::None);
  confirmCancel.captured(10200);
  assert(press(confirmCancel, 11000) == ButtonAction::None);
  assert(confirmCancel.button(true, 21030) == ButtonAction::Cancel);
  assert(release(confirmCancel, 21100) == ButtonAction::None);

  Controller timeout;
  enterLearning(timeout, 0);
  assert(!timeout.expire(40029));
  assert(timeout.expire(40030));
  assert(release(timeout, 40100) == ButtonAction::None);

  Controller confirmTimeout;
  enterLearning(confirmTimeout, 0);
  assert(release(confirmTimeout, 10100) == ButtonAction::None);
  confirmTimeout.captured(39000);
  assert(!confirmTimeout.expire(68899));  // Confirmation gets its own 30 s.
  assert(press(confirmTimeout, 68900) == ButtonAction::None);
  assert(confirmTimeout.expire(69000));
  assert(release(confirmTimeout, 69100) == ButtonAction::None);

  // A press that is still debouncing when capture arrives cannot confirm.
  Controller debouncing;
  enterLearning(debouncing, 0);
  assert(release(debouncing, 10100) == ButtonAction::None);
  assert(debouncing.button(true, 11000) == ButtonAction::None);
  debouncing.captured(11010);
  assert(debouncing.button(true, 11030) == ButtonAction::None);
  assert(release(debouncing, 11100) == ButtonAction::None);
  assert(press(debouncing, 12000) == ButtonAction::None);
  assert(release(debouncing, 12100) == ButtonAction::Confirm);

  Controller rollover;
  uint32_t start = UINT32_MAX - 5000;
  enterLearning(rollover, start);
  assert(!rollover.expire(start + 40029));
  assert(rollover.expire(start + 40030));
}

Signal sample() {
  Signal signal;
  signal.length = 3;
  signal.pulses[0] = 9000;
  signal.pulses[1] = 4500;
  signal.pulses[2] = 560;
  return signal;
}

void testCapture() {
  uint16_t ticks[] = {50000, 4500, 2250, 280};
  Signal signal;
  assert(captureSignal(ticks, 4, 2, false, false, signal));
  assert(signal.length == 3 && signal.pulses[0] == 9000);
  assert(!captureSignal(ticks, 4, 2, true, false, signal));
  assert(!captureSignal(ticks, 4, 2, false, true, signal));
  assert(!captureSignal(ticks, 3, 2, false, false, signal));
  assert(!captureSignal(nullptr, 4, 2, false, false, signal));
  ticks[2] = 0;
  assert(!captureSignal(ticks, 4, 2, false, false, signal));
  ticks[2] = 40000;
  assert(captureSignal(ticks, 4, 2, false, false, signal));
  assert(signal.length == 5 && signal.pulses[1] == 65535 &&
         signal.pulses[2] == 0 && signal.pulses[3] == 14465);

  std::vector<uint16_t> excessive(1024, 40000);
  assert(!captureSignal(excessive.data(), excessive.size(), 2, false, false, signal));
  assert(!captureSignal(excessive.data(), 1025, 2, false, false, signal));
  signal = sample();
  signal.length = 33;
  for (unsigned i = 0; i < signal.length; ++i) signal.pulses[i] = 65535;
  assert(!validSignal(signal));  // Longer than two seconds.
  signal = sample();
  signal.length = 2;
  assert(!validSignal(signal));
  signal = sample();
  signal.pulses[1] = 0;
  assert(!validSignal(signal));
  signal = sample();
  signal.khz = 40;
  assert(!validSignal(signal));
}

struct MemoryStorage : StorageBackend {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> pending;
  bool failWrite = false;
  bool failCommit = false;
  bool corruptRead = false;
  size_t read(uint8_t *destination, size_t capacity) override {
    if (bytes.empty() || bytes.size() > capacity) return 0;
    memcpy(destination, bytes.data(), bytes.size());
    return bytes.size();
  }
  size_t readPending(uint8_t *destination, size_t capacity) override {
    if (pending.empty() || pending.size() > capacity) return 0;
    memcpy(destination, pending.data(), pending.size());
    if (corruptRead) destination[pending.size() - 1] ^= 1;
    return pending.size();
  }
  bool write(const uint8_t *source, size_t length) override {
    if (failWrite) return false;
    pending.assign(source, source + length);
    return true;
  }
  bool commit() override {
    if (failCommit) return false;
    bytes = pending;
    return true;
  }
};

void testStorage() {
  MemoryStorage backend;
  SignalStore store(backend);
  Signal loaded;
  assert(!store.load(loaded));
  Signal original = sample();
  assert(store.save(original));
  assert(store.load(loaded));
  assert(loaded.length == original.length && loaded.pulses[0] == 9000);
  // Recreate the store to simulate firmware restarting against the same NVS.
  SignalStore rebooted(backend);
  assert(rebooted.load(loaded));
  Signal replacement = sample();
  replacement.pulses[0] = 8000;
  backend.failWrite = true;
  assert(!store.save(replacement));
  assert(rebooted.load(loaded) && loaded.pulses[0] == 9000);
  backend.failWrite = false;
  backend.corruptRead = true;
  assert(!store.save(replacement));
  assert(store.load(loaded));
  assert(loaded.pulses[0] == 9000);  // Failed verification preserves selection.
  backend.corruptRead = false;
  backend.failCommit = true;
  assert(!store.save(replacement));
  assert(rebooted.load(loaded) && loaded.pulses[0] == 9000);
  backend.failCommit = false;
  backend.bytes.back() ^= 1;
  assert(!store.load(loaded));
  assert(loaded.pulses[0] == 9000);

  uint8_t record[kMaxRecordBytes];
  size_t size = encodeSignal(original, record, sizeof(record));
  assert(size == 26);
  assert(!encodeSignal(original, record, 25));
  for (size_t i = 0; i < size; ++i) {
    uint8_t saved = record[i];
    record[i] ^= 1;
    assert(!decodeSignal(record, size, loaded));
    record[i] = saved;
  }
  assert(!decodeSignal(record, size - 1, loaded));
  assert(!decodeSignal(record, size + 1, loaded));
  assert(!decodeSignal(record, kMaxRecordBytes + 1, loaded));
  assert(decodeSignal(record, size, loaded));
}

void testProtocol() {
  StaticJsonDocument<512> document;
  Request request;
  assert(parseRequest("{\"id\":1,\"cmd\":\"toggle\"}", document, request));
  assert(request.id == 1 && !strcmp(request.command, "toggle"));
  assert(parseRequest("{\"id\":0,\"cmd\":\"status\"}\r ", document, request));
  assert(parseRequest("{\"id\":2147483647,\"cmd\":\"ping\"}", document, request));
  const char *invalid[] = {
      "{broken", "[]", "null", "{\"cmd\":\"toggle\"}",
      "{\"id\":-1,\"cmd\":\"toggle\"}", "{\"id\":true,\"cmd\":\"toggle\"}",
      "{\"id\":1.0,\"cmd\":\"toggle\"}", "{\"id\":2147483648,\"cmd\":\"toggle\"}",
      "{\"id\":1,\"cmd\":null}", "{\"id\":1,\"cmd\":\"\"}",
      "{\"id\":1,\"cmd\":\"toggle\\u0000suffix\"}",
      "{\"id\":1,\"cmd\":\"toggle\"}junk",
      "{\"id\":1,\"cmd\":\"toggle\"}x",
      "{\"id\":1,\"cmd\":\"toggle\"}0",
      "{\"id\":1,\"cmd\":\"toggle\"}{\"id\":2,\"cmd\":\"toggle\"}"};
  for (const char *line : invalid) assert(!parseRequest(line, document, request));

  LineReader reader;
  for (const char c : std::string("{\"id\":2,")) assert(!reader.push(c));
  for (const char c : std::string("\"cmd\":\"ping\"}")) assert(!reader.push(c));
  assert(reader.push('\n') && !reader.overflowed());
  assert(parseRequest(reader.line(), document, request));
  assert(request.id == 2);
  for (unsigned i = 0; i < 256; ++i) assert(!reader.push('x'));
  assert(reader.push('\n') && !reader.overflowed());
  for (unsigned i = 0; i < 257; ++i) assert(!reader.push('x'));
  assert(reader.push('\n') && reader.overflowed());
  for (const char c : std::string("{\"id\":3,\"cmd\":\"status\"}")) reader.push(c);
  assert(reader.push('\n') && !reader.overflowed());
  assert(parseRequest(reader.line(), document, request) && request.id == 3);
  reader.push('\0');
  assert(reader.push('\n') && reader.overflowed());
}
}  // namespace

int main() {
  testButton();
  testCapture();
  testStorage();
  testProtocol();
  std::cout << "Firmware tests passed: button/state, capture, storage, USB framing/parser\n";
}
