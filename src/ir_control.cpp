#include "ir_control.h"

void IrControl::begin() {
  sender_.begin();
  pinMode(32, INPUT);
  receiver_.setUnknownThreshold(12);
  // Do not enable the receiver until the user explicitly enters learning.
}

void IrControl::listen(bool enabled) {
  if (enabled) receiver_.enableIRIn();
  else receiver_.disableIRIn();
}

bool IrControl::capture(Signal &signal, bool &valid) {
  if (!receiver_.decode(&decoded_)) return false;
  valid = captureSignal(const_cast<const uint16_t *>(decoded_.rawbuf),
                        decoded_.rawlen, kRawTick, decoded_.overflow,
                        decoded_.repeat, signal);
  receiver_.resume();
  return true;
}

void IrControl::transmit(const Signal &signal) {
  // Exactly one stored frame, without library-added protocol repetitions.
  sender_.sendRaw(signal.pulses, signal.length, signal.khz);
}
