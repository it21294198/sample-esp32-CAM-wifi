#include <Wire.h>
#include <ArduinoJson.h>
#include <Servo.h>

#define STEPPER_PIN1 2
#define STEPPER_PIN2 3
#define STEPPER_PIN3 4
#define STEPPER_PIN4 5

#define ENDPOINT_PIN 6
#define ENDPOINT_MOTOR 7

#define RIGHTEND_POINT_PIN 8
#define LEFTENDPOINT_PIN 9

#define SUB_ARM_SERVO 10
#define MAIN_ARM_SERVO 11

#define ROVER_WHEEL_PIN 12
#define ROVER_WHEEL_SENSOR 13

#define I2C_SLAVE_ADDR 0x08
#define JSON_CAPACITY 512
#define BUTTON_PIN 8
#define MAX_COORDINATES 10  // Maximum number of coordinates to store

// Custom Command
#define RESET_COMMAND "rs" // for "reset"
#define MOVENEXT_COMMAND "mn" // for "move_next"

// Set to 1 to enable serial debugging, 0 to disable
#define SERIAL_DEBUG 1

Servo servo1;
Servo servo2;

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

// Coordinate structure
struct Coordinates {
  float x;
  float y;
  
  // Default constructor with zero initialization
  Coordinates() : x(0.0), y(0.0) {}
  
  // Parameterized constructor
  Coordinates(float xCoord, float yCoord) : x(xCoord), y(yCoord) {}
};

// Global variables
StaticJsonDocument<JSON_CAPACITY> doc;
char jsonBuffer[JSON_CAPACITY];
volatile size_t expectedLength = 0;
volatile size_t currentIndex = 0;
volatile bool receivingLength = true;
volatile bool newData = false;
volatile int roverCurrentState = 0;
volatile bool resetReceived = false;
volatile bool moveNextReceived = false;

// Fixed-size array to store coordinates
Coordinates coordinatesArray[MAX_COORDINATES];
volatile size_t coordinatesCount = 0;

int initialHorizontalPoint = 0;

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

  servo1.attach(MAIN_ARM_SERVO);
  servo2.attach(SUB_ARM_SERVO);

  pinMode(ENDPOINT_PIN, INPUT);
  pinMode(ENDPOINT_MOTOR, OUTPUT);

  pinMode(RIGHTEND_POINT_PIN, INPUT);
  pinMode(LEFTENDPOINT_PIN, INPUT);

  pinMode(ROVER_WHEEL_PIN,OUTPUT);
  pinMode(ROVER_WHEEL_SENSOR,INPUT);

  pinMode(STEPPER_PIN1, OUTPUT);
  pinMode(STEPPER_PIN2, OUTPUT);
  pinMode(STEPPER_PIN3, OUTPUT);
  pinMode(STEPPER_PIN4, OUTPUT);

  // Setup button pin with internal pullup
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop() {
  #if SERIAL_DEBUG
  Serial.println(roverCurrentState);
  #endif
  roverCurrentState = 0;

  if (resetReceived) {
    #if SERIAL_DEBUG
    Serial.println("Reset command received!");
    #endif
    roverCurrentState = 2;
    
    currentIndex = 0;
    receivingLength = true;
    newData = false;
    memset(jsonBuffer, 0, JSON_CAPACITY);
    
    delay(1000);
    resetRover();
    resetReceived = false;
    roverCurrentState = 0;
  }

  if (moveNextReceived) {
    #if SERIAL_DEBUG
    Serial.println("Move_next command received!");
    #endif
    roverCurrentState = 3;
    moveNextReceived = false;
    
    currentIndex = 0;
    receivingLength = true;
    newData = false;
    memset(jsonBuffer, 0, JSON_CAPACITY);
    
    delay(1000);
    moveNextRover();
    roverCurrentState = 0;
  }

  if (newData) {
    coordinatesCount = 0;
    roverCurrentState = 0;

    DeserializationError error = deserializeJson(doc, jsonBuffer);

    if (!error) {
      #if SERIAL_DEBUG
      Serial.println("Detected Points:");
      #endif

      if (doc.containsKey("points") && doc["points"].is<JsonArray>()) {
        JsonArray points = doc["points"].as<JsonArray>();

        int pointCount = 0;
        for (JsonVariant pointVar : points) {
          if (coordinatesCount >= MAX_COORDINATES) break;

          float x = pointVar["x"].as<float>();
          float y = pointVar["y"].as<float>();

          #if SERIAL_DEBUG
          Serial.print("Point ");
          Serial.print(pointCount + 1);
          Serial.print(": x = ");
          Serial.print(x);
          Serial.print(", y = ");
          Serial.println(y);
          #endif

          coordinatesArray[coordinatesCount] = Coordinates(x, y);
          coordinatesCount++;
          pointCount++;
        }
        
        startRoverOperation();

        #if SERIAL_DEBUG
        Serial.println("Rover Task is done");
        #endif
      }
    } else {
      #if SERIAL_DEBUG
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      #endif
    }

    currentIndex = 0;
    receivingLength = true;
    newData = false;
    memset(jsonBuffer, 0, sizeof(jsonBuffer));
  }

  delay(50);
}

