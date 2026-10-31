#pragma once

#include <IRrecv.h>
#include <IRsend.h>

#include "ir_signal.h"

class IrControl {
 public:
  void begin();
  void listen(bool enabled);
  // Returns true when a frame was received; valid distinguishes rejected data.
  bool capture(Signal &signal, bool &valid);
  void transmit(const Signal &signal);

 private:
  IRsend sender_{12};  // ATOM Lite onboard emitter; U002 TX is unused.
  IRrecv receiver_{32, kPulseCapacity, 50, true};
  decode_results decoded_{};
};
