#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =========================
// CONFIG
// =========================
namespace Config {
  constexpr int SCREEN_WIDTH  = 128;
  constexpr int SCREEN_HEIGHT = 64;

  constexpr int OLED_SDA  = 8;
  constexpr int OLED_SCL  = 9;
  constexpr int OLED_ADDR = 0x3C;

  constexpr const char* AP_SSID = "IKAN-01";
  constexpr const char* AP_PASS = "12345678";

  constexpr int MIN_FEED_GRAMS = 50;
  constexpr int MAX_FEED_GRAMS = 5000;
  constexpr int FEED_SLOT_COUNT = 3;

  // LoRa UART pins
  constexpr int LORA_RX_PIN = 4;   // ESP RX  <- E220 TX
  constexpr int LORA_TX_PIN = 5;   // ESP TX  -> E220 RX
  constexpr long LORA_BAUD  = 9600;

  // Optional mode pins for E220
  constexpr int LORA_M0_PIN = 6;
  constexpr int LORA_M1_PIN = 7;
}

// =========================
// DATA MODEL
// =========================
struct FeedSlot {
  String time;
  int grams;
};

FeedSlot feedSlots[Config::FEED_SLOT_COUNT] = {
  {"--:--", 50},
  {"--:--", 50},
  {"--:--", 50}
};

// Mock received value from LoRa
int receivedGrams = 0;

// =========================
// GLOBAL OBJECTS
// =========================
Adafruit_SSD1306 display(
  Config::SCREEN_WIDTH,
  Config::SCREEN_HEIGHT,
  &Wire,
  -1
);

WebServer server(80);
HardwareSerial LoRaSerial(1);

// =========================
// LORA MANAGER
// =========================
namespace LoRaManager {
  String rxBuffer = "";

  void begin() {
    pinMode(Config::LORA_M0_PIN, OUTPUT);
    pinMode(Config::LORA_M1_PIN, OUTPUT);

    // Normal mode
    digitalWrite(Config::LORA_M0_PIN, LOW);
    digitalWrite(Config::LORA_M1_PIN, LOW);

    LoRaSerial.begin(
      Config::LORA_BAUD,
      SERIAL_8N1,
      Config::LORA_RX_PIN,
      Config::LORA_TX_PIN
    );

    Serial.println("[LORA] Started");
  }

  String buildSlotPacket(int slotIndex, const FeedSlot& slot) {
    String packet = "SLOT:";
    packet += String(slotIndex + 1);
    packet += ",TIME:";
    packet += slot.time;
    packet += ",GRAMS:";
    packet += String(slot.grams);
    return packet;
  }

  void sendMessage(const String& msg) {
    LoRaSerial.println(msg);
    Serial.print("[LORA TX] ");
    Serial.println(msg);
  }

  void sendAllSlots() {
    for (int i = 0; i < Config::FEED_SLOT_COUNT; i++) {
      sendMessage(buildSlotPacket(i, feedSlots[i]));
      delay(50);
    }
  }

  int extractGrams(const String& msg) {
    int idx = msg.indexOf("GRAMS:");
    if (idx == -1) return -1;

    idx += 6;
    String gramsStr = msg.substring(idx);

    int commaIdx = gramsStr.indexOf(',');
    if (commaIdx != -1) {
      gramsStr = gramsStr.substring(0, commaIdx);
    }

    gramsStr.trim();
    return gramsStr.toInt();
  }

  void handleReceivedMessage(const String& msg) {
    Serial.print("[LORA RX] ");
    Serial.println(msg);

    int grams = extractGrams(msg);
    if (grams > 0) {
      receivedGrams = grams;
      Serial.print("[LORA RX] receivedGrams updated to: ");
      Serial.println(receivedGrams);
    }
  }

  void update() {
    while (LoRaSerial.available()) {
      char c = (char)LoRaSerial.read();

      if (c == '\n') {
        rxBuffer.trim();
        if (rxBuffer.length() > 0) {
          handleReceivedMessage(rxBuffer);
        }
        rxBuffer = "";
      } else if (c != '\r') {
        rxBuffer += c;
      }
    }
  }
}

// =========================
// HTML PAGE
// =========================
const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>IKAN Feeder</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      margin: 20px;
      background: #f5f5f5;
      color: #222;
    }
    .card {
      max-width: 420px;
      margin: auto;
      background: white;
      padding: 20px;
      border-radius: 12px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.08);
    }
    h2 {
      margin-top: 0;
      margin-bottom: 18px;
    }
    .slot {
      border: 1px solid #ddd;
      border-radius: 10px;
      padding: 14px;
      margin-bottom: 14px;
      background: #fafafa;
    }
    label {
      display: block;
      margin-top: 10px;
      margin-bottom: 6px;
      font-weight: bold;
    }
    input[type="time"],
    input[type="number"] {
      width: 100%;
      padding: 10px;
      font-size: 16px;
      box-sizing: border-box;
    }
    button {
      margin-top: 18px;
      width: 100%;
      padding: 12px;
      font-size: 16px;
      border: none;
      border-radius: 8px;
      background: #1f7ae0;
      color: white;
      cursor: pointer;
    }
    .info {
      margin-top: 16px;
      font-size: 14px;
      color: #555;
    }
    .hint {
      font-size: 12px;
      color: #666;
      margin-top: 4px;
    }
  </style>
