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

int stepCounter;
int steps = 2000;
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
}

void loop() {
  // Run each motor forward then backward in sequence
  moveMotor(dirPin1, stepPin1);
  moveMotor(dirPin2, stepPin2);
  moveMotor(dirPin3, stepPin3);
  moveMotor(dirPin4, stepPin4);
  moveMotor(dirPin5, stepPin5);
  moveMotor(dirPin6, stepPin6);
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
  delay(1000);

  // Reverse direction
  digitalWrite(dirPin, LOW);
  for (stepCounter = 0; stepCounter < steps; stepCounter++) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(stepDelay);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(stepDelay);
  }
  delay(1000);
}
