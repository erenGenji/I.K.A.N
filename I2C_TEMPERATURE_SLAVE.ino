#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// -------------------- Hardware Config --------------------
#define ONE_WIRE_BUS        4

#define I2C_SLAVE_ADDR      0x12
#define I2C_SDA_PIN         8
#define I2C_SCL_PIN         9

// -------------------- Sensor Config --------------------
#define TEMP_RESOLUTION     10
#define READ_INTERVAL_MS    1000
#define STALE_TIMEOUT_MS    5000

// -------------------- Device Info --------------------
#define DEVICE_ID           0xA1
#define FW_VERSION_MAJOR    1
#define FW_VERSION_MINOR    0

// -------------------- Status Codes --------------------
#define STATUS_OK           0
#define STATUS_NO_PROBE     1
#define STATUS_DISCONNECTED 2
#define STATUS_STALE        3

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Standardized I2C data packet
struct __attribute__((packed)) TempRegisterMap {
  uint8_t  deviceId;        // Fixed: 0xA1
  uint8_t  fwMajor;         // Firmware major version
  uint8_t  fwMinor;         // Firmware minor version
  uint8_t  status;          // Sensor status

  int16_t  tempC_x100;      // Temperature in Celsius * 100
  uint16_t updateRateMs;    // Reading interval

  uint32_t uptimeMs;        // ESP uptime
  uint32_t sampleCounter;   // Increments every sample attempt

  uint8_t  crc8;            // Checksum
};

TempRegisterMap regMap;

volatile uint8_t registerPointer = 0;

unsigned long lastReadTime = 0;
unsigned long lastGoodReadTime = 0;

// -------------------- CRC-8 --------------------
uint8_t calculateCRC8(const uint8_t *data, uint8_t length) {
  uint8_t crc = 0x00;

  for (uint8_t i = 0; i < length; i++) {
    crc ^= data[i];

    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x07;
      } else {
        crc <<= 1;
      }
    }
  }

  return crc;
}

// -------------------- Update Register Map --------------------
void updateRegisterMap(uint8_t status, int16_t tempC_x100) {
  noInterrupts();

  regMap.deviceId = DEVICE_ID;
  regMap.fwMajor = FW_VERSION_MAJOR;
  regMap.fwMinor = FW_VERSION_MINOR;
  regMap.status = status;
  regMap.tempC_x100 = tempC_x100;
  regMap.updateRateMs = READ_INTERVAL_MS;
  regMap.uptimeMs = millis();
  regMap.sampleCounter++;

  regMap.crc8 = calculateCRC8(
    (uint8_t *)&regMap,
    sizeof(TempRegisterMap) - 1
  );

  interrupts();
}

// -------------------- I2C Receive --------------------
// Master writes 1 byte to choose register offset
void onI2CReceive(int length) {
  if (Wire.available()) {
    registerPointer = Wire.read();
  }

  while (Wire.available()) {
    Wire.read(); // discard extra bytes
  }
}

// -------------------- I2C Request --------------------
// Master reads bytes starting from registerPointer
void onI2CRequest() {
  uint8_t buffer[sizeof(TempRegisterMap)];

  noInterrupts();
  memcpy(buffer, (const void *)&regMap, sizeof(TempRegisterMap));
  interrupts();

  uint8_t *data = buffer;
  uint8_t size = sizeof(TempRegisterMap);

  if (registerPointer >= size) {
    Wire.write(0xFF);
    return;
  }

  Wire.write(data + registerPointer, size - registerPointer);
}

// -------------------- Setup --------------------
void setup() {
  Serial.begin(115200);

  sensors.begin();
  sensors.setResolution(TEMP_RESOLUTION);

  memset(&regMap, 0, sizeof(regMap));
  updateRegisterMap(STATUS_NO_PROBE, 0);

  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  Wire.begin(
    (uint8_t)I2C_SLAVE_ADDR,
    I2C_SDA_PIN,
    I2C_SCL_PIN,
    100000
  );

  Serial.println("Standardized ESP32-C3 I2C temperature sensor started");
}

// -------------------- Loop --------------------
void loop() {
  unsigned long now = millis();

  if (now - lastReadTime >= READ_INTERVAL_MS) {
    lastReadTime = now;

    readTemperature();
  }

  if ((millis() - lastGoodReadTime > STALE_TIMEOUT_MS) &&
      regMap.status == STATUS_OK) {
    updateRegisterMap(STATUS_STALE, regMap.tempC_x100);
  }
}

// -------------------- Read Temperature --------------------
void readTemperature() {
  regMap.sampleCounter++;

  if (sensors.getDeviceCount() == 0) {
    updateRegisterMap(STATUS_NO_PROBE, 0);
    Serial.println("ERROR: No probe detected");
    return;
  }

  sensors.requestTemperatures();

  float tempC = sensors.getTempCByIndex(0);

  if (tempC == DEVICE_DISCONNECTED_C) {
    updateRegisterMap(STATUS_DISCONNECTED, 0);
    Serial.println("ERROR: Probe disconnected");
    return;
  }

  int16_t tempC_x100 = (int16_t)(tempC * 100.0);

  lastGoodReadTime = millis();

  updateRegisterMap(STATUS_OK, tempC_x100);

  Serial.print("Temp: ");
  Serial.print(tempC, 2);
  Serial.println(" C");
}