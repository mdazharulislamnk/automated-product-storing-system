#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- Pin Definitions ---
#define TRIG_PIN 5
#define ECHO_PIN 18
#define SERVO_PIN 19
#define LED_IDLE 27   // Red LED (No object)
#define LED_SMALL 25  // Green LED (Bin A)
#define LED_LARGE 26  // Yellow LED (Bin B)

Servo sortingServo;
LiquidCrystal_I2C lcd(0x27, 16, 2); // Standard I2C address for 16x2 LCD

// --- FreeRTOS Queue & Task Handles ---
QueueHandle_t distanceQueue; 
TaskHandle_t SensorTaskHandle;
TaskHandle_t SortingTaskHandle;

void setup() {
  Serial.begin(115200);

  // Initialize GPIOs
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_IDLE, OUTPUT);
  pinMode(LED_SMALL, OUTPUT);
  pinMode(LED_LARGE, OUTPUT);

  // Initialize Servo & I2C LCD
  sortingServo.attach(SERVO_PIN);
  sortingServo.write(90); 
  
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System Booting..");

  // Create FreeRTOS Queue (holds up to 5 integer distances)
  distanceQueue = xQueueCreate(5, sizeof(int));
  if (distanceQueue == NULL) {
    Serial.println("Error: Failed to create queue");
    return;
  }

  // --- Task 1: Sensor Reading (Pinned to Core 0) ---
  xTaskCreatePinnedToCore(
    readSensorTask,    
    "Sensor Task",     
    2048,              
    NULL,              
    1,                 
    &SensorTaskHandle, 
    0                  
  );

  // --- Task 2: Sorting Logic & LCD (Pinned to Core 1) ---
  xTaskCreatePinnedToCore(
    sortingTask,       
    "Sorting Task",    
    2048,              
    NULL,              
    1,                 
    &SortingTaskHandle,
    1                  
  );

  Serial.println("FreeRTOS Automated Sorting System Booted!");
  delay(1000); // Brief pause to show boot screen
}

void loop() {
  // FreeRTOS handles the tasks autonomously. Main loop is empty.
}

// ==========================================
// TASK 1: Read HC-SR04 (Runs on Core 0)
// ==========================================
void readSensorTask(void *pvParameters) {
  for (;;) {
    digitalWrite(TRIG_PIN, LOW);
    vTaskDelay(pdMS_TO_TICKS(2));
    digitalWrite(TRIG_PIN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(10));
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH);
    int distance = duration * 0.034 / 2; 

    // Send distance to Queue safely
    xQueueSend(distanceQueue, &distance, portMAX_DELAY);

    // Run this task every 100 milliseconds
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}

// ==========================================
// TASK 2: Diverter, LEDs, & I2C LCD (Runs on Core 1)
// ==========================================
void sortingTask(void *pvParameters) {
  int receivedDistance;
  
  for (;;) {
    // Block and wait until data arrives in the queue from Task 1
    if (xQueueReceive(distanceQueue, &receivedDistance, portMAX_DELAY) == pdPASS) {
      
      digitalWrite(LED_IDLE, LOW);
      digitalWrite(LED_SMALL, LOW);
      digitalWrite(LED_LARGE, LOW);

      // Update Top Row of I2C LCD
      lcd.setCursor(0, 0);
      lcd.print("Dist: ");
      lcd.print(receivedDistance);
      lcd.print(" cm   "); // Padding spaces overwrite old characters

      // Update Bottom Row & Hardware based on thresholds
      lcd.setCursor(0, 1);
      if (receivedDistance > 200) {
        sortingServo.write(90);
        digitalWrite(LED_IDLE, HIGH);
        lcd.print("Stat: No Object ");
      } 
      else if (receivedDistance > 100 && receivedDistance <= 200) {
        sortingServo.write(45);
        digitalWrite(LED_SMALL, HIGH);
        lcd.print("Bin A: Small    ");
        
        Serial.print("[SORTING] Small Object (");
        Serial.print(receivedDistance);
        Serial.println("cm) routed to BIN A");
      } 
      else {
        sortingServo.write(135);
        digitalWrite(LED_LARGE, HIGH);
        lcd.print("Bin B: Large    ");
        
        Serial.print("[SORTING] Large Object (");
        Serial.print(receivedDistance);
        Serial.println("cm) routed to BIN B");
      }
    }
  }
}