</head>
<body>
  <div class="card">
    <h2>IKAN Feeding Schedule</h2>

    <form action="/save" method="POST">
      <div class="slot">
        <h3>Slot 1</h3>
        <label for="feed1_time">Feeding Time</label>
        <input type="time" id="feed1_time" name="feed1_time" required>

        <label for="feed1_grams">Feed Amount (g)</label>
        <input type="number" id="feed1_grams" name="feed1_grams" min="50" max="5000" required>
        <div class="hint">Range: 50g to 5000g</div>
      </div>

      <div class="slot">
        <h3>Slot 2</h3>
        <label for="feed2_time">Feeding Time</label>
        <input type="time" id="feed2_time" name="feed2_time" required>

        <label for="feed2_grams">Feed Amount (g)</label>
        <input type="number" id="feed2_grams" name="feed2_grams" min="50" max="5000" required>
        <div class="hint">Range: 50g to 5000g</div>
      </div>

      <div class="slot">
        <h3>Slot 3</h3>
        <label for="feed3_time">Feeding Time</label>
        <input type="time" id="feed3_time" name="feed3_time" required>

        <label for="feed3_grams">Feed Amount (g)</label>
        <input type="number" id="feed3_grams" name="feed3_grams" min="50" max="5000" required>
        <div class="hint">Range: 50g to 5000g</div>
      </div>

      <button type="submit">Save Schedule</button>
    </form>

    <div class="info">
      Connect to IKAN-01 and open 192.168.4.1
    </div>
  </div>
</body>
</html>
)rawliteral";

// =========================
// DISPLAY FUNCTIONS
// =========================
bool initDisplay() {
  Wire.begin(Config::OLED_SDA, Config::OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, Config::OLED_ADDR)) {
    return false;
  }

  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(255);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.display();
  return true;
}

void showAPInfo(const String& ip) {
  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("SSID: IKAN-01");

  display.setCursor(0, 16);
  display.println("PASS: 12345678");

  display.setCursor(0, 32);
  display.print("IP: ");
  display.println(ip);

  display.setCursor(0, 48);
  display.println("READY");

  display.display();
}

// =========================
// WIFI FUNCTIONS
// =========================
void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(Config::AP_SSID, Config::AP_PASS);

  IPAddress ip = WiFi.softAPIP();

  Serial.println("Access Point started");
  Serial.print("SSID: ");
  Serial.println(Config::AP_SSID);
  Serial.print("PASS: ");
  Serial.println(Config::AP_PASS);
  Serial.print("IP: ");
  Serial.println(ip);

  showAPInfo(ip.toString());
}

// =========================
// HELPERS
// =========================
int clampFeedGrams(int grams) {
  if (grams < Config::MIN_FEED_GRAMS) return Config::MIN_FEED_GRAMS;
  if (grams > Config::MAX_FEED_GRAMS) return Config::MAX_FEED_GRAMS;
  return grams;
}

void updateFeedSlotFromRequest(int slotIndex, const String& timeArgName, const String& gramsArgName) {
  if (server.hasArg(timeArgName)) {
    feedSlots[slotIndex].time = server.arg(timeArgName);
  }

  if (server.hasArg(gramsArgName)) {
    int grams = server.arg(gramsArgName).toInt();
    feedSlots[slotIndex].grams = clampFeedGrams(grams);
  }
}

void printScheduleToSerial() {
  Serial.println("===== FEEDING SCHEDULE SAVED =====");
  for (int i = 0; i < Config::FEED_SLOT_COUNT; i++) {
    Serial.print("Slot ");
    Serial.print(i + 1);
    Serial.print(" -> Time: ");
    Serial.print(feedSlots[i].time);
    Serial.print(", Amount: ");
    Serial.print(feedSlots[i].grams);
    Serial.println(" g");
  }
  Serial.println("=================================");
}

String buildSaveResponseHtml() {
  String response;
  response += "<!DOCTYPE html><html><head>";
  response += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  response += "<title>Saved</title></head>";
  response += "<body style='font-family:Arial; margin:20px;'>";
  response += "<h2>Schedule Saved</h2>";

  for (int i = 0; i < Config::FEED_SLOT_COUNT; i++) {
    response += "<p><b>Slot ";
    response += String(i + 1);
    response += "</b><br>Time: ";
    response += feedSlots[i].time;
    response += "<br>Amount: ";
    response += String(feedSlots[i].grams);
    response += " g</p>";
  }

  response += "<p><b>Mock receivedGrams:</b> ";
  response += String(receivedGrams);
  response += "</p>";

  response += "<a href='/'>Back</a>";
  response += "</body></html>";
  return response;
}

// =========================
// WEB HANDLERS
// =========================
void handleRoot() {
  server.send(200, "text/html", MAIN_PAGE);
}

void handleSave() {
  updateFeedSlotFromRequest(0, "feed1_time", "feed1_grams");
  updateFeedSlotFromRequest(1, "feed2_time", "feed2_grams");
  updateFeedSlotFromRequest(2, "feed3_time", "feed3_grams");

  printScheduleToSerial();
  LoRaManager::sendAllSlots();

  server.send(200, "text/html", buildSaveResponseHtml());
}

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("Web server started");
}

// =========================
// SETUP / LOOP
// =========================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Booting...");
  Serial.print("Mock receivedGrams default = ");
  Serial.println(receivedGrams);

  if (!initDisplay()) {
    Serial.println("OLED not found");
  }

  startAccessPoint();
  LoRaManager::begin();
  setupRoutes();
}

void loop() {
  server.handleClient();
  LoRaManager::update();
}