// Out-of-line definition for DaliPort::receiveBackwardFrame.
//
// dali.h declares it `virtual ... receiveBackwardFrame(...)` (non-pure, no
// body). In the ESPHome build a subclass override is always the "key
// function" home for its own vtable, but DaliPort's own vtable/typeinfo still
// needs a home TU for RTTI (used by our mock subclasses). Provide a trivial
// definition here so the host test binary links; it is never called.

#include "dali.h"

uint8_t DaliPort::receiveBackwardFrame(unsigned long /*timeout_ms*/) {
    return 0;
}
