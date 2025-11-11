#include <ADS1X15.h>

// === Pressure Sensors ===
ADS1115 adc0(0x48);  // ADDR to GND
ADS1115 adc1(0x49);  // ADDR to VCC
float pressureReadings[6];
const unsigned long sensorInterval = 10000; // 10ms

// === Motors ===
struct Motor {
  const uint8_t stepPin;
  const uint8_t dirPin;
};

const Motor motors[] = {
  {19, 14},  // Motor 1
  {18, 27},  // Motor 2
  {5,  26},  // Motor 3
  {17, 25},  // Motor 4
  {16, 32},  // Motor 5
  {4,  33}   // Motor 6
};

const int NUM_MOTORS = sizeof(motors) / sizeof(motors[0]);
const int STEPS = 250;
const int STEP_DELAY_US = 500;

// === Function Prototypes ===
void moveMotor(const Motor& m);
float readPressure(ADS1115& adc, int channel);

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Initialize ADCs
  adc0.begin(); adc1.begin();
  adc0.setGain(0); adc1.setGain(0);
  adc0.setDataRate(7); adc1.setDataRate(7);

  // Setup motor pins
  for (const auto& m : motors) {
    pinMode(m.stepPin, OUTPUT);
    pinMode(m.dirPin,  OUTPUT);
  }

  // Header
  Serial.println("Pressure Readings:");
  Serial.println("P1\tP2\tP3\tP4\tP5\tP6");
  Serial.println("----------------------------------------");
}

void loop() {
  // Run only Motor 1 (uncomment others as needed)
  moveMotor(motors[0]);
  readPressureSensors();

  // Uncomment to run all motors sequentially:
  // for (int i = 0; i < NUM_MOTORS; i++) {
  //   moveMotor(motors[i]);
  //   readPressureSensors();
  //   delay(100); // Optional: small pause between motors
  // }
}

void moveMotor(const Motor& m) {
  // Forward
  digitalWrite(m.dirPin, HIGH);
  for (int i = 0; i < STEPS; i++) {
    digitalWrite(m.stepPin, HIGH);
    delayMicroseconds(STEP_DELAY_US);
    digitalWrite(m.stepPin, LOW);
    delayMicroseconds(STEP_DELAY_US);
  }
  delay(250);

  // Reverse
  digitalWrite(m.dirPin, LOW);
  for (int i = 0; i < STEPS; i++) {
    digitalWrite(m.stepPin, HIGH);
    delayMicroseconds(STEP_DELAY_US);
    digitalWrite(m.stepPin, LOW);
    delayMicroseconds(STEP_DELAY_US);
  }
  delay(250);
}

void readPressureSensors() {
  // ADC0: P4, P5, P6 (A0, A1, A2)
  pressureReadings[3] = readPressure(adc0, 0);
  pressureReadings[4] = readPressure(adc0, 1);
  pressureReadings[5] = readPressure(adc0, 2);

  // ADC1: P1, P2, P3 (A0, A1, A2)
  pressureReadings[0] = readPressure(adc1, 0);
  pressureReadings[1] = readPressure(adc1, 1);
  pressureReadings[2] = readPressure(adc1, 2);

  // Print all readings
  Serial.print('\n');
  for (int i = 0; i < 6; i++) {
    Serial.print(pressureReadings[i], 2);
    Serial.print(" psi");
    if (i < 5) Serial.print('\t');
  }
  Serial.println();
}

float readPressure(ADS1115& adc, int channel) {
  int16_t raw = adc.readADC(channel);
  float voltage = raw * (6.144f / 32768.0f);
  voltage = constrain(voltage, 0.0f, 5.0f);

  // ABPDANV015PGAA5: 0.45V @ 0 psi, 4.75V @ 15 psi → span = 4.3V
  float pressure = (voltage - 0.45f) / 4.3f * 15.0f;
  return constrain(pressure, 0.0f, 15.0f);
}
