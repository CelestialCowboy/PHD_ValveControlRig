#include <ADS1X15.h>
// ===============================================
// Pressure Sensors (ADS1115)
// ===============================================
ADS1115 adc0(0x48); // ADDR to GND → P4, P5, P6
ADS1115 adc1(0x49); // ADDR to VCC → P1, P2, P3
float pressureReadings[6];
const float TOLERANCE = 0.1;

// ===============================================
// Motor Configuration
// ===============================================
struct MotorConfig {
  const uint8_t stepPin;
  const uint8_t dirPin;
};
const MotorConfig motorConfigs[] = {
  {19, 14}, // P1
  {18, 27}, // P2
  {5,  26}, // P3
  {17, 25}, // P4
  {16, 32}, // P5
  {4,  33}  // P6
};
constexpr uint8_t NUM_MOTORS = sizeof(motorConfigs) / sizeof(motorConfigs[0]);

struct MotorState {
  float targetPsi = -1.0f;
  int manualSteps = 0;      // NEW: for manual stepping
  bool manualActive = false;
};
MotorState motorStates[NUM_MOTORS];

constexpr uint16_t STEP_DELAY_US = 500;
constexpr int STEPS_PER_ITER = 10;

// ===============================================
// Serial handling
// ===============================================
String serialBuffer = "";
bool serialComplete = false;

// ===============================================
// Prototypes
// ===============================================
float readPressure(ADS1115 &adc, uint8_t channel);
void updatePressureReadings();
void printPressureReadings();
void parseCommand(const String &cmd);
void stopAllMotors();
void controlMotor(uint8_t idx);
void moveMotorSteps(uint8_t idx, int steps);  // NEW

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Wire.begin();
  adc0.begin(); adc1.begin();
  adc0.setGain(0); adc1.setGain(0);
  adc0.setDataRate(7); adc1.setDataRate(7);

  for (const auto &cfg : motorConfigs) {
    pinMode(cfg.stepPin, OUTPUT);
    pinMode(cfg.dirPin, OUTPUT);
  }

  Serial.println(F("\n=== Pressure Control Ready ==="));
  Serial.println(F("Commands:"));
  Serial.println(F(" P#-<psi>   e.g. P1-5.0 (0.25–12.5 psi)"));
  Serial.println(F(" M#±<steps> e.g. M1+100, M3-250"));
  Serial.println(F(" stop       → stop all motors"));
  Serial.println(F("P1\tP2\tP3\tP4\tP5\tP6"));
  Serial.println(F("----------------------------------------"));
}

void loop() {
  // ----- Serial input -----
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length()) serialComplete = true;
    } else {
      serialBuffer += c;
    }
  }

  if (serialComplete) {
    parseCommand(serialBuffer);
    serialBuffer = "";
    serialComplete = false;
  }

  // ----- 100 Hz control loop -----
  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate >= 10) {
    lastUpdate = millis();

    updatePressureReadings();
    printPressureReadings();

    for (uint8_t i = 0; i < NUM_MOTORS; ++i) {
      // Pressure control
      if (motorStates[i].targetPsi >= 0) {
        controlMotor(i);
      }
      // Manual step control
      else if (motorStates[i].manualActive && motorStates[i].manualSteps != 0) {
        int steps = min(abs(motorStates[i].manualSteps), STEPS_PER_ITER);  // ← now safe
        bool forward = (motorStates[i].manualSteps > 0);
        moveMotorSteps(i, forward ? steps : -steps);
        motorStates[i].manualSteps -= (forward ? steps : -steps);
        if (motorStates[i].manualSteps == 0) {
          motorStates[i].manualActive = false;
          Serial.print(F("DONE: M")); Serial.print(i + 1);
          Serial.println(F(" manual move complete"));
        }
      }
    }
  }
}

// ===============================================
// Pressure conversion
// ===============================================
float readPressure(ADS1115 &adc, uint8_t channel) {
  int16_t raw = adc.readADC(channel);
  float voltage = raw * (6.144f / 32768.0f);
  voltage = constrain(voltage, 0.0f, 5.0f);
  float pressure = (voltage - 0.45f) / 4.3f * 15.0f;
  return constrain(pressure, 0.0f, 15.0f);
}

void updatePressureReadings() {
  pressureReadings[3] = readPressure(adc0, 0);
  pressureReadings[4] = readPressure(adc0, 1);
  pressureReadings[5] = readPressure(adc0, 2);
  pressureReadings[0] = readPressure(adc1, 0);
  pressureReadings[1] = readPressure(adc1, 1);
  pressureReadings[2] = readPressure(adc1, 2);
}

