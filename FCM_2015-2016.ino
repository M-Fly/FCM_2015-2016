//M-Fly Flight Control Software

// NOTE: Servos do work, but interrupt input from
// receiver frequently drops, causing servos to jitter
// uncontrollably

// NOTE: Altitude Measurement in Meters

#include <XBee.h>
#include <stdlib.h>

#include <Adafruit_Sensor.h>

#include <Adafruit_BMP085_U.h>
#include <Adafruit_LSM303_U.h>//Accel/Magneto sensor library
#include <Adafruit_L3GD20_U.h>//Gyroscope library
#include <SFE_BMP180.h>

#include "Data.h"
#include <Wire.h>

#include <Servo.h>

Data *data;

volatile uint8_t numDropped = 0;

// Hertz Rate for Data Collection
const int hertz = 5;
const int delayTime = 1000 / hertz;

const long debounceTime = 20000;

// Variables for Data Collection
float batteryVoltage = 9;

//Interrupt Pin
const int interruptPin = 3;
volatile long lastTime = 0; 
volatile long pulseWidth = 0;

volatile bool doorOpen = false;

Servo doorServo;
const int doorServoPin = 9;

Servo releaseServo1;
const int releaseServo1Pin = 11;

Servo releaseServo2;
const int releaseServo2Pin = 10;

//PWM min - 870 PWM max - 2100

const int PWM_MAX = 1960;
const int PWM_MIN = 1250;

const int ledPin = 13;
byte ledState = 0;

bool dropped = false;

const int DOOR_SERVO_START = 75;
const int B_MSG_REPEAT_ATTEMPTS = 25;
const int RELEASE_SERVO_START = 15;

// XBee Setup

XBee xbee;
XBeeAddress64 broadcast = XBeeAddress64(0x00000000, 0x0000ffff);

void setup() {  
  // Initiate Serial Port
  Serial.begin(9600);
  xbee.begin(Serial);
  
  // Create Data class instance
  data = new Data();
  data->update();

  // Initiate Servos

  doorServo.attach(doorServoPin);
  doorServo.write(DOOR_SERVO_START);
  
  releaseServo1.attach(releaseServo1Pin);
  releaseServo1.write(180 - RELEASE_SERVO_START);
  
  releaseServo2.attach(releaseServo2Pin);
  releaseServo2.write(RELEASE_SERVO_START);
  
  pinMode(ledPin, OUTPUT);

  //attach interupts as the last task in setup
  pinMode(interruptPin, INPUT); //set interruptPin as an input
  digitalWrite(interruptPin,LOW); //set pin to Low initially
  attachInterrupt(digitalPinToInterrupt(interruptPin), rising, RISING); //execute interrupt when voltage drops
}

long lastLoopTime = 0;
int lastAlt = 0;

void loop() {
  data->update();
  
  // Disable interrupts to get volatile parameters
  noInterrupts();
  
  bool localDropped = dropped;
  int localWidth = pulseWidth;
  bool localDoorOpen = doorOpen;
  
  interrupts();
  
  // Update Servos after 1000 milliseconds to ensure no false
  // readings from the receiver  
  if (millis() > (long)1000)
    updateDropAndDoorServos(localWidth);


  if (!localDoorOpen) {
    lastAlt = data->getAltitude();
  }

  // Blink LED and Write Data to Serial regularly
  if (millis() - lastLoopTime > delayTime) {
    lastLoopTime = millis();
    ledState = !ledState;
    digitalWrite(ledPin, ledState);
    
    if (dropped) writeDataB();
    else writeData();
  }
}

void writeDataB() {
  static int drop_repeat = 0;
  static int drop_alt = 0;
  static int num_dropped_old = 0;
  static char bBuffer[32];
  
  if (drop_repeat == 0 || num_dropped_old != numDropped) {
    drop_alt = lastAlt;
    drop_repeat = B_MSG_REPEAT_ATTEMPTS;

    num_dropped_old = numDropped;

    // B,TIME,DROPPED,ALT,
    
    String bMessage = "B,";
    bMessage += millis() / 1000.0f;
    bMessage += ',';
    bMessage += numDropped;
    bMessage += ',';
    bMessage += drop_alt;
    bMessage += ',';
    
    bMessage.toCharArray(bBuffer, bMessage.length());
  }
  
  ZBTxRequest zbtx = ZBTxRequest(broadcast, (uint8_t *) bBuffer, strlen(bBuffer));
  xbee.send(zbtx);

  // Reset Drop Repeat to ensure that the message gets repeated as much as it needs to.
  if (drop_repeat > 0) {
    --drop_repeat;

    if (drop_repeat == 0) dropped = false;
  }
}

