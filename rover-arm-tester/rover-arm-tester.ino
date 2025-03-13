#include <Servo.h>

// Set to 1 to enable serial debugging, 0 to disable
#define SERIAL_DEBUG 1
#define ARM_TEST_MODE 1

#define BUFFER_SIZE 3

#define STEPPER_PIN1 2
#define STEPPER_PIN2 3
#define STEPPER_PIN3 4
#define STEPPER_PIN4 5

#define ENDPOINT_PIN 6
#define ENDPOINT_MOTOR 7

#define LEFT_ENDPOINT_PIN 9
#define ROVER_WHEEL_PIN 10

#define MAIN_ARM_SERVO_PIN 11

#define Z_ARM_UP_PIN 12
#define Z_ARM_DOWN_PIN 13

#define PI 3.1415926535897932384626433832795

Servo mainArmServo;

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

// int16_t xValues[BUFFER_SIZE] = {2, 3};
// int16_t yValues[BUFFER_SIZE] = {100, 200};
int16_t xValues[BUFFER_SIZE] = {5, 10, 15};
int16_t yValues[BUFFER_SIZE] = {100, 200, 300};

int currentHorizontalPosition = 0;
long timer = 0;
bool isReset = false;
int initialTimerValue = 4000;

void setup()
{
  #if SERIAL_DEBUG
      Serial.begin(9600);
      Serial.println("Arduino I2C Slave initialized");
  #endif

    mainArmServo.attach(MAIN_ARM_SERVO_PIN);

    pinMode(ENDPOINT_PIN, INPUT);
    pinMode(ENDPOINT_MOTOR, OUTPUT);

    pinMode(LEFT_ENDPOINT_PIN, INPUT);

    pinMode(STEPPER_PIN1, OUTPUT);
    pinMode(STEPPER_PIN2, OUTPUT);
    pinMode(STEPPER_PIN3, OUTPUT);
    pinMode(STEPPER_PIN4, OUTPUT);

    pinMode(ROVER_WHEEL_PIN,OUTPUT);

    pinMode(Z_ARM_DOWN_PIN,OUTPUT);
    pinMode(Z_ARM_UP_PIN,OUTPUT);

    digitalWrite(Z_ARM_UP_PIN,LOW);
    digitalWrite(Z_ARM_DOWN_PIN,LOW);
    delay(1000);
}

void moveNextRover(){
  #if SERIAL_DEBUG
    Serial.println("Move rover next");
  #endif
  digitalWrite(ROVER_WHEEL_PIN,HIGH);
  delay(2 * 1000); // move wheel for 2 seconds
  digitalWrite(ROVER_WHEEL_PIN,LOW);
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

void gotoInitialServoArmPoint(){
  const int mainArmInitialPoint = 170;
  mainArmServo.write(mainArmInitialPoint);
}

void moveToHorizontalPositionTimer(){
  currentHorizontalPosition++;
  initialTimerValue += 100;
  timer = 0;
  #if SERIAL_DEBUG
    Serial.println(currentHorizontalPosition);
  #endif
}

void moveToHorizontalPosition()
{
  #if SERIAL_DEBUG
      Serial.println("Usual arm movements");
  #endif

  for (int i = 0; i < BUFFER_SIZE; i++){
    moveStepperLine(xValues[i]);
    moveServoAngle(map(yValues[i],0,400,80,180)); // max 170 - min 80
  }
}

void moveStepperLine(int horizontalTarget){
        while (currentHorizontalPosition <= horizontalTarget)
        {
            stepMotor(true);
            if(timer>=initialTimerValue){
              moveToHorizontalPositionTimer();
            }
            timer++;
            delay(1);
        }
}

void moveServoAngle(int angle){

    for(int i = 180 ; i >= angle ; i--){
      mainArmServo.write(i);
      delay(50);
    }

    // performZAction();

    for(int i = angle ; i <= 180 ; i++){
      mainArmServo.write(i);
      delay(50);
    }

}

void perfomrPollination(){
  digitalWrite(ENDPOINT_MOTOR, HIGH);
  delay(3000);
  digitalWrite(ENDPOINT_MOTOR, LOW);
}

void performZAction() {
    unsigned long startTime = millis(); // Record the start time

    // Move the arm down until the endpoint switch is triggered OR 7 seconds have passed
    while (!digitalRead(ENDPOINT_PIN) && (millis() - startTime < 7000)) {  
    // while ((millis() - startTime < 7000)) {  
        digitalWrite(Z_ARM_DOWN_PIN, HIGH);
    }
    digitalWrite(Z_ARM_DOWN_PIN, LOW); // Stop moving down after timeout or endpoint trigger

    perfomrPollination();

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

void resetRover()
{

  if(isReset){
    return 0;
  }
  isReset = true;

  #if SERIAL_DEBUG
      Serial.println("Resetting the rover arm");
  #endif

  gotoInitialStepperArmPoint();

  for(int i = 180 ; i >= 0 ; i--){
    mainArmServo.write(i);
    delay(50);
  }

  for(int i = 0 ; i <= 180 ; i++){
    mainArmServo.write(i);
    delay(50);
  }
}

void testLeftRightEndButton(){
  Serial.print("Endpoint status : ");
  Serial.print(digitalRead(ENDPOINT_PIN));
  Serial.print(" Left status : ");
  Serial.print(digitalRead(LEFT_ENDPOINT_PIN));
  delay(100);
}

float calculateInverseSine(float y, float r) {
  if (r == 0) {
    Serial.println("Error: Radius cannot be zero");
    return 0;
  }
  float ratio = y/r;
  if (ratio < -1 || ratio > 1) {
    Serial.println("Error: y/r ratio must be between -1 and 1");
    return 0;
  }
  float val = asin(ratio);
  return val;
}

float calculateResult(float x, float r, float val) {
  float cosVal = r * cos(val);
  float result = x + cosVal;
  return result;
}


void loop()
{
  // int BUFFER = 3;
  // int16_t xVal[BUFFER] = {200, 400, 600};
  // int16_t yVal[BUFFER] = {50, 75, 100};
  // Serial.println("----------------------------");
  // for (int i = 0; i < BUFFER; i++){
  //   float x = xVal[i];
  //   float y = yVal[i];
  //   float r = 100;
  //   float val = calculateInverseSine(y, r);
  //   float result = calculateResult(x, r, val);

  //   Serial.print("Angle : ");
  //   Serial.print(val * r);
  //   Serial.print(" Distance : ");
  //   Serial.println(result);
  //   delay(3000);
  // }

  // moveServoAngle(map(0,0,400,80,170));
  // testLeftRightEndButton();
  // resetRover();
  // perfomrPollination();
  // performZAction();

  gotoInitialServoArmPoint();
  gotoInitialStepperArmPoint();
  moveToHorizontalPosition();
  // gotoInitialStepperArmPoint();
  // moveNextRover();

}

