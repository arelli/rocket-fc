// Flight computer firmware entry point. Everything lives in App (app.cpp).
#include <Arduino.h>

#include "app.h"

namespace {
App app;  // constructor touches no hardware; setup() drives the pyro gate low first
}

void setup() { app.setup(); }

void loop() { app.loop(); }
