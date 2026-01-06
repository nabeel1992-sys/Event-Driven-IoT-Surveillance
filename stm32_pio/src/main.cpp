#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

// Variables
float baseX = 0, baseY = 0, baseZ = 0;
bool isLocked = false;             
unsigned long lastAlertTime = 0;   
const unsigned long COOLDOWN = 2000; // 2 seconds waiting time to avoid spam
const float SENSITIVITY = 3.0;     // Movement (m/s^2)

void calibrate() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  baseX = a.acceleration.x;
  baseY = a.acceleration.y;
  baseZ = a.acceleration.z;
  Serial.println("Re-Calibrated! System Ready for next motion.");
}

void setup() {
  Serial1.setTx(PA9);
  Serial1.setRx(PA10);
  Serial1.begin(115200);
  Serial.begin(115200);

  if (!mpu.begin()) {
    while (1) yield();
  }

  Serial.println("Initial Calibration... Do not move.");
  delay(2000); 
  calibrate(); // Initial position saved
}

void loop() {
  unsigned long currentTime = millis();

  if (!isLocked) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // Baseline se difference check karein
    float diffX = abs(a.acceleration.x - baseX);
    float diffY = abs(a.acceleration.y - baseY);
    float diffZ = abs(a.acceleration.z - baseZ);

    if (diffX > SENSITIVITY || diffY > SENSITIVITY || diffZ > SENSITIVITY) {
      // 1. Send Alert
      Serial1.println("TILT_ALERT");
      Serial.println("ALERT: Motion Detected! Sending to ESP32S3...");
      
      // 2. System lock and note the time
      isLocked = true;
      lastAlertTime = currentTime;
    }
  } 
  else {
    // Agar 3 seconds 
    if (currentTime - lastAlertTime > COOLDOWN) {
      Serial.println("Cooldown finished. Setting new baseline...");
      calibrate();  // "Zero" set position
      isLocked = false; 
    }
  }

  delay(50);
}