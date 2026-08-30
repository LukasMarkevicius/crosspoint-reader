#ifdef SIMULATOR

#include "SimulatorHomeKeyInput.h"

#include <SDL.h>

SimulatorHomeKeyInput simulatorHomeKeyInput;

void SimulatorHomeKeyInput::update() {
  tappedThisFrame = false;
  longPressedThisFrame = false;

#ifdef SIMULATOR_DEVICE_X4_PRO
  const uint8_t* keyboardState = SDL_GetKeyboardState(nullptr);
  updateState(keyboardState != nullptr && keyboardState[SDL_SCANCODE_H], SDL_GetTicks());
#endif
}

void SimulatorHomeKeyInput::updateState(const bool pressed, const uint32_t now) {
  if (pressed && !wasPressed) {
    pressedAt = now;
    longPressReported = false;
  } else if (pressed && !longPressReported && now - pressedAt >= LONG_PRESS_MS) {
    longPressedThisFrame = true;
    longPressReported = true;
  } else if (!pressed && wasPressed && !longPressReported) {
    tappedThisFrame = true;
  }
  wasPressed = pressed;
}

#endif