void writeData() {
  static char csvBuffer[64];
  
  // Write telemetry to serial
  float batteryVoltage = readVcc() / 1000.0f;
  
  float time = millis() / 1000.0f;

  // A,M-Fly,TIME,ALTITUDE,GYROX,GYROY,GYROZ,AIRSPEED,VOLTAGE,DROPPED,ACCELX,ACCELY,ACCELZ,
  
  String message = "A,M-Fly,";
  message += time;
  message += ',';
  message += lastAlt;
  message += ',';
  message += data->getGyroX();
  message += ',';
  message += data->getGyroY();
  message += ',';
  message += data->getGyroZ();
  message += ',';
  message += random(10,40);
  message += ',';
  message += batteryVoltage;
  message += ',';
  message += numDropped;
  message += ',';
  message += data->getAccelX();
  message += ',';
  message += data->getAccelY();
  message += ',';
  message += data->getAccelZ();
  
  message.toCharArray(csvBuffer, message.length());
  
  ZBTxRequest zbtx = ZBTxRequest(broadcast, (uint8_t *) csvBuffer, strlen(csvBuffer));
  xbee.send(zbtx);
}

void updateDropAndDoorServos(int localPulseWidth) {
  const int MAX = 255;

  // Map value from receiver to usable values
  int val = map(localPulseWidth, PWM_MIN, PWM_MAX, 0, MAX);
  val = constrain(val, 0, MAX);
  
  // TODO: Possibly change integer division to floating division
  // based on testing parameters
  
  static int doorWrite = DOOR_SERVO_START;
  int newDoorWrite = DOOR_SERVO_START;

  doorOpen = false;
   
  // Open door if val > MAX / 4
  if (val > MAX / 4.0f) {
    newDoorWrite = 180;
    doorOpen = true;
  }
  
  // Write to door servos
  if (doorWrite != newDoorWrite) {
    doorWrite = newDoorWrite;
    doorServo.write(doorWrite);
  }
  
  // hold values to write to servo
  static int releaseWrite1 = 0;
  int newReleaseWrite1 = RELEASE_SERVO_START;

  static int releaseWrite2 = 0;
  int newReleaseWrite2 = RELEASE_SERVO_START;
  
  // Drop payloads as specified
  
  if (val > MAX * 0.75f) {  
    newReleaseWrite1 = 160;
    newReleaseWrite2 = 180;

    if (!dropped) {
      ++numDropped;
      dropped = true;
    }
  } else if (val > MAX * 0.5f) {
    newReleaseWrite1 = 110;
    newReleaseWrite2 = 110;
    
    if (numDropped < 1) {
      numDropped = 1;
      dropped = true;
    }
  }
  
  // Only write to servo if necessary to avoid jitter
  if (releaseWrite1 != newReleaseWrite1) {
    releaseWrite1 = newReleaseWrite1;
    releaseServo1.write(180 - releaseWrite1);
  }

  if (releaseWrite2 != newReleaseWrite2) {
    releaseWrite2 = newReleaseWrite2;
    releaseServo2.write(releaseWrite2);
  }
}

// Gets Vcc Voltage of Battery
// From: https://code.google.com/p/tinkerit/wiki/SecretVoltmeter
long readVcc() {
  long result;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result; // Back-calculate AVcc in mV
  return result;
}

// Interrupt for rising edge
void rising(){
  if (micros() - lastTime > debounceTime) {
    lastTime = micros();
  
    detachInterrupt(digitalPinToInterrupt(interruptPin));
    attachInterrupt(digitalPinToInterrupt(interruptPin), drop, FALLING);
  }
}

// Interrupt for falling edge
void drop(){ //writes the drop agle to the servo and logs current flight data at that instant
  pulseWidth = micros() - lastTime;
  pulseWidth = constrain(pulseWidth, PWM_MIN, PWM_MAX);
  
  detachInterrupt(digitalPinToInterrupt(interruptPin));
  attachInterrupt(digitalPinToInterrupt(interruptPin), rising, RISING);
}

