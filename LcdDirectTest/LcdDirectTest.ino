// Standalone test for a plain (non-I2C) 16x2 HD44780 LCD wired directly to
// the Arduino in 4-bit mode. Prints "Hello World" plus a running counter.
//
// Not part of the car firmware: upload this instead of CarControl.ino, then
// re-upload CarControl.ino when done.
//
// Why not I2C: the I2C backpack is meant to be soldered onto the LCD's
// 16-pin header (its pins map 1:1 onto them). Bridging the two through a
// breadboard means getting all 16 lines right, which is what was failing.
// Driving the LCD directly skips the backpack entirely.
//
// Uses the LiquidCrystal library, which ships with the Arduino IDE - nothing
// to install.
//
// WIRING - LCD pin (they're numbered 1-16 on the header) -> Arduino:
//
//   1  VSS  -> GND
//   2  VDD  -> 5V
//   3  V0   -> middle leg of a 10k potentiometer  <-- CONTRAST, see note
//   4  RS   -> A0
//   5  RW   -> GND            (grounded = write-only, which is all we need)
//   6  E    -> A1
//   7  D0   -> not connected  (4-bit mode uses only D4-D7)
//   8  D1   -> not connected
//   9  D2   -> not connected
//   10 D3   -> not connected
//   11 D4   -> A2
//   12 D5   -> A3
//   13 D6   -> A4
//   14 D7   -> A5
//   15 A    -> 5V through a 220 ohm resistor   (backlight +)
//   16 K    -> GND                             (backlight -)
//
// The 10k potentiometer for contrast: outer legs to 5V and GND, middle leg
// to LCD pin 3. This is not optional - with pin 3 left floating or at the
// wrong voltage the display shows either nothing or solid blocks even when
// everything else is perfect. That is the single most common reason a
// correctly-wired HD44780 looks dead. If you have no potentiometer, a fixed
// resistor (~1k) from pin 3 to GND usually lands close enough to see text.
//
// Note this consumes all six remaining free pins (A0-A5). If you later want
// those back for other sensors, solder the I2C backpack onto the LCD header
// properly - that reduces it to just A4/A5.

#include <LiquidCrystal.h>

// Constructor argument order is: RS, E, D4, D5, D6, D7
const int PIN_RS = A0;
const int PIN_E  = A1;
const int PIN_D4 = A2;
const int PIN_D5 = A3;
const int PIN_D6 = A4;
const int PIN_D7 = A5;

LiquidCrystal lcd(PIN_RS, PIN_E, PIN_D4, PIN_D5, PIN_D6, PIN_D7);

void setup() {
  Serial.begin(9600);
  Serial.println();
  Serial.println("=== Direct (4-bit) LCD test ===");
  Serial.println("Sending 'Hello World' to the LCD.");
  Serial.println();
  Serial.println("Blank screen or solid blocks means the data lines are");
  Serial.println("probably fine and it's the contrast - turn the 10k pot on");
  Serial.println("LCD pin 3 slowly through its whole range before changing");
  Serial.println("any wiring. Text only appears in part of that range.");

  lcd.begin(16, 2); // 16 columns, 2 rows
  lcd.setCursor(0, 0);
  lcd.print("Hello World");
}

void loop() {
  // A static message could be one lucky write; a counter that keeps
  // incrementing shows the LCD is still being driven correctly over time.
  lcd.setCursor(0, 1);
  lcd.print("up:");
  lcd.print(millis() / 1000);
  lcd.print("s      ");
  delay(1000);
}
