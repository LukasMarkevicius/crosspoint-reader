#pragma once

#ifdef SIMULATOR

#include <cstdint>

class SimulatorHomeKeyInput {
 public:
  void update();

  bool wasTapped() const { return tappedThisFrame; }
  bool wasLongPressed() const { return longPressedThisFrame; }

 private:
  static constexpr uint32_t LONG_PRESS_MS = 700;

  void updateState(bool pressed, uint32_t now);

  bool wasPressed = false;
  bool longPressReported = false;
  bool tappedThisFrame = false;
  bool longPressedThisFrame = false;
  uint32_t pressedAt = 0;
};

extern SimulatorHomeKeyInput simulatorHomeKeyInput;

#endif
