#include <Wire.h>
#include <HX711_ADC.h>
#include <EEPROM.h>
#include <ESP32Servo.h>

// ================= I2C TEMP =================
#define I2C_SLAVE_ADDR 0x12
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// ================= HX711 =================
#define HX711_DOUT 32
#define HX711_SCK  33
HX711_ADC LoadCell(HX711_DOUT, HX711_SCK);
bool hx711OK = false;

// ================= EBYTE E220 LORA =================
// SAME TESTED FEEDER PINS
#define LORA_RX_PIN 16   // ESP32 RX <- E220 TX
#define LORA_TX_PIN 17   // ESP32 TX -> E220 RX
#define LORA_AUX_PIN 19
#define LORA_M0_PIN  5
#define LORA_M1_PIN  23

HardwareSerial LoRaSerial(2);

// ================= EEPROM =================
#define EEPROM_SIZE 128
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_CAL_ADDR   4
#define EEPROM_FEEDER_CAL_ADDR 12
#define EEPROM_MAGIC      12345

float calibrationValue = 1.0;
float feederGramsPerCycle = 10.0;

// ================= STEPPER =================
#define IN1 13
#define IN2 12
#define IN3 14
#define IN4 27

#define STEP_DELAY_MS 3
#define STEPS_PER_REV 4096

const int stepSequence[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1}
};

// ================= SERVO =================
#define SERVO_PIN 18
#define SERVO_CLOSED_ANGLE 0
#define SERVO_OPEN_ANGLE   110
#define SERVO_SWEEP_DELAY  15

Servo pinionServo;

// ================= FEEDING =================
#define CLOSE_MARGIN_G 2.0
#define MAX_FILL_TIME_MS 20000

float lastTempC = 0.0;
float activityValue = 0.0;

unsigned long lastStatusSend = 0;
unsigned long lastPrint = 0;

// ================= TEMP STRUCT =================
struct __attribute__((packed)) TempRegisterMap {
  uint8_t  deviceId;
  uint8_t  fwMajor;
  uint8_t  fwMinor;
  uint8_t  status;
  int16_t  tempC_x100;
  uint16_t updateRateMs;
  uint32_t uptimeMs;
  uint32_t sampleCounter;
  uint8_t  crc8;
};

// ================= CRC =================
uint8_t calculateCRC8(const uint8_t *data, uint8_t length) {
  uint8_t crc = 0x00;

  for (uint8_t i = 0; i < length; i++) {
    crc ^= data[i];

    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) crc = (crc << 1) ^ 0x07;
      else crc <<= 1;
    }
  }

  return crc;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(800);

  Serial.println();
  Serial.println("IKAN FEEDER NODE STARTING");

  setupLoRa();
  setupMotorPins();
  setupServo();
  setupEEPROM();
  setupHX711();
  setupTempI2C();

  Serial.println("READY");
  Serial.println("Waiting LoRa command from hub: G:30");
}

// ================= LOOP =================
void loop() {
  readLoRaCommand();
  
  if (hx711OK) {
    LoadCell.update();
  }

  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();

    readTemperatureFromSlave();

    Serial.print("TEMP: ");
    Serial.print(lastTempC, 2);
    Serial.print(" C | ACT: ");
    Serial.println(activityValue, 2);
  }

  if (millis() - lastStatusSend >= 5000) {
    lastStatusSend = millis();
    sendStatusToHub();
  }

  //processFeedRequest(30.0);
}

// ================= SETUP HELPERS =================
void setupLoRa() {
  pinMode(LORA_AUX_PIN, INPUT);
  pinMode(LORA_M0_PIN, OUTPUT);
  pinMode(LORA_M1_PIN, OUTPUT);

  digitalWrite(LORA_M0_PIN, LOW);
  digitalWrite(LORA_M1_PIN, LOW);

  delay(1000);

  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  Serial.println("[LORA] Started");
  Serial.println("[LORA] M0 LOW, M1 LOW, UART 9600");
}

void setupMotorPins() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  releaseStepper();

  Serial.println("[STEPPER] Ready");
}

void setupServo() {
  pinionServo.attach(SERVO_PIN);
  delay(300);

  closePinion();

  Serial.println("[SERVO] Ready and closed");
}

void setupEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  loadCalibration();

  Serial.print("[EEPROM] HX711 cal: ");
  Serial.println(calibrationValue);

  Serial.print("[EEPROM] Feeder g/cycle: ");
  Serial.println(feederGramsPerCycle);
}

void setupHX711() {
  LoadCell.begin();

  unsigned long stabilizingTime = 2000;
  bool tare = true;

  LoadCell.start(stabilizingTime, tare);

  if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag()) {
    Serial.println("[HX711] ERROR - bypassing scale");
    hx711OK = false;
    return;
  }

  hx711OK = true;
  LoadCell.setCalFactor(calibrationValue);

  Serial.println("[HX711] OK");
}

