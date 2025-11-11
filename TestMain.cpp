// This code is designed to move stepper motors and sample after each movement

#include <ADS1X15.h>

// Pressure sensor configuration
ADS1115 adc0(0x48); // Address pin = 0 (ADDR to GND)
ADS1115 adc1(0x49); // Address pin = 1 (ADDR to VCC)
float pressureReadings[6]; // Store pressure values for each sensor
const unsigned long sensorInterval = 10000; // 10ms = 100Hz sampling

// Motor 1
const int stepPin1 = 19;
const int dirPin1  = 14;

// Motor 2
const int stepPin2 = 18;
const int dirPin2  = 27;

// Motor 3
const int stepPin3 = 5;
const int dirPin3  = 26;

// Motor 4
const int stepPin4 = 17;
const int dirPin4  = 25;

// Motor 5
const int stepPin5 = 16;
const int dirPin5  = 32;

// Motor 6
const int stepPin6 = 4;
const int dirPin6  = 33;

// Motor configuration
struct Motor {
  int stepPin;
  int dirPin;
};

int stepCounter;
int steps = 250;
int stepDelay = 500;  // microseconds, adjust for speed

void setup() {
  // Set all step/dir pins as outputs
  pinMode(stepPin1, OUTPUT); 
  pinMode(dirPin1, OUTPUT);
  pinMode(stepPin2, OUTPUT); 
  pinMode(dirPin2, OUTPUT);
  pinMode(stepPin3, OUTPUT); 
  pinMode(dirPin3, OUTPUT);
  pinMode(stepPin4, OUTPUT); 
  pinMode(dirPin4, OUTPUT);
  pinMode(stepPin5, OUTPUT); 
  pinMode(dirPin5, OUTPUT);
  pinMode(stepPin6, OUTPUT); 
  pinMode(dirPin6, OUTPUT);

  // Initialize ADS1115
  Wire.begin();
  adc0.begin();
  adc1.begin();
  adc0.setGain(0);
  adc1.setGain(0);
  adc0.setDataRate(7);
  adc1.setDataRate(7);

  // Initialize Serial for steady output
  Serial.begin(115200);
  // Print header once
  Serial.println("Pressure Readings:");
  Serial.println("P1\tP2\tP3\tP4\tP5\tP6");
  Serial.println("----------------------------------------");
}

void loop() {
  // Run each motor forward then backward in sequence
  moveMotor(dirPin1, stepPin1);
  readPressureSensors();
//  moveMotor(dirPin2, stepPin2);
//  readPressureSensors();
//  moveMotor(dirPin3, stepPin3);
//  readPressureSensors();
//  moveMotor(dirPin4, stepPin4);
//  readPressureSensors();
//  moveMotor(dirPin5, stepPin5);
//  readPressureSensors();
//  moveMotor(dirPin6, stepPin6);
//  readPressureSensors();
}

void moveMotor(int dirPin, int stepPin) {
  // Forward direction
  digitalWrite(dirPin, HIGH);
  for (stepCounter = 0; stepCounter < steps; stepCounter++) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(stepDelay);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(stepDelay);
  }
  delay(250);

  // Reverse direction
  digitalWrite(dirPin, LOW);
  for (stepCounter = 0; stepCounter < steps; stepCounter++) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(stepDelay);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(stepDelay);
  }
  delay(250);
}

void readPressureSensors() {
  // Read from ADC0 (Pressure 4, 5, 6)
  pressureReadings[3] = readPressure(adc0, 0); // A0
  pressureReadings[4] = readPressure(adc0, 1); // A1
  pressureReadings[5] = readPressure(adc0, 2); // A2

  // Read from ADC1 (Pressure 1, 2, 3)
  pressureReadings[0] = readPressure(adc1, 0); // A0
  pressureReadings[1] = readPressure(adc1, 1); // A1
  pressureReadings[2] = readPressure(adc1, 2); // A2

  // Output pressure values
  Serial.print("\n");
  for (int i = 0; i < 6; i++) {
    Serial.print(pressureReadings[i], 2);
    Serial.print(" psi");
    if (i < 5) Serial.print("\t");
  }
}

float readPressure(ADS1115 &adc, int channel) {
  int16_t raw = adc.readADC(channel); // Single-ended reading
  // Convert raw ADC value to voltage (assuming gain = 0, ±6.144V)
  float voltage = raw * (6.144 / 32768.0);
  // Clamp voltage to valid range (0-5V for 5V supply)
  voltage = constrain(voltage, 0.0, 5.0);
  // Convert to pressure for ABPDANV015PGAA5: P = (Vout - 0.25) / 4.5 * 15 psi
  // Offset = 0.05 * VDD (0.25V at 5V), Span = 0.9 * VDD (4.5V for 0-15 psi)
  float pressure = ((voltage - 0.45) / 4.3) * 15.0;
  // Clamp to 0-15 psi (gauge sensor, no negative pressure)
  return constrain(pressure, 0.0, 15.0);
}
