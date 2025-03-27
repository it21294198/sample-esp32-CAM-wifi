#define ROVER_WHEEL_PIN 10
#define SERIAL_DEBUG 1  // Enable serial debugging

const int moveDuration = 1000; // 1 seconds
bool wheelState = false; // Track the state of the wheel
unsigned long previousMillis = 0; // Store the last toggle time

void setup() {
    Serial.begin(9600);
    pinMode(ROVER_WHEEL_PIN, OUTPUT);
    digitalWrite(ROVER_WHEEL_PIN, LOW);
}

void loop() {
    moveRoverForwardNonBlocking();
}


}

