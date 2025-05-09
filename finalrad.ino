#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

// Define pins for each ultrasonic sensor
// US1
const int trigPin1 = 13;
const int echoPin1 = 12;
// US2
const int trigPin2 = 3;
const int echoPin2 = 2;
// US3
const int trigPin3 = 9;
const int echoPin3 = 8;

// Servo pin
const int servoPin = 10;

// SIM800L pins
const int sim800lTx = 6;
const int sim800lRx = 7;

// Number of measurements to average for US2 and US3
const int numReadings = 5;

// Variables to hold the current readings for US1 over 3 seconds
const int numUS1Readings = 30;
long us1Readings[numUS1Readings];
int us1Index = 0;
long totalUS1 = 0;
long averageUS1 = 0;
bool us1Initialized = false;

// Variable to hold the current active reading of US1
long currentDistanceUS1 = 0;
bool freezeUS1 = false;

// Servo object
Servo myServo;

// Door position degrees
int doorOpen = 120;
int doorClose = 0;
int currentServoPosition = doorClose; // Track the current servo position

// I2C LCD object
LiquidCrystal_I2C lcd(0x27, 16, 2); // Adjust the I2C address if necessary

// SIM800L object
SoftwareSerial sim800l(sim800lTx, sim800lRx);

// Variable to track if a call has been made
bool callMade = false;

// Variable to track door state
bool doorOpenState = false;

void setup() {
  // Initialize Serial communication
  Serial.begin(9600);

  // Initialize SIM800L communication
  sim800l.begin(9600);

  // Set the trig pins as outputs:
  pinMode(trigPin1, OUTPUT);
  pinMode(trigPin2, OUTPUT);
  pinMode(trigPin3, OUTPUT);

  // Set the echo pins as inputs:
  pinMode(echoPin1, INPUT);
  pinMode(echoPin2, INPUT);
  pinMode(echoPin3, INPUT);

  // Attach the servo
  myServo.attach(servoPin);
  myServo.write(doorClose); // Start with the door closed

  // Initialize US1 readings array
  for (int i = 0; i < numUS1Readings; i++) {
    us1Readings[i] = 0;
  }

  // Initialize the LCD
  lcd.init();
  lcd.backlight();

  // Set SMS mode to text
  sim800l.println("AT+CMGF=1");
  delay(100);
}

