#include <Wire.h>
#include <ArduinoJson.h>
#include <Servo.h>

#define STEPPER_PIN1 2
#define STEPPER_PIN2 3
#define STEPPER_PIN3 4
#define STEPPER_PIN4 5

#define ENDPOINT_PIN 6
#define ENDPOINT_MOTOR 7

#define BUTTON_PIN 8

#define LEFT_ENDPOINT_PIN 9
#define ROVER_WHEEL_PIN 10

#define MAIN_ARM_SERVO_PIN 11

#define Z_ARM_UP_PIN 12
#define Z_ARM_DOWN_PIN 13

#define I2C_SLAVE_ADDR 0x08

// Custom Command
#define RESET_COMMAND "rs"    // for "reset"
#define MOVENEXT_COMMAND "mn" // for "move_next"

// Set to 1 to enable serial debugging, 0 to disable
#define SERIAL_DEBUG 1
#define ARM_TEST_MODE 1

Servo mainArmServo;

// Current step in the sequence
int stepIndex = 0;
const int stepSequence[8][4] = {
    {1, 0, 0, 0}, // Step 1
    {1, 1, 0, 0}, // Step 2
    {0, 1, 0, 0}, // Step 3
    {0, 1, 1, 0}, // Step 4
    {0, 0, 1, 0}, // Step 5
    {0, 0, 1, 1}, // Step 6
    {0, 0, 0, 1}, // Step 7
    {1, 0, 0, 1}  // Step 8
};

volatile size_t currentIndex = 0;
volatile bool receivingLength = true;
volatile bool newData = false;
volatile int roverCurrentState = 0;
volatile bool resetReceived = false;
volatile bool moveNextReceived = false;

// Define the maximum buffer size for the received data
#define BUFFER_SIZE 20
// Actual buffer size
size_t count = 0;
// Variables to hold the received data
int16_t xValues[BUFFER_SIZE];
int16_t yValues[BUFFER_SIZE];

int currentHorizontalPosition = 0;
long timer = 0;
bool isReset = false;

void setup()
{
  // Initialize I2C communication
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

// Initialize serial communication for debugging
#if SERIAL_DEBUG
  Serial.begin(9600);
  Serial.println("Arduino I2C Slave initialized");
#endif

  // Setup button pin with internal pullup
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  mainArmServo.attach(MAIN_ARM_SERVO_PIN);

  pinMode(ENDPOINT_PIN, INPUT);
  pinMode(ENDPOINT_MOTOR, OUTPUT);

  pinMode(LEFT_ENDPOINT_PIN, INPUT);

  pinMode(STEPPER_PIN1, OUTPUT);
  pinMode(STEPPER_PIN2, OUTPUT);
  pinMode(STEPPER_PIN3, OUTPUT);
  pinMode(STEPPER_PIN4, OUTPUT);

  pinMode(ROVER_WHEEL_PIN, OUTPUT);

  pinMode(Z_ARM_DOWN_PIN, OUTPUT);
  pinMode(Z_ARM_UP_PIN, OUTPUT);

  digitalWrite(Z_ARM_UP_PIN, LOW);
  digitalWrite(Z_ARM_DOWN_PIN, LOW);
  delay(1000);
}

void loop()
{
#if SERIAL_DEBUG
  Serial.println(roverCurrentState);
#endif
  roverCurrentState = 1;

  if (resetReceived)
  {
#if SERIAL_DEBUG
    Serial.println("Reset command received!");
#endif
    roverCurrentState = 2;

    currentIndex = 0;
    receivingLength = true;
    newData = false;

    delay(1000);
    resetRover();
    resetReceived = false;
    roverCurrentState = 1;
  }

  if (moveNextReceived)
  {
#if SERIAL_DEBUG
    Serial.println("Move_next command received!");
#endif
    roverCurrentState = 3;
    moveNextReceived = false;

    currentIndex = 0;
    receivingLength = true;
    newData = false;

    delay(1000);
    moveRoverForward();

    roverCurrentState = 1;
  }

  if (newData)
  {
    isReset = false;
    roverCurrentState = 0;
    startRoverOperation();
    newData = false;
    roverCurrentState = 1;
  }

  delay(50);
}

void receiveEvent(int numBytes)
{
  // Check if this might be a reset command
  if (numBytes == 2)
  {                        // Length of "rs" to indicates "reset"
    char command[3] = {0}; // Extra byte for null terminator
    int i = 0;
    while (Wire.available() && i < 2)
    {
      command[i++] = Wire.read();
    }
    command[2] = '\0';

    if (strcmp(command, RESET_COMMAND) == 0)
    {
      resetReceived = true;
      return;
    }

    if (strcmp(command, MOVENEXT_COMMAND) == 0)
    {
      moveNextReceived = true;
      return;
    }
  }

  while (Wire.available())
  {
    // Read the count of coordinate pairs (16-bit integer)
    count = Wire.read() | (Wire.read() << 8);

    // Read the coordinates
    for (size_t i = 0; i < count; i++)
    {
      if (Wire.available() >= 4)
      {
        xValues[i] = Wire.read() | (Wire.read() << 8);
        yValues[i] = Wire.read() | (Wire.read() << 8);
      }
    }
    if (count > 0)
    {
      newData = true;
    }
  }
}

