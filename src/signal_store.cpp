#include "signal_store.h"

#include <string.h>

bool SignalStore::load(Signal &signal) {
  size_t length = backend_.read(bytes_, sizeof(bytes_));
  return length && decodeSignal(bytes_, length, signal);
}

bool SignalStore::save(const Signal &signal) {
  size_t length = encodeSignal(signal, bytes_, sizeof(bytes_));
  if (!length || !backend_.write(bytes_, length)) return false;
  // Verify the inactive record before changing the durable active selector.
  // A failed readback leaves the old signal selected, including after reboot.
  Signal verified;
  size_t storedLength = backend_.readPending(bytes_, sizeof(bytes_));
  if (!storedLength || !decodeSignal(bytes_, storedLength, verified) ||
      verified.khz != signal.khz ||
      verified.length != signal.length) return false;
  if (memcmp(verified.pulses, signal.pulses,
             signal.length * sizeof(uint16_t)) != 0) return false;
  return backend_.commit();
}
