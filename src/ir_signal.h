#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint16_t kPulseCapacity = 1024;
constexpr uint32_t kMaxSignalUs = 2000000;

struct Signal {
  uint16_t khz = 38;  // U002 demodulates; it cannot measure carrier frequency.
  uint16_t length = 0;
  uint16_t pulses[kPulseCapacity] = {};
};

bool validSignal(const Signal &signal);
// Input includes the receiver's leading idle gap, which must not be replayed.
bool captureSignal(const uint16_t *ticks, size_t length, uint16_t tickUs,
                   bool overflow, bool repeat, Signal &signal);

constexpr size_t kMaxRecordBytes = 16 + kPulseCapacity * 2 + 4;
size_t encodeSignal(const Signal &signal, uint8_t *bytes, size_t capacity);
bool decodeSignal(const uint8_t *bytes, size_t length, Signal &signal);
