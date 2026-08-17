// 4WD car control via keyboard (Bluetooth serial), see README.md for wiring.
// Car mode:   W = forward, S = backward, A = left, D = right, Space = stop
// Servo mode: A = pan left, D = pan right, W = center, entered/exited via 'g'/'c'

#include <SoftwareSerial.h>
#include <Servo.h>

const int BT_RX = 7; // HC-05 TXD
const int BT_TX = 8; // HC-05 RXD (through voltage divider)
SoftwareSerial bt(BT_RX, BT_TX);

const int IN1 = 2;  // Left side
const int IN2 = 3;  // Left side
const int IN3 = 4;  // Right side
const int IN4 = 5;  // Right side
const int ENA = 6;  // Left side enable (PWM)
const int ENB = 11; // Right side enable (PWM)

const int SERVO_PIN = 9;
const int SERVO_STEP = 10; // degrees per keypress
Servo panServo;
int panAngle = 90; // 0-180, centered
bool servoMode = false; // toggled by the GUI's Debug button ('g'/'c')

const int TRIG_PIN = 12;
const int ECHO_PIN = 10;
const unsigned long DISTANCE_INTERVAL_MS = 200;
unsigned long lastDistanceSend = 0;

int speed = 200; // 0-255, changed via 'V' + one raw byte
bool awaitingSpeedByte = false;

void leftForward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
}

void leftBackward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
}

void leftStop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
}

void rightForward() {
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void rightBackward() {
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void rightStop() {
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void moveForward() {
  leftForward();
  rightForward();
}

void moveBackward() {
  leftBackward();
  rightBackward();
}

void turnLeft() {
  leftBackward();
  rightForward();
}

void turnRight() {
  leftForward();
  rightBackward();
}

void stopCar() {
  leftStop();
  rightStop();
}

// Returns distance in cm, or -1 if no echo was received (out of range/timeout).
long readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout, ~5m range
  if (duration == 0) return -1;
  return duration / 58;
}

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  analogWrite(ENA, speed);
  analogWrite(ENB, speed);
  stopCar();

  panServo.attach(SERVO_PIN);
  panServo.write(panAngle);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  bt.begin(9600);
}

void loop() {
  while (bt.available() > 0) {
    char c = bt.read();

    if (awaitingSpeedByte) {
      speed = (uint8_t)c;
      analogWrite(ENA, speed);
      analogWrite(ENB, speed);
      awaitingSpeedByte = false;
      continue;
    }

    if (c == 'g' || c == 'G') {
      stopCar();
      servoMode = true;
      bt.print("A:"); bt.println(panAngle);
      continue;
    }
    if (c == 'c' || c == 'C') {
      servoMode = false;
      continue;
    }

    if (servoMode) {
      switch (c) {
        // Servo is mounted reversed, so 'a' (pan left) increases the angle
        // and 'd' (pan right) decreases it.
        case 'a': case 'A':
          panAngle = min(180, panAngle + SERVO_STEP);
          panServo.write(panAngle);
          bt.print("A:"); bt.println(panAngle);
          break;
        case 'd': case 'D':
          panAngle = max(0, panAngle - SERVO_STEP);
          panServo.write(panAngle);
          bt.print("A:"); bt.println(panAngle);
          break;
        case 'w': case 'W':
          panAngle = 90;
          panServo.write(panAngle);
          bt.print("A:"); bt.println(panAngle);
          break;
        default:
          break;
      }
      continue;
    }

    switch (c) {
      case 'w': case 'W':
        moveForward();
        break;
      case 's': case 'S':
        moveBackward();
        break;
      case 'a': case 'A':
        turnLeft();
        break;
      case 'd': case 'D':
        turnRight();
        break;
      case ' ':
        stopCar();
        break;
      case 'v': case 'V':
        awaitingSpeedByte = true;
        break;
      default:
        break;
    }
  }

  if (servoMode && millis() - lastDistanceSend >= DISTANCE_INTERVAL_MS) {
    lastDistanceSend = millis();
    long cm = readDistanceCM();
    bt.print("D:");
    bt.println(cm);
  }
}
