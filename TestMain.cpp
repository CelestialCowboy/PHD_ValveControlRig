// This code is designed to test if the stepper motors can be controlled simultaneously but independantly
// of the pressure sensor monitoring

#include <ADS1X15.h>

// Motor configuration
struct Motor {
  int stepPin;
  int dirPin;
};

Motor motors[] = {
  {19, 14}, // Motor 1
  {18, 27}, // Motor 2
  {5, 26},  // Motor 3
  {17, 25}, // Motor 4
  {16, 32}, // Motor 5
  {4, 33}   // Motor 6
};

const int numMotors = 6;
const int steps = 2000;
const int stepDelay = 500; // microseconds per step half-cycle

// Pressure sensor configuration
ADS1115 adc0(0x48); // Address pin = 0 (ADDR to GND)
ADS1115 adc1(0x49); // Address pin = 1 (ADDR to VCC)
float pressureReadings[6]; // Store pressure values for each sensor
const unsigned long sensorInterval = 10000; // 10ms = 100Hz sampling

// Motor state
struct MotorState {
  bool active;
  int currentStep;
  bool direction; // true = forward, false = reverse
  unsigned long lastStepTime;
};

MotorState motorStates[numMotors];
unsigned long lastSensorTime = 0;

void setup() {
  // Initialize motor pins
  for (int i = 0; i < numMotors; i++) {
    pinMode(motors[i].stepPin, OUTPUT);
    pinMode(motors[i].dirPin, OUTPUT);
    motorStates[i] = {true, 0, true, 0}; // Start active, forward
  }

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
  unsigned long currentTime = micros();

  // Update motor states
  for (int i = 0; i < numMotors; i++) {
    if (motorStates[i].active) {
      updateMotor(i, currentTime);
    }
  }

  // Read pressure sensors at high frequency
  if (currentTime - lastSensorTime >= sensorInterval) {
    readPressureSensors();
    lastSensorTime = currentTime;
  }
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

  // Steady output: Overwrite the same line
  Serial.print("\n"); // Move cursor to start of line
  for (int i = 0; i < 6; i++) {
    Serial.print(pressureReadings[i], 2); // 2 decimal places
    Serial.print(" psi");
    if (i < 5) Serial.print("\t"); // Tab between values
  }
}

void updateMotor(int motorIndex, unsigned long currentTime) {
  MotorState &state = motorStates[motorIndex];
  Motor &motor = motors[motorIndex];

  // Check if it's time for the next step
  if (currentTime - state.lastStepTime >= stepDelay) {
    if (state.currentStep < steps) {
      // Perform a step
      digitalWrite(motor.stepPin, HIGH);
      delayMicroseconds(10); // Short pulse
      digitalWrite(motor.stepPin, LOW);
      state.currentStep++;
      state.lastStepTime = currentTime;
    } else {
      // Switch direction or move to next motor
      if (state.direction) {
        // Just finished forward, switch to reverse
        state.direction = false;
        state.currentStep = 0;
        digitalWrite(motor.dirPin, LOW);
        delay(1000); // Maintain 1-second pause (blocking, but short)
      } else {
        // Just finished reverse, deactivate motor
        state.active = false;
        // Activate next motor if available
        if (motorIndex + 1 < numMotors) {
          motorStates[motorIndex + 1] = {true, 0, true, currentTime};
          digitalWrite(motors[motorIndex + 1].dirPin, HIGH);
        }
      }
    }
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