void setupTempI2C() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(50000);
  Wire.setTimeOut(50);

  Serial.println("[TEMP I2C] Ready");
}

// ================= LORA =================
void readLoRaCommand() {
  while (LoRaSerial.available()) {
    String msg = LoRaSerial.readStringUntil('\n');
    msg.trim();

    if (msg.length() == 0) return;

    Serial.print("[LORA RX] ");
    Serial.println(msg);

    float grams = parseGramCommand(msg);

    if (grams > 0) {
      Serial.print("[COMMAND] Feed ");
      Serial.print(grams);
      Serial.println(" g");

      processFeedRequest(grams);
      sendStatusToHub();
    } else {
      Serial.println("[COMMAND] Invalid command");
    }
  }
}

float parseGramCommand(String msg) {
  msg.toUpperCase();
  msg.trim();

  if (msg.startsWith("G:")) {
    msg.replace("G:", "");
    msg.trim();
    return msg.toFloat();
  }

  if (msg.startsWith("G")) {
    msg.replace("G", "");
    msg.trim();
    return msg.toFloat();
  }

  return 0;
}

void sendStatusToHub() {
  readTemperatureFromSlave();

  String status = "TEMP:";
  status += String(lastTempC, 2);
  status += ",ACT:";
  status += String(activityValue, 2);

  LoRaSerial.println(status);
  LoRaSerial.flush();

  Serial.print("[LORA TX] ");
  Serial.println(status);
}

// ================= FEED PROCESS =================
void processFeedRequest(float targetGrams) {
  Serial.println();
  Serial.println("========== FEED START ==========");

  Serial.print("Target: ");
  Serial.print(targetGrams, 2);
  Serial.println(" g");

  if (!hx711OK) {
    Serial.println("[FEED] HX711 bypass mode");

    int cyclesNeeded = ceil(targetGrams / feederGramsPerCycle);
    if (cyclesNeeded < 1) cyclesNeeded = 1;

    closePinion();
    delay(500);

    for (int i = 0; i < cyclesNeeded; i++) {
      Serial.print("[FEED] Cycle ");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.println(cyclesNeeded);

      runFeederCycle();
      delay(500);
    }

    releaseStepper();

    sendDoneMessage(targetGrams, -1, cyclesNeeded);

    Serial.println("=========== FEED END ===========");
    return;
  }

  closePinion();
  delay(800);

  Serial.println("[FEED] Taring chamber");
  LoadCell.tare();
  delay(1000);

  float startWeight = getStableWeight();

  Serial.print("[FEED] Start weight: ");
  Serial.print(startWeight, 2);
  Serial.println(" g");

  openPinion();

  unsigned long fillStart = millis();
  float addedToChamber = 0;

  while (true) {
    if (hx711OK) {
      LoadCell.update();
    }

    float currentWeight = getQuickStableWeight();
    addedToChamber = currentWeight - startWeight;

    if (addedToChamber < 0) addedToChamber = 0;

    Serial.print("[FEED] Filling: ");
    Serial.print(addedToChamber, 2);
    Serial.println(" g");

    if (addedToChamber >= targetGrams - CLOSE_MARGIN_G) {
      Serial.println("[FEED] Target nearly reached");
      break;
    }

    if (millis() - fillStart > MAX_FILL_TIME_MS) {
      Serial.println("[FEED] Fill timeout");
      break;
    }

    delay(200);
  }

  closePinion();
  delay(1500);

  float finalWeight = getStableWeight();
  float measuredFeed = finalWeight - startWeight;

  if (measuredFeed < 0) measuredFeed = 0;

  Serial.print("[FEED] Measured: ");
  Serial.print(measuredFeed, 2);
  Serial.println(" g");

  int cyclesNeeded = ceil(measuredFeed / feederGramsPerCycle);

  if (cyclesNeeded < 1 && measuredFeed > 0) {
    cyclesNeeded = 1;
  }

  Serial.print("[FEED] Stepper cycles: ");
  Serial.println(cyclesNeeded);

  for (int i = 0; i < cyclesNeeded; i++) {
    Serial.print("[FEED] Dispense cycle ");
    Serial.print(i + 1);
    Serial.print("/");
    Serial.println(cyclesNeeded);

    runFeederCycle();
    delay(500);
  }

  releaseStepper();

  sendDoneMessage(targetGrams, measuredFeed, cyclesNeeded);

  Serial.println("=========== FEED END ===========");
}

void sendDoneMessage(float requested, float measured, int cycles) {
  String doneMsg = "DONE,REQ:";
  doneMsg += String(requested, 2);
  doneMsg += ",MEASURED:";
  doneMsg += String(measured, 2);
  doneMsg += ",CYCLES:";
  doneMsg += String(cycles);

  LoRaSerial.println(doneMsg);
  LoRaSerial.flush();

  Serial.print("[LORA TX] ");
  Serial.println(doneMsg);
}

