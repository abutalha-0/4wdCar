// 4WD car control via keyboard (Bluetooth serial), see README.md for wiring.
// W = forward, S = backward, A = left, D = right, Space = stop

#include <SoftwareSerial.h>

const int BT_RX = 7; // HC-05 TXD
const int BT_TX = 8; // HC-05 RXD (through voltage divider)
SoftwareSerial bt(BT_RX, BT_TX);

const int IN1 = 2;  // Left side
const int IN2 = 3;  // Left side
const int IN3 = 4;  // Right side
const int IN4 = 5;  // Right side
const int ENA = 6;  // Left side enable (PWM)
const int ENB = 11; // Right side enable (PWM)

const int SPEED = 200; // 0-255

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

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  analogWrite(ENA, SPEED);
  analogWrite(ENB, SPEED);
  stopCar();

  bt.begin(9600);
}

void loop() {
  while (bt.available() > 0) {
    char c = bt.read();
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
      default:
        break;
    }
  }
}
