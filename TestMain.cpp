#include <ADS1X15.h>

// ===============================================
//  Pressure Sensors (ADS1115)
// ===============================================
ADS1115 adc0(0x48);   // ADDR to GND → P4, P5, P6
ADS1115 adc1(0x49);   // ADDR to VCC → P1, P2, P3

float pressureReadings[6];     // Live pressure values
const float TOLERANCE = 0.05;   // Stop when within ±0.5 psi

// ===============================================
//  Motor Configuration (Immutable)
// ===============================================
struct MotorConfig {
  const uint8_t stepPin;
  const uint8_t dirPin;
};

const MotorConfig motorConfigs[] = {
  {19, 14},  // Motor 1 → P1
  {18, 27},  // Motor 2 → P2
  {5,  26},  // Motor 3 → P3
  {17, 25},  // Motor 4 → P4
  {16, 32},  // Motor 5 → P5
  {4,  33}   // Motor 6 → P6
};

constexpr uint8_t NUM_MOTORS = sizeof(motorConfigs) / sizeof(motorConfigs[0]);

// ===============================================
//  Motor Runtime State (Mutable)
// ===============================================
struct MotorState {
  float targetPsi = -1.0f;  // <0 → idle
};

MotorState motorStates[NUM_MOTORS];  // One per motor

// ===============================================
//  Stepper Control Settings
// ===============================================
constexpr uint16_t STEP_DELAY_US = 500;     // Microseconds per half-step
constexpr uint8_t  STEPS_PER_ITER = 10;      // Steps per control loop (1 = fine control)

// ===============================================
//  Serial Command Handling
// ===============================================
String serialBuffer = "";
bool serialComplete = false;

// ===============================================
//  Function Prototypes
// ===============================================
float readPressure(ADS1115 &adc, uint8_t channel);
void  updatePressureReadings();
void  printPressureReadings();
void  parseCommand(const String &cmd);
void  controlMotor(uint8_t idx);

void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for serial (optional, for some boards)

  Wire.begin();

  // --- Initialize ADCs ---
  adc0.begin();  adc1.begin();
  adc0.setGain(0);   adc1.setGain(0);   // ±6.144V
  adc0.setDataRate(7); adc1.setDataRate(7); // 860 SPS

  // --- Initialize motor pins ---
  for (const auto &cfg : motorConfigs) {
    pinMode(cfg.stepPin, OUTPUT);
    pinMode(cfg.dirPin,  OUTPUT);
  }

  // --- Startup message ---
  Serial.println(F("\n=== Pressure Control System Ready ==="));
  Serial.println(F("Send: P1-5.0  → Set P1 to 5.0 psi"));
  Serial.println(F("P1\tP2\tP3\tP4\tP5\tP6"));
  Serial.println(F("----------------------------------------"));
}

void loop() {
  // --- Read serial commands ---
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) serialComplete = true;
    } else {
      serialBuffer += c;
    }
  }

  if (serialComplete) {
    parseCommand(serialBuffer);
    serialBuffer = "";
    serialComplete = false;
  }

  // --- Main control loop: 100 Hz ---
  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate >= 10) {
    lastUpdate = millis();

    updatePressureReadings();   // Refresh all sensors
    printPressureReadings();    // Live output

    // Run active motors
    for (uint8_t i = 0; i < NUM_MOTORS; ++i) {
      if (motorStates[i].targetPsi >= 0) {
        controlMotor(i);
      }
    }
  }
}

// ===============================================
//  Read single pressure sensor
// ===============================================
float readPressure(ADS1115 &adc, uint8_t channel) {
  int16_t raw = adc.readADC(channel);
  float voltage = raw * (6.144f / 32768.0f);
  voltage = constrain(voltage, 0.0f, 5.0f);

  // ABPDANV015PGAA5: 0.45V @ 0 psi, 4.75V @ 15 psi → 4.3V span
  float pressure = (voltage - 0.45f) / 4.3f * 15.0f;
  return constrain(pressure, 0.0f, 15.0f);
}

// ===============================================
//  Update all 6 pressure values
// ===============================================
void updatePressureReadings() {
  // ADC0 → P4, P5, P6
  pressureReadings[3] = readPressure(adc0, 0);
  pressureReadings[4] = readPressure(adc0, 1);
  pressureReadings[5] = readPressure(adc0, 2);

  // ADC1 → P1, P2, P3
  pressureReadings[0] = readPressure(adc1, 0);
  pressureReadings[1] = readPressure(adc1, 1);
  pressureReadings[2] = readPressure(adc1, 2);
}

// ===============================================
//  Print live pressures (tab-separated)
// ===============================================
void printPressureReadings() {
  for (uint8_t i = 0; i < 6; ++i) {
    Serial.print(pressureReadings[i], 2);
    if (i < 5) Serial.print('\t');
  }
  Serial.println();
}

// ===============================================
//  Parse command: "P1-5.0" → sensor 0, target 5.0
// ===============================================
void parseCommand(const String &cmd) {
  if (cmd.length() < 4 || cmd[0] != 'P' || cmd[2] != '-') {
    Serial.println(F("ERR: Format: P#-# (e.g. P1-5.0)"));
    return;
  }

  uint8_t sensor = cmd[1] - '1';  // '1'→0, '6'→5
  if (sensor > 5) {
    Serial.println(F("ERR: Sensor must be 1-6"));
    return;
  }

  float target = cmd.substring(3).toFloat();
  if (target < 0 || target > 15) {
    Serial.println(F("ERR: Target must be 0.0–15.0 psi"));
    return;
  }

  motorStates[sensor].targetPsi = target;

  Serial.print(F("SET: P")); Serial.print(sensor + 1);
  Serial.print(F(" → ")); Serial.print(target, 2); Serial.println(F(" psi"));
}

// ===============================================
//  Closed-loop control for one motor
// ===============================================
void controlMotor(uint8_t idx) {
  const MotorConfig &cfg = motorConfigs[idx];
  MotorState &state = motorStates[idx];
  float current = pressureReadings[idx];

  // Check if target reached
  if (fabsf(current - state.targetPsi) <= TOLERANCE) {
    state.targetPsi = -1.0f;  // Deactivate
    Serial.print(F("DONE: P")); Serial.print(idx + 1);
    Serial.print(F(" = ")); Serial.print(current, 2); Serial.println(F(" psi"));
    return;
  }

  // Determine direction
  bool forward = (current < state.targetPsi);
  digitalWrite(cfg.dirPin, forward ? HIGH : LOW);

  // Step the motor (one step per cycle)
  for (uint8_t s = 0; s < STEPS_PER_ITER; ++s) {
    digitalWrite(cfg.stepPin, HIGH);
    delayMicroseconds(STEP_DELAY_US);
    digitalWrite(cfg.stepPin, LOW);
    delayMicroseconds(STEP_DELAY_US);
  }
}
