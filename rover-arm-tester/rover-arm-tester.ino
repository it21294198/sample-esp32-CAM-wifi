#include <Servo.h>

// Set to 1 to enable serial debugging, 0 to disable
#define SERIAL_DEBUG 1
#define BUFFER_SIZE 3

#define STEPPER_PIN1 2
#define STEPPER_PIN2 3
#define STEPPER_PIN3 4
#define STEPPER_PIN4 5

#define ENDPOINT_PIN 6
#define ENDPOINT_MOTOR 7

#define RIGHT_ENDPOINT_PIN 8
#define LEFT_ENDPOINT_PIN 9

#define SUB_ARM_SERVO_PIN 10
#define MAIN_ARM_SERVO_PIN 11

#define ROVER_WHEEL_PIN 12

Servo mainArmServo;
Servo subArmServo;

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

int16_t xValues[BUFFER_SIZE] = {2, 4, 6};
int16_t yValues[BUFFER_SIZE] = {100, 200, 300};

int currentHorizontalPosition = 0;
long timer = 0;
bool isReset = false;

void setup()
{
#if SERIAL_DEBUG
    Serial.begin(9600);
    Serial.println("Arduino I2C Slave initialized");
#endif

    mainArmServo.attach(MAIN_ARM_SERVO_PIN);
    subArmServo.attach(SUB_ARM_SERVO_PIN);

    pinMode(ENDPOINT_PIN, INPUT);
    pinMode(ENDPOINT_MOTOR, OUTPUT);

    pinMode(RIGHT_ENDPOINT_PIN, INPUT);
    pinMode(LEFT_ENDPOINT_PIN, INPUT);

    pinMode(STEPPER_PIN1, OUTPUT);
    pinMode(STEPPER_PIN2, OUTPUT);
    pinMode(STEPPER_PIN3, OUTPUT);
    pinMode(STEPPER_PIN4, OUTPUT);

    pinMode(ROVER_WHEEL_PIN,OUTPUT);
}

void moveNextRover(){
  #if SERIAL_DEBUG
    Serial.println("Move rover next");
  #endif

  digitalWrite(ROVER_WHEEL_PIN,HIGH);
  delay(2000); // move wheel for 3 seconds
  digitalWrite(ROVER_WHEEL_PIN,LOW);
  delay(2 * 1000);
}

void gotoInitialStepperArmPoint()
{
    while (digitalRead(LEFT_ENDPOINT_PIN) == LOW)
    {
        stepMotor(false); // Move in reverse to the initial point
        delay(1);
    }
    currentHorizontalPosition = 0;
}

void gotoInitialServoArmPoint(){
  const int subArmInitialPoint = 150;
  const int mainArmInitialPoint = 50;
  subArmServo.write(subArmInitialPoint);
  mainArmServo.write(mainArmInitialPoint);
}

void moveToHorizontalPositionTimer(){
  currentHorizontalPosition++;
  timer = 0;
  // #if SERIAL_DEBUG
  //   Serial.println(currentHorizontalPosition);
  // #endif
}

void moveToHorizontalPosition()
{
  #if SERIAL_DEBUG
      Serial.println("Usual arm movements");
  #endif

    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        int horizontalTarget = xValues[i];
        while (currentHorizontalPosition <= horizontalTarget)
        {
            stepMotor(true);
            if(timer>=10000){
              moveToHorizontalPositionTimer();
            }
            timer++;
            delay(1);
        }
        #if SERIAL_DEBUG
            Serial.println(horizontalTarget);
        #endif
        moveRoverArm(yValues[i]);
        delay(2000);
    }
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

void moveRoverArm(int targetPoint)
{
    int subArmEndPointMax = 50;
    const int subArmInitialPoint = 150;
    const int mainArmInitialPoint = 50;
    const int mainDelay = 30;
    const int subDelay = 50;

    const int mainArmTargetPoint = map(targetPoint,0,400,mainArmInitialPoint,100);
    #if SERIAL_DEBUG
      Serial.println(mainArmTargetPoint);
    #endif
    // Move main arm to the target position
    for (int pos = mainArmInitialPoint; pos <= mainArmTargetPoint; pos++)
    {
        mainArmServo.write(pos);
        delay(mainDelay);
    }

    // Move sub arm down
    for (int pos = subArmInitialPoint; pos >= subArmEndPointMax; pos--)
    {
        if (digitalRead(ENDPOINT_PIN) == HIGH)
        {
            subArmEndPointMax = subArmInitialPoint;
            break;
        }
        subArmServo.write(pos);
        delay(subDelay);
    }

    // Activate endpoint motor
    digitalWrite(ENDPOINT_MOTOR, HIGH);
    delay(1000); // Activate motor for 1 second
    digitalWrite(ENDPOINT_MOTOR, LOW);

    // Reset sub arm position
    for (int pos = subArmEndPointMax; pos <= subArmInitialPoint; pos++)
    {
        subArmServo.write(pos);
        delay(subDelay);
    }

    // Reset main arm position
    for (int pos = mainArmTargetPoint; pos >= mainArmInitialPoint; pos--)
    {
        mainArmServo.write(pos);
        delay(mainDelay);
    }
}

void resetRover()
{

  // if(isReset){
  //   return 0;
  // }
  // isReset = true;

#if SERIAL_DEBUG
    Serial.println("Resetting the rover arm");
#endif

    for (int pos = 50; pos <= 100; pos++)
    {
        mainArmServo.write(pos);
        delay(20);
    }

    for (int pos = 50; pos <= 160; pos++)
    {
        subArmServo.write(pos);
        delay(40);
    }

    for (int pos = 160; pos >= 50; pos--)
    {
        subArmServo.write(pos);
        delay(40);
    }

    for (int pos =100; pos >= 50; pos--)
    {
        mainArmServo.write(pos);
        delay(20);
    }

}
void testLeftRightEndButton(){
  Serial.print("Endpoint status : ");
  Serial.print(digitalRead(ENDPOINT_PIN));
  Serial.print(" Left status : ");
  Serial.print(digitalRead(LEFT_ENDPOINT_PIN));
  Serial.print(" Right status : ");
  Serial.println(digitalRead(RIGHT_ENDPOINT_PIN));
  delay(100);
}

void loop()
{
  // testLeftRightEndButton();
  // resetRover();
  gotoInitialStepperArmPoint();
  gotoInitialServoArmPoint();
  moveToHorizontalPosition();
  moveNextRover();
}
