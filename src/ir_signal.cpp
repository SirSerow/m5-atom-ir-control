#include "ir_signal.h"

namespace {
void put16(uint8_t *p, uint16_t value) {
  p[0] = value;
  p[1] = value >> 8;
}
uint16_t get16(const uint8_t *p) { return p[0] | (uint16_t(p[1]) << 8); }
void put32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = value >> (i * 8);
}
uint32_t get32(const uint8_t *p) {
  return uint32_t(get16(p)) | (uint32_t(get16(p + 2)) << 16);
}
uint32_t checksum(const uint8_t *bytes, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}
uint32_t duration(const Signal &signal) {
  uint32_t total = 0;
  for (unsigned i = 0; i < signal.length; ++i) total += signal.pulses[i];
  return total;
}
}  // namespace

bool validSignal(const Signal &signal) {
  // A complete frame starts and ends with a mark. Zero entries are allowed
  // only as phase-preserving separators when splitting a >65535 us interval.
  if (signal.khz != 38 || signal.length < 3 ||
      signal.length > kPulseCapacity || !(signal.length & 1)) return false;
  for (unsigned i = 0; i < signal.length; ++i) {
    if (!signal.pulses[i] &&
        (i == 0 || i + 1 == signal.length ||
         signal.pulses[i - 1] != UINT16_MAX || !signal.pulses[i + 1]))
      return false;
  }
  uint32_t total = duration(signal);
  return total > 0 && total <= kMaxSignalUs;
}

bool captureSignal(const uint16_t *ticks, size_t length, uint16_t tickUs,
                   bool overflow, bool repeat, Signal &signal) {
  signal.length = 0;
  signal.khz = 38;
  if (!ticks || overflow || repeat || !tickUs || length < 4 ||
      length > kPulseCapacity || length % 2 != 0) return false;
  for (size_t i = 1; i < length; ++i) {
    uint32_t micros = uint32_t(ticks[i]) * tickUs;
    if (!micros) return false;
    while (micros > UINT16_MAX) {
      if (signal.length + 2 >= kPulseCapacity) return false;
      signal.pulses[signal.length++] = UINT16_MAX;
      signal.pulses[signal.length++] = 0;
      micros -= UINT16_MAX;
    }
    if (signal.length >= kPulseCapacity) return false;
    signal.pulses[signal.length++] = micros;
  }
  return validSignal(signal);
}

size_t encodeSignal(const Signal &signal, uint8_t *bytes, size_t capacity) {
  size_t size = 16 + size_t(signal.length) * 2 + 4;
  if (!validSignal(signal) || capacity < size) return 0;
  // Explicit little-endian layout avoids compiler padding and ABI dependence.
  put32(bytes, 0x31524954);  // "TIR1"
  put16(bytes + 4, 1);      // Storage version, independent of USB version.
  put16(bytes + 6, signal.khz);
  put16(bytes + 8, signal.length);
  put16(bytes + 10, 0);
  put32(bytes + 12, duration(signal));
  for (unsigned i = 0; i < signal.length; ++i)
    put16(bytes + 16 + i * 2, signal.pulses[i]);
  put32(bytes + size - 4, checksum(bytes, size - 4));
  return size;
}

bool decodeSignal(const uint8_t *bytes, size_t length, Signal &signal) {
  if (length < 26 || length > kMaxRecordBytes ||
      get32(bytes) != 0x31524954 || get16(bytes + 4) != 1 ||
      get16(bytes + 10) != 0) return false;
  uint16_t count = get16(bytes + 8);
  if (count > kPulseCapacity || length != 16 + size_t(count) * 2 + 4 ||
      checksum(bytes, length - 4) != get32(bytes + length - 4)) return false;
  Signal candidate;
  candidate.khz = get16(bytes + 6);
  candidate.length = count;
  for (unsigned i = 0; i < count; ++i)
    candidate.pulses[i] = get16(bytes + 16 + i * 2);
  if (!validSignal(candidate) || duration(candidate) != get32(bytes + 12))
    return false;
  signal = candidate;
  return true;
}