// ================= SERVO =================
void openPinion() {
  Serial.println("[SERVO] Opening");

  for (int pos = SERVO_CLOSED_ANGLE; pos <= SERVO_OPEN_ANGLE; pos++) {
    pinionServo.write(pos);
    delay(SERVO_SWEEP_DELAY);
  }
}

void closePinion() {
  Serial.println("[SERVO] Closing");

  for (int pos = SERVO_OPEN_ANGLE; pos >= SERVO_CLOSED_ANGLE; pos--) {
    pinionServo.write(pos);
    delay(SERVO_SWEEP_DELAY);
  }
}

// ================= STEPPER =================
void runFeederCycle() {
  Serial.println("[STEPPER] Forward 360");
  rotateStepper(STEPS_PER_REV, true);

  delay(500);

  Serial.println("[STEPPER] Backward 360");
  rotateStepper(STEPS_PER_REV, false);

  releaseStepper();
}

void rotateStepper(int steps, bool clockwise) {
  for (int i = 0; i < steps; i++) {
    int index;

    if (clockwise) {
      index = i % 8;
    } else {
      index = 7 - (i % 8);
    }

    digitalWrite(IN1, stepSequence[index][0]);
    digitalWrite(IN2, stepSequence[index][1]);
    digitalWrite(IN3, stepSequence[index][2]);
    digitalWrite(IN4, stepSequence[index][3]);

    delay(STEP_DELAY_MS);
  }
}

void releaseStepper() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// ================= WEIGHT =================
float getStableWeight() {
  if (!hx711OK) return 0;

  const int samples = 25;
  float readings[samples];

  for (int i = 0; i < 8; i++) {
    LoadCell.update();
    delay(40);
  }

  for (int i = 0; i < samples; i++) {
    LoadCell.update();
    readings[i] = LoadCell.getData();
    delay(60);
  }

  sortReadings(readings, samples);

  return readings[samples / 2];
}

float getQuickStableWeight() {
  if (!hx711OK) return 0;

  const int samples = 7;
  float readings[samples];

  for (int i = 0; i < samples; i++) {
    LoadCell.update();
    readings[i] = LoadCell.getData();
    delay(35);
  }

  sortReadings(readings, samples);

  return readings[samples / 2];
}

void sortReadings(float arr[], int n) {
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (arr[j] < arr[i]) {
        float temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
      }
    }
  }
}

// ================= TEMP =================
bool readTemperatureFromSlave() {
  TempRegisterMap data;

  Wire.beginTransmission(I2C_SLAVE_ADDR);
  Wire.write(0x00);

  byte error = Wire.endTransmission(false);

  if (error != 0) {
    Serial.println("[TEMP] I2C error");
    return false;
  }

  int received = Wire.requestFrom(I2C_SLAVE_ADDR, (uint8_t)sizeof(data));

  if (received != sizeof(data)) {
    Serial.println("[TEMP] Read error");
    return false;
  }

  Wire.readBytes((uint8_t *)&data, sizeof(data));

  uint8_t crc = calculateCRC8((uint8_t *)&data, sizeof(data) - 1);

  if (crc != data.crc8) {
    Serial.println("[TEMP] CRC error");
    return false;
  }

  if (data.status == 0) {
    lastTempC = data.tempC_x100 / 100.0;
    return true;
  }

  return false;
}

// ================= EEPROM =================
void loadCalibration() {
  int magic;
  EEPROM.get(EEPROM_MAGIC_ADDR, magic);

  if (magic == EEPROM_MAGIC) {
    EEPROM.get(EEPROM_CAL_ADDR, calibrationValue);
    EEPROM.get(EEPROM_FEEDER_CAL_ADDR, feederGramsPerCycle);

    if (calibrationValue == 0 || isnan(calibrationValue)) {
      calibrationValue = 1.0;
    }

    if (feederGramsPerCycle <= 0 || isnan(feederGramsPerCycle)) {
      feederGramsPerCycle = 10.0;
    }

    Serial.println("[EEPROM] Calibration loaded");
  } else {
    Serial.println("[EEPROM] No calibration found, using defaults");

    calibrationValue = 1.0;
    feederGramsPerCycle = 10.0;

    saveCalibration();
  }
}

void saveCalibration() {
  int magic = EEPROM_MAGIC;

  EEPROM.put(EEPROM_MAGIC_ADDR, magic);
  EEPROM.put(EEPROM_CAL_ADDR, calibrationValue);
  EEPROM.put(EEPROM_FEEDER_CAL_ADDR, feederGramsPerCycle);
  EEPROM.commit();

  Serial.println("[EEPROM] Calibration saved");
}