void requestEvent()
{
  // Send the button state when master requests it
  Wire.write(roverCurrentState);
}

void startRoverOperation()
{
  // Process the received coordinates
#if SERIAL_DEBUG
  Serial.print("Received ");
  Serial.print(count);
  Serial.println(" coordinate pairs:");
  for (size_t i = 0; i < count; i++)
  {
    Serial.print("Point ");
    Serial.print(i + 1);
    Serial.print(": X = ");
    Serial.print(xValues[i]);
    Serial.print(", Y = ");
    Serial.println(yValues[i]);
  }
#endif

// Control logic starts from here
#if SERIAL_DEBUG
  Serial.println("Move_to_Initial_Point");
#endif
  gotoInitialServoArmPoint();
  gotoInitialStepperArmPoint();
#if SERIAL_DEBUG
  Serial.println("Move_To_Pollination_Points");
#endif
  moveToHorizontalPosition();
#if SERIAL_DEBUG
  Serial.println("Move_Rover_Forward");
#endif
  moveRoverForward();

  delay(50);
  // allowing to perform next operation
  roverCurrentState = 1;
}

void moveToHorizontalPosition()
{
#if SERIAL_DEBUG
  Serial.println("Starting arm movements");
#endif

  for (int i = 0; i < count; i++)
  {
    moveStepperLine(xValues[i]);
    moveServoAngle(map(yValues[i], 0, 40, 180, 80)); // max 180 - min 80
  }
}

void moveToHorizontalPositionTimer()
{
  currentHorizontalPosition++;
  timer = 0;
#if SERIAL_DEBUG
  Serial.println(currentHorizontalPosition);
#endif
}

void moveStepperLine(int horizontalTarget)
{
  while (currentHorizontalPosition <= horizontalTarget)
  {
    stepMotor(true);
    if (timer >= 5000) // 10,000
    {
      moveToHorizontalPositionTimer();
    }
    timer++;
    delay(1);
  }
}

void moveServoAngle(int angle)
{

  for (int i = 180; i >= angle; i--)
  {
    mainArmServo.write(i);
    delay(50);
  }

  performZAction();

  for (int i = angle; i <= 180; i++)
  {
    mainArmServo.write(i);
    delay(50);
  }
}

void performPollination()
{
  digitalWrite(ENDPOINT_MOTOR, HIGH);
  delay(3000);
  digitalWrite(ENDPOINT_MOTOR, LOW);
}

void performZAction()
{
  unsigned long startTime = millis(); // Record the start time

  // Move the arm down until the endpoint switch is triggered OR 7 seconds have passed
  // while (!digitalRead(ENDPOINT_PIN) && (millis() - startTime < 7000))
  while ((millis() - startTime < 7000))
  {
    digitalWrite(Z_ARM_DOWN_PIN, HIGH);
  }
  digitalWrite(Z_ARM_DOWN_PIN, LOW); // Stop moving down after timeout or endpoint trigger

  performPollination();

  // Move the arm up
  digitalWrite(Z_ARM_UP_PIN, HIGH);
  delay(7000);
  digitalWrite(Z_ARM_UP_PIN, LOW);

  delay(3000); // Final wait time
}

void setStepperPins(int step[4])
{
  digitalWrite(STEPPER_PIN1, step[0]);
  digitalWrite(STEPPER_PIN2, step[1]);
  digitalWrite(STEPPER_PIN3, step[2]);
  digitalWrite(STEPPER_PIN4, step[3]);
}

void stepMotor(bool direction)
{
  stepIndex = (direction) ? (stepIndex + 1) % 8 : (stepIndex - 1 + 8) % 8;
  setStepperPins(stepSequence[stepIndex]);
}

void moveRoverForward()
{
#if SERIAL_DEBUG
  Serial.println("Move rover next");
#endif
  digitalWrite(ROVER_WHEEL_PIN, HIGH);
  delay(2 * 1000); // move wheel for 2 seconds
  digitalWrite(ROVER_WHEEL_PIN, LOW);
  delay(2 * 1000);
}

void gotoInitialStepperArmPoint()
{
  while (digitalRead(LEFT_ENDPOINT_PIN) == LOW)
  {
    stepMotor(false);
    delay(1);
  }
  currentHorizontalPosition = 0;
}

void gotoInitialServoArmPoint()
{
  const int mainArmInitialPoint = 180;
  mainArmServo.write(mainArmInitialPoint);
}

void resetRover()
{
  if (isReset)
  {
    return 0;
  }
  isReset = true;

#if SERIAL_DEBUG
  Serial.println("Resetting the rover arm");
#endif

  gotoInitialStepperArmPoint();

  for (int i = 170; i >= 80; i--)
  {
    mainArmServo.write(i);
    delay(50);
  }

  for (int i = 80; i <= 170; i++)
  {
    mainArmServo.write(i);
    delay(50);
  }
}