void printPressureReadings() {
  for (uint8_t i = 0; i < 6; ++i) {
    Serial.print(pressureReadings[i], 2);
    if (i < 5) Serial.print('\t');
  }
  Serial.println();
}

// ===============================================
// Command parser – now handles "M#±steps"
// ===============================================
void parseCommand(const String &cmd) {
  String lower = cmd;
  lower.trim();
  lower.toLowerCase();

  if (lower == "stop") {
    stopAllMotors();
    return;
  }

  // === Manual Motor Move: M#±steps ===
  if (cmd.length() >= 4 && cmd[0] == 'M' && (cmd[2] == '+' || cmd[2] == '-')) {
    uint8_t motor = cmd[1] - '1';
    if (motor > 5) {
      Serial.println(F("ERR: Motor must be 1-6"));
      return;
    }
    int steps = cmd.substring(3).toInt();
    if (steps == 0) {
      Serial.println(F("ERR: Steps must be non-zero"));
      return;
    }
    bool forward = (cmd[2] == '+');
    motorStates[motor].manualSteps = forward ? steps : -steps;
    motorStates[motor].manualActive = true;
    motorStates[motor].targetPsi = -1.0f; // cancel pressure mode

    Serial.print(F("MOV: M")); Serial.print(motor + 1);
    Serial.print(forward ? F(" +") : F(" -")); Serial.print(abs(steps));
    Serial.println(F(" steps"));
    return;
  }

  // === Pressure Set: P#-<psi> ===
  if (cmd.length() < 4 || cmd[0] != 'P' || cmd[2] != '-') {
    Serial.println(F("ERR: Format: P#-# or M#±steps"));
    return;
  }

  uint8_t sensor = cmd[1] - '1';
  if (sensor > 5) {
    Serial.println(F("ERR: Sensor must be 1-6"));
    return;
  }

  float target = cmd.substring(3).toFloat();
  if (target < 0.25f || target > 12.5f) {
    Serial.println(F("ERR: Target must be 0.25–12.5 psi"));
    return;
  }

  motorStates[sensor].targetPsi = target;
  motorStates[sensor].manualActive = false; // cancel manual
  motorStates[sensor].manualSteps = 0;

  Serial.print(F("SET: P")); Serial.print(sensor + 1);
  Serial.print(F(" to ")); Serial.print(target, 2); Serial.println(F(" psi"));
}

// ===============================================
// STOP ALL MOTORS
// ===============================================
void stopAllMotors() {
  for (uint8_t i = 0; i < NUM_MOTORS; ++i) {
    motorStates[i].targetPsi = -1.0f;
    motorStates[i].manualSteps = 0;
    motorStates[i].manualActive = false;
    const auto &cfg = motorConfigs[i];
    digitalWrite(cfg.stepPin, LOW);
    digitalWrite(cfg.dirPin, LOW);
  }
  Serial.println(F("STOP: All motors halted"));
}

// ===============================================
// Closed-loop pressure control
// ===============================================
void controlMotor(uint8_t idx) {
  const MotorConfig &cfg = motorConfigs[idx];
  MotorState &state = motorStates[idx];
  float current = pressureReadings[idx];

  if (fabsf(current - state.targetPsi) <= TOLERANCE) {
    state.targetPsi = -1.0f;
    Serial.print(F("DONE: P")); Serial.print(idx + 1);
    Serial.print(F(" = ")); Serial.print(current, 2); Serial.println(F(" psi"));
    return;
  }

  bool forward = (current < state.targetPsi);
  digitalWrite(cfg.dirPin, forward ? HIGH : LOW);

  for (uint8_t s = 0; s < STEPS_PER_ITER; ++s) {
    digitalWrite(cfg.stepPin, HIGH);
    delayMicroseconds(STEP_DELAY_US);
    digitalWrite(cfg.stepPin, LOW);
    delayMicroseconds(STEP_DELAY_US);
  }
}

// ===============================================
// NEW: Manual step mover (called from loop)
// ===============================================
void moveMotorSteps(uint8_t idx, int steps) {
  if (steps == 0) return;
  const MotorConfig &cfg = motorConfigs[idx];
  bool forward = (steps > 0);
  int count = abs(steps);

  digitalWrite(cfg.dirPin, forward ? HIGH : LOW);
  for (int i = 0; i < count; ++i) {
    digitalWrite(cfg.stepPin, HIGH);
    delayMicroseconds(STEP_DELAY_US);
    digitalWrite(cfg.stepPin, LOW);
    delayMicroseconds(STEP_DELAY_US);
  }
}