void loop() {
  // Read the average distance from US3
  long distanceUS3 = getAverageDistance(trigPin3, echoPin3);
  // Read the average distance from US2
  long distanceUS2 = getAverageDistance(trigPin2, echoPin2);

  // Determine if US1 should be frozen or unfrozen
  if (distanceUS3 < 30 && currentServoPosition != doorOpen) {
    freezeUS1 = true;
    moveServo(doorOpen, 15); // Open the door slowly
  } else if (distanceUS3 > 30 && distanceUS2 > 8 && currentServoPosition != doorClose) {
    freezeUS1 = false;
    moveServo(doorClose, 15); // Close the door slowly
  }

  // If US2 < 8 cm, ensure the door remains open even if US3 > 30
  if (distanceUS2 < 8 && currentServoPosition != doorOpen) {
    moveServo(doorOpen, 15); // Keep the door open
  }

  // Update US1 distance if not frozen
  if (!freezeUS1) {
    long newDistanceUS1 = getSingleDistance(trigPin1, echoPin1);

    // Update the running total
    totalUS1 = totalUS1 - us1Readings[us1Index] + newDistanceUS1;
    // Store the new reading in the array
    us1Readings[us1Index] = newDistanceUS1;
    // Update the index
    us1Index = (us1Index + 1) % numUS1Readings;

    // Calculate the average once the readings array is fully initialized
    if (us1Index == 0 && !us1Initialized) {
      us1Initialized = true;
    }
    if (us1Initialized) {
      currentDistanceUS1 = totalUS1 / numUS1Readings;
    } else {
      currentDistanceUS1 = totalUS1 / (us1Index + 1);
    }
  }

  // Calculate percentage for US1
  int percentageUS1 = calculatePercentage(currentDistanceUS1, 8, 28);
  // Calculate filled percentage
  int filledPercentage = 100 - percentageUS1;

  // Print the distances and percentages
  Serial.print("Distance US1: ");
  Serial.print(currentDistanceUS1);
  Serial.print(" cm (");
  Serial.print(percentageUS1);
  Serial.print("%), Filled: ");
  Serial.print(filledPercentage);
  Serial.print("%, ");

  Serial.print("Distance US2: ");
  Serial.print(distanceUS2);
  Serial.print(" cm, ");

  Serial.print("Distance US3: ");
  Serial.print(distanceUS3);
  Serial.println(" cm");

  // Display the filled percentage on the LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Filled: ");
  lcd.print(filledPercentage);
  lcd.print("%");

  // Check if filled percentage is greater than 50%
  if (filledPercentage > 50 && !callMade) {
    Serial.println("Dustbin is more than 50% filled. Making a call.");
    makeCall();
    callMade = true;
  } else if (filledPercentage <= 50 && callMade) {
    callMade = false; // Reset call flag if filled percentage goes below 50%
  }

  // Check for incoming call
  if (sim800l.available()) {
    String callData = sim800l.readString();
    if (callData.indexOf("+CLIP: \"0753907112\"") >= 0) {
      Serial.println("Call from 0753907112 detected.");
      if (!doorOpenState) {
        moveServo(doorOpen, 15); // Open the door slowly
        doorOpenState = true;
      } else {
        moveServo(doorClose, 15); // Close the door slowly
        doorOpenState = false;
      }
      hangUpCall(); // Hang up the call
    }
  }

  // Delay between measurements
  delay(100); // Take readings every 100ms, so we get 30 readings in 3 seconds
}

long getAverageDistance(int trigPin, int echoPin) {
  long sum = 0;
  for (int i = 0; i < numReadings; i++) {
    // Clear the trigPin by setting it LOW:
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);

    // Set the trigPin high for 10 microseconds to send the ultrasonic pulse:
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    // Read the signal from the echoPin, which is the duration in microseconds
    long duration = pulseIn(echoPin, HIGH);

    // Calculate the distance (in cm) based on the speed of sound.
    long distance = duration * 0.034 / 2;

    // Add to sum
    sum += distance;

    // Short delay between readings
    delay(10);
  }

  // Return the average distance
  return sum / numReadings;
}

long getSingleDistance(int trigPin, int echoPin) {
  // Clear the trigPin by setting it LOW:
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  // Set the trigPin high for 10 microseconds to send the ultrasonic pulse:
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Read the signal from the echoPin, which is the duration in microseconds
  long duration = pulseIn(echoPin, HIGH);

  // Calculate the distance (in cm) based on the speed of sound.
  long distance = duration * 0.034 / 2;

  return distance;
}

void moveServo(int position, int speedDelay) {
  if (position > currentServoPosition) {
    for (int pos = currentServoPosition; pos <= position; pos++) {
      myServo.write(pos);
      delay(speedDelay);
    }
  } else {
    for (int pos = currentServoPosition; pos >= position; pos--) {
      myServo.write(pos);
      delay(speedDelay);
    }
  }
  currentServoPosition = position; // Update current position
}

int calculatePercentage(long distance, int minRange, int maxRange) {
  if (distance < minRange) {
    return 0;
  } else if (distance > maxRange) {
    return 100;
  } else {
    return (distance - minRange) * 100 / (maxRange - minRange);
  }
}

void makeCall() {
  sim800l.println("ATD0766098815;"); // Send command to make a call
  delay(10000); // Wait for 10 seconds before hanging up
  sim800l.println("ATH"); // Hang up the call
}

void hangUpCall() {
  sim800l.println("ATH"); // Hang up any active call
  delay(1000); // Wait for a second to ensure the call is hung up
}
