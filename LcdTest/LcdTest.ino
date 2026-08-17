// Standalone I2C LCD test - scans the bus, then prints "Hello World" to
// whatever display it finds. Not part of the car firmware: upload this
// instead of CarControl.ino, then re-upload CarControl.ino when done.
//
// Wiring:
//   LCD VCC -> 5V
//   LCD GND -> GND
//   LCD SDA -> A4
//   LCD SCL -> A5
//
// The address is detected rather than hardcoded. Backpacks ship as 0x27 or
// 0x3F depending on the chip (PCF8574 vs PCF8574A) and the solder-jumper
// settings, so guessing wastes time - this just asks the bus.
//
// Needs the "LiquidCrystal I2C" library by Frank de Brabander:
//   Arduino IDE -> Tools -> Manage Libraries -> search "LiquidCrystal I2C"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C* lcd = nullptr;
byte lcdAddress = 0;

// Checks whether SDA/SCL are actually being pulled up by an attached module.
//
// Every I2C backpack has pull-up resistors (~4.7k) on both lines, so if the
// wires really reach the module, these pins sit HIGH on their own. If a wire
// is broken, in the wrong hole, or the module isn't powered, the pin floats
// instead. That distinguishes "wire isn't connected" from "module isn't
// responding" - two very different problems.
//
// Must run BEFORE Wire.begin(): on AVR the Wire library switches on the
// chip's own internal pull-ups, which would make a floating pin read HIGH
// and hide exactly what we're looking for.
bool checkPullup(int pin, const char* name) {
  // Drain the pin first. A floating pin holds its last charge for a moment,
  // so without this a previously-HIGH pin can read HIGH even unconnected.
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delayMicroseconds(100);

  pinMode(pin, INPUT); // plain INPUT - no internal pull-up
  delayMicroseconds(100);

  bool pulledUp = digitalRead(pin) == HIGH;
  Serial.print("  ");
  Serial.print(name);
  Serial.println(pulledUp ? ": pulled HIGH (module is connected)"
                          : ": floating (NOT connected to a powered module)");
  return pulledUp;
}

// Returns the first responding address, or 0 if the bus is silent.
byte scanI2C() {
  byte first = 0;
  int count = 0;

  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  found device at 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      if (count == 0) first = addr;
      count++;
    }
  }

  Serial.print("  devices found: ");
  Serial.println(count);
  return first;
}

void setup() {
  Serial.begin(9600);
  delay(500); // let the display's own power-on reset finish

  Serial.println();
  Serial.println("=== I2C LCD test ===");

  // Wiring check first - runs before Wire.begin() on purpose (see above).
  Serial.println("Checking SDA/SCL wiring...");
  bool sdaOk = checkPullup(A4, "SDA (A4)");
  bool sclOk = checkPullup(A5, "SCL (A5)");

  if (!sdaOk || !sclOk) {
    Serial.println();
    Serial.println("At least one data line is floating, so the Arduino isn't");
    Serial.println("electrically reaching the display. No point scanning yet.");
    Serial.println("Check, in this order:");
    Serial.println("  1. Swap the SDA and SCL wires.");
    Serial.println("  2. Try different jumper wires - a broken one is very");
    Serial.println("     common and looks exactly like this.");
    Serial.println("  3. Confirm the module has a 4-pin I2C backpack soldered");
    Serial.println("     on the back (GND/VCC/SDA/SCL). A bare 16-pin LCD with");
    Serial.println("     no backpack has no I2C at all and cannot work this way.");
    Serial.println("  4. Confirm VCC is on 5V and GND is actually connected -");
    Serial.println("     the backlight can look fine on a bad ground.");
    return;
  }

  Wire.begin();
  Serial.println();
  Serial.println("Both lines pulled up. Scanning bus...");

  lcdAddress = scanI2C();

  if (lcdAddress == 0) {
    Serial.println();
    Serial.println("Both lines are pulled up, but nothing answered.");
    Serial.println("So the wiring is good and the backpack is powered - the");
    Serial.println("chip itself isn't acknowledging. Most likely SDA and SCL");
    Serial.println("are swapped (pull-ups look identical either way, but the");
    Serial.println("chip only answers when they're the right way round).");
    Serial.println("Swap those two wires and re-run before suspecting anything else.");
    return;
  }

  Serial.println();
  Serial.print("Using 0x");
  Serial.println(lcdAddress, HEX);

  lcd = new LiquidCrystal_I2C(lcdAddress, 16, 2);
  lcd->init();
  lcd->backlight();
  lcd->setCursor(0, 0);
  lcd->print("Hello World");

  Serial.println("Sent 'Hello World'.");
  Serial.println("Blank or solid blocks? Turn the contrast pot on the back");
  Serial.println("of the backpack - text only appears in part of its range.");
}

void loop() {
  if (lcd == nullptr) return;

  // Second line counts seconds. A static message could be one lucky write;
  // a number that keeps incrementing proves the I2C link stays up.
  lcd->setCursor(0, 1);
  lcd->print("0x");
  lcd->print(lcdAddress, HEX);
  lcd->print(" up:");
  lcd->print(millis() / 1000);
  lcd->print("s   ");

  delay(1000);
}