void receiveEvent(int numBytes)
{
  // Check if this might be a reset command
  if (numBytes == 2) {  // Length of "rs" to indicates "reset"
    char command[3] = {0};  // Extra byte for null terminator
    int i = 0;
    while (Wire.available() && i < 2) {
      command[i++] = Wire.read();
    }
    command[2] = '\0';
    
    if (strcmp(command, RESET_COMMAND) == 0) {
      resetReceived = true;
      return;
    }

    if (strcmp(command, MOVENEXT_COMMAND) == 0) {
      moveNextReceived = true;
      return;
    }
  }

  while (Wire.available())
    {
        if (receivingLength)
        {
            // First receive the expected length (2 bytes)
            byte lowByte = Wire.read();
            if (Wire.available())
            {
                byte highByte = Wire.read();
                expectedLength = (highByte << 8) | lowByte;
                receivingLength = false;
                
                // Reset if expected length is too large
                if (expectedLength > JSON_CAPACITY - 1) {
                    expectedLength = JSON_CAPACITY - 1;
                }
            }
        }
        else
        {
            // Receive JSON data
            if (currentIndex < expectedLength)
            {
                jsonBuffer[currentIndex] = Wire.read();
                currentIndex++;

                if (currentIndex >= expectedLength)
                {
                    jsonBuffer[currentIndex] = '\0'; // Null terminate
                    newData = true;
                }
            }
        }
    }
}

void requestEvent()
{
    // Send the button state when master requests it
    Wire.write(roverCurrentState);
}

void startRoverOperation() {
  // Process the received coordinates
  for (size_t i = 0; i < coordinatesCount; i++) {
    #if SERIAL_DEBUG
    Serial.print("Processing Point ");
    Serial.print(i + 1);
    Serial.print(": X = ");
    Serial.print(coordinatesArray[i].x);
    Serial.print(", Y = ");
    Serial.println(coordinatesArray[i].y);
    #endif
  }
    
    // Control logic starts from here
    #if SERIAL_DEBUG
      Serial.println("goto_Initial_Stepper_Arm_Point");
    #endif
    gotoInitialStepperArmPoint();
    #if SERIAL_DEBUG
      Serial.println("move_To_Horizontal_Position");
    #endif
    moveToHorizontalPosition();
    #if SERIAL_DEBUG
      Serial.println("move_Rover_Forward");
    #endif
    moveRoverForward();

    delay(50);
    // allowing to performe next operation
    roverCurrentState = 1;
}

void setStepperPins(int step[4]) {
  digitalWrite(STEPPER_PIN1, step[0]);
  digitalWrite(STEPPER_PIN2, step[1]);
  digitalWrite(STEPPER_PIN3, step[2]);
  digitalWrite(STEPPER_PIN4, step[3]);
}

void stepMotor(bool direction) {
  if (direction) {
    stepIndex = (stepIndex + 1) % 8; // Move to the next step
  } else {
    stepIndex = (stepIndex - 1 + 8) % 8; // Move to the previous step
  }
  setStepperPins(stepSequence[stepIndex]);
}

void moveRoverForward(){
  while(ROVER_WHEEL_SENSOR == HIGH){
      digitalWrite(ROVER_WHEEL_PIN,HIGH);
  }
  digitalWrite(ROVER_WHEEL_PIN,LOW);
  digitalWrite(ROVER_WHEEL_PIN,HIGH);
  delay(200); // move wheel magnet away from sensor
  digitalWrite(ROVER_WHEEL_PIN,LOW);
}

void moveToHorizontalPosition() {
    for (size_t i = 0; i < coordinatesCount; i++) {
      int horizontalTarget = int(coordinatesArray[i].x);
        while (initialHorizontalPoint < horizontalTarget ) {
            stepMotor(false);
            initialHorizontalPoint++;
            delay(1); // Adding a small delay for smooth motor movement
        }
        roverArm(int(coordinatesArray[i].y)); // Perfomr servo arm action
        delay(2000);
    }
}

void gotoInitialStepperArmPoint() {
    while (digitalRead(LEFTENDPOINT_PIN) == LOW) {
        stepMotor(true); // Move in reverse to the initial point
        delay(1);
    }
    initialHorizontalPoint = 0;
}

void roverArm(int mainArmTargetPoint){
  int endPoint = 150;
  int endPointMax = 50; // usually 0
  int subArmInitialPoint = 150;
  int mainArmInitialPoint = 50;
  int mainDelay = 20;
  int subDelay = 30;

  for (int pos = mainArmInitialPoint; pos <= mainArmTargetPoint; pos++) {
    servo1.write(pos);
    delay(mainDelay);
  }

  for (int pos = subArmInitialPoint; pos >= endPointMax; pos--) {
    if (digitalRead(ENDPOINT_PIN) == HIGH) { 
      endPoint = pos;
      break;
    }
    servo2.write(pos);
    endPoint = endPointMax;
    delay(subDelay);
  }

  digitalWrite(ENDPOINT_MOTOR,HIGH);
  delay(1000); // pollination end is run for 1 second
  digitalWrite(ENDPOINT_MOTOR,LOW);

  for (int pos = endPoint ; pos <= subArmInitialPoint; pos++) {
    servo2.write(pos);
    delay(subDelay);
  }

  for (int pos = mainArmTargetPoint; pos >= mainArmInitialPoint; pos--) {
    servo1.write(pos);
    delay(mainDelay);
  }
}

void resetRover(){
  #if SERIAL_DEBUG
    Serial.println("Reset the rover");
  #endif  
  delay(10 * 1000);
}

void moveNextRover(){
  #if SERIAL_DEBUG
    Serial.println("Move rover next");
  #endif
  delay(10 * 1000);
}