// Standalone pin test - sweeps A0-A5 one at a time so each can be checked
// with an LED. Not part of the car firmware: upload this instead of
// CarControl.ino to test pins, then re-upload CarControl.ino when done.
//
// Wiring for the LED:
//   LED long leg (anode)  -> the pin under test
//   LED short leg (cathode) -> resistor (220-330 ohm) -> GND
//
// The resistor is not optional. An LED straight across a pin can draw
// enough current to damage the pin - which is the opposite of helpful when
// the whole point is finding out whether a pin is already damaged.
//
// Disconnect the LCD (or anything else) from A4/A5 before running this.
// Other devices on those lines have their own pull-up resistors and will
// muddy the result.
//
// Pins are swept one at a time rather than blinked together on purpose: if
// two pins are shorted to each other, an LED in one will also light during
// the other's turn, which an all-at-once blink would hide.

const int TEST_PINS[] = {A0, A1, A2, A3, A4, A5};
const char* PIN_NAMES[] = {"A0", "A1", "A2", "A3", "A4", "A5"};
const int PIN_COUNT = sizeof(TEST_PINS) / sizeof(TEST_PINS[0]);

const unsigned long ON_MS = 500;        // how long each pin stays HIGH
const unsigned long CYCLE_GAP_MS = 1500; // all-off gap marking the end of a sweep

void setup() {
  Serial.begin(9600);
  for (int i = 0; i < PIN_COUNT; i++) {
    pinMode(TEST_PINS[i], OUTPUT);
    digitalWrite(TEST_PINS[i], LOW);
  }

  Serial.println();
  Serial.println("=== A0-A5 pin test ===");
  Serial.println("Each pin goes HIGH for 500ms in turn, then a 1.5s all-off gap.");
  Serial.println("Put the LED on one pin and watch it for a few full sweeps:");
  Serial.println("  blinks once per sweep  -> pin is fine");
  Serial.println("  never blinks           -> pin is dead (or LED is backwards)");
  Serial.println("  blinks more than once  -> shorted to another pin");
  Serial.println();
}

void loop() {
  for (int i = 0; i < PIN_COUNT; i++) {
    Serial.print("HIGH: ");
    Serial.println(PIN_NAMES[i]);

    digitalWrite(TEST_PINS[i], HIGH);
    delay(ON_MS);
    digitalWrite(TEST_PINS[i], LOW);
  }

  Serial.println("--- end of sweep ---");
  Serial.println();
  delay(CYCLE_GAP_MS);
}
