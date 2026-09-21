// FULL IKAN TRANSMITTER HUB CODE
// 1 feeder only
// Sends: G:<grams>
// Receives: TEMP:xx.xx,ACT:0.00
// Web UI shows temp/activity

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>
#include <Update.h>

// ================= PINS =================
#define I2C_SDA         6
#define I2C_SCL         7

#define LORA_TX_PIN     21
#define LORA_RX_PIN     20
#define LORA_AUX_PIN    10
#define LORA_M0_PIN     8
#define LORA_M1_PIN     9

// ================= OLED =================
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDRESS    0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HardwareSerial LoRaSerial(1);
RTC_DS3231 rtc;
WebServer server(80);
DNSServer dnsServer;

// ================= CONFIG =================
#define EEPROM_SIZE     1024
#define MAX_SCHEDULES   1

char ssid[32] = "FishFeeder-Setup";
char password[32] = "fish1234";

bool rtcOK = false;
bool oledOK = false;
bool loraOK = false;
bool otaInProgress = false;
bool otaUploadRejected = false;
bool otaUploadSucceeded = false;

int lastMinute = -1;
unsigned long lastDisplayUpdate = 0;
int displayPage = 0;

String lastFeederStatus = "No data";
float pondTemp = 0.0;
float pondActivity = 0.0;

struct Schedule {
  int hour;
  int minute;
  int grams;
  char fish[20];
  char pond[20];
  bool active;
  bool fedToday;
};

Schedule schedules[MAX_SCHEDULES];

// ================= LORA =================
void setupLoRa() {
  pinMode(LORA_AUX_PIN, INPUT);
  pinMode(LORA_M0_PIN, OUTPUT);
  pinMode(LORA_M1_PIN, OUTPUT);

  digitalWrite(LORA_M0_PIN, LOW);
  digitalWrite(LORA_M1_PIN, LOW);

  delay(1000);

  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  unsigned long start = millis();

  while (digitalRead(LORA_AUX_PIN) == LOW) {
    if (millis() - start > 3000) {
      Serial.println("[LORA] ERROR: AUX not HIGH");
      loraOK = false;
      return;
    }
    delay(10);
  }

  loraOK = true;
  Serial.println("[LORA] Ready");
}

bool waitLoRaReady() {
  unsigned long start = millis();

  while (digitalRead(LORA_AUX_PIN) == LOW) {
    if (millis() - start > 2000) {
      Serial.println("[LORA] AUX timeout");
      return false;
    }
    delay(10);
  }

  delay(50);
  return true;
}

void sendFeedGrams(int grams) {
  if (!loraOK) {
    Serial.println("[LORA] Cannot send. LoRa not ready.");
    return;
  }

  if (!waitLoRaReady()) {
    Serial.println("[LORA] AUX busy, sending anyway for test...");
  }

  String data = "G:";
  data += String(grams);
  data += "\n";

  LoRaSerial.print(data);
  LoRaSerial.flush();

  Serial.print("[LORA] Sent: ");
  Serial.println(data);
}

void readLoRaStatus() {
  while (LoRaSerial.available()) {
    String msg = LoRaSerial.readStringUntil('\n');
    msg.trim();

    if (msg.length() > 0) {
      lastFeederStatus = msg;

      int tempIndex = msg.indexOf("TEMP:");
      int actIndex = msg.indexOf("ACT:");

      if (tempIndex >= 0 && actIndex >= 0) {
        int commaIndex = msg.indexOf(",", tempIndex);

        String tempStr = msg.substring(tempIndex + 5, commaIndex);
        String actStr = msg.substring(actIndex + 4);

        pondTemp = tempStr.toFloat();
        pondActivity = actStr.toFloat();
      }

      Serial.print("[FEEDER STATUS] ");
      Serial.println(msg);
    }
  }
}

// ================= OLED =================
void setupOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[OLED] ERROR");
    oledOK = false;
    return;
  }

  oledOK = true;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("IKAN Hub");
  display.println("Starting...");
  display.display();

  Serial.println("[OLED] OK");
}

void displayMainScreen() {
  if (!oledOK) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("=== IKAN HUB ===");

  if (rtcOK) {
    DateTime now = rtc.now();

    display.setTextSize(2);
    display.setCursor(10, 14);

    if (now.hour() < 10) display.print("0");
    display.print(now.hour());
    display.print(":");
    if (now.minute() < 10) display.print("0");
    display.print(now.minute());
    display.print(":");
    if (now.second() < 10) display.print("0");
    display.print(now.second());
  }

  display.setTextSize(1);
  display.setCursor(0, 40);
  display.print("RTC:");
  display.print(rtcOK ? "OK" : "ERR");
  display.print(" LORA:");
  display.println(loraOK ? "OK" : "ERR");

  display.setCursor(0, 52);
  display.print("IP:");
  display.println(WiFi.softAPIP());

  display.display();
}

void displayStatusScreen() {
  if (!oledOK) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("=== POND 1 ===");

  display.setCursor(0, 14);
  display.print("Temp: ");
  display.print(pondTemp, 2);
  display.println(" C");

  display.setCursor(0, 26);
  display.print("Act: ");
  display.println(pondActivity, 2);

  display.setCursor(0, 40);
  display.println(lastFeederStatus);

  display.display();
}

void updateDisplay() {
  if (millis() - lastDisplayUpdate > 4000) {
    lastDisplayUpdate = millis();
    displayPage = (displayPage + 1) % 2;
  }

  if (displayPage == 0) displayMainScreen();
  else displayStatusScreen();
}

// ================= EEPROM =================
void clearScheduleData(int index) {
  schedules[index].hour = 0;
  schedules[index].minute = 0;
  schedules[index].grams = 0;
  memset(schedules[index].fish, 0, sizeof(schedules[index].fish));
  memset(schedules[index].pond, 0, sizeof(schedules[index].pond));
  schedules[index].active = false;
  schedules[index].fedToday = false;
}

void saveEEPROM() {
  int addr = 0;

  EEPROM.write(addr++, 0xAA);

  EEPROM.put(addr, ssid);
  addr += 32;

  EEPROM.put(addr, password);
  addr += 32;

  for (int i = 0; i < MAX_SCHEDULES; i++) {
    EEPROM.put(addr, schedules[i]);
    addr += sizeof(Schedule);
  }

  EEPROM.commit();
  Serial.println("[EEPROM] Saved");
}

void loadEEPROM() {
  int addr = 0;

  if (EEPROM.read(addr++) != 0xAA) {
    Serial.println("[EEPROM] No saved data");

    for (int i = 0; i < MAX_SCHEDULES; i++) {
      clearScheduleData(i);
    }

    return;
  }

  EEPROM.get(addr, ssid);
  addr += 32;

  EEPROM.get(addr, password);
  addr += 32;

  for (int i = 0; i < MAX_SCHEDULES; i++) {
    EEPROM.get(addr, schedules[i]);
    addr += sizeof(Schedule);
  }

  Serial.println("[EEPROM] Loaded");
}

void clearAllSchedules() {
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    clearScheduleData(i);
  }

  saveEEPROM();
}

// ================= FEEDING =================
void feedFish(int grams, const char* fish, const char* pond) {
  DateTime now = rtc.now();

  Serial.println("================================");
  Serial.println("[FEED] Triggered");
  Serial.print("[FEED] Time: ");
  Serial.print(now.hour());
  Serial.print(":");
  Serial.println(now.minute());
  Serial.print("[FEED] Pond: ");
  Serial.println(pond);
  Serial.print("[FEED] Fish: ");
  Serial.println(fish);
  Serial.print("[FEED] Grams: ");
  Serial.println(grams);

  sendFeedGrams(grams);

  Serial.println("[FEED] Sent to feeder");
  Serial.println("================================");
}

void checkSchedules() {
  if (!rtcOK) return;

  DateTime now = rtc.now();

  int currentHour = now.hour();
  int currentMinute = now.minute();

  if (currentMinute == lastMinute) return;

  lastMinute = currentMinute;

  if (currentHour == 0 && currentMinute == 0) {
    schedules[0].fedToday = false;
  }

  if (schedules[0].active && !schedules[0].fedToday) {
    if (schedules[0].hour == currentHour &&
        schedules[0].minute == currentMinute) {

      feedFish(
        schedules[0].grams,
        schedules[0].fish,
        schedules[0].pond
      );

      schedules[0].fedToday = true;
    }
  }
}

// ================= JSON HELPERS =================
int extract(String j, String key) {
  int i = j.indexOf("\"" + key + "\":");

  if (i == -1) return 0;

  i += key.length() + 3;

  int e = i;

  while (e < j.length() && (isDigit(j[e]) || j[e] == '-')) {
    e++;
  }

  return j.substring(i, e).toInt();
}

void extractStr(String j, String key, char* out) {
  int i = j.indexOf("\"" + key + "\":\"");

  if (i == -1) {
    out[0] = 0;
    return;
  }

  i += key.length() + 4;

  int e = j.indexOf("\"", i);

  String val = j.substring(i, e);

  val.toCharArray(out, 20);
}

// ================= WEB UI =================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>IKAN Hub</title>
  <style>
    body {
      font-family: Arial;
      background: #f6f8fb;
      margin: 0;
      color: #111827;
    }

    .top {
      background: #0f172a;
      color: white;
      padding: 18px;
      text-align: center;
    }

    .container {
      padding: 16px;
      max-width: 900px;
      margin: auto;
    }

    .card {
      background: white;
      padding: 16px;
      border-radius: 16px;
      margin-bottom: 16px;
      box-shadow: 0 4px 14px rgba(0,0,0,0.08);
    }

    input, button {
      width: 100%;
      padding: 12px;
      margin-top: 8px;
      border-radius: 10px;
      border: 1px solid #d1d5db;
      font-size: 15px;
      box-sizing: border-box;
    }

    button {
      background: #0f172a;
      color: white;
      font-weight: bold;
      border: none;
      cursor: pointer;
    }

    .danger { background: #991b1b; }
    .success { background: #166534; }
    .disabled { background: #9ca3af; }

    .schedule {
      border: 1px solid #e5e7eb;
      border-radius: 12px;
      padding: 12px;
      margin-top: 10px;
      background: #f8fafc;
    }

    .metric {
      font-size: 20px;
      font-weight: bold;
      margin: 8px 0;
    }

    .small {
      color: #6b7280;
      font-size: 13px;
    }
  </style>
</head>

<body>
  <div class="top">
    <h1>IKAN Hub</h1>
    <p id="time">Loading time...</p>
    <p id="status">Loading status...</p>
  </div>

  <div class="container">

    <div class="card">
      <h2>Pond 1 Status</h2>
      <div class="metric" id="tempBox">Temperature: -- °C</div>
      <div class="metric" id="actBox">Activity: --</div>
      <p class="small" id="rawStatus">Waiting for feeder...</p>
    </div>

    <div class="card">
      <h2>Sync Time</h2>
      <button onclick="syncTime()">Sync ESP Time From Phone</button>
    </div>

    <div class="card">
      <h2>Manual Feed</h2>
      <input id="manualGrams" type="number" placeholder="Grams e.g. 30">
      <button class="success" onclick="manualFeed()">Feed Now</button>
    </div>

    <div class="card">
      <h2>One Feeder Schedule</h2>
      <input id="pond" placeholder="Pond name" value="Pond 1">
      <input id="fish" placeholder="Fish name">
      <input id="feedTime" type="time">
      <input id="grams" type="number" placeholder="Grams">
      <button onclick="saveSchedule()">Save Schedule</button>
      <button class="disabled" onclick="addFeeder()">+ Add Feeder</button>
      <p class="small">Only 1 feeder is supported in this demo.</p>
    </div>

    <div class="card">
      <h2>Saved Schedule</h2>
      <div id="scheduleList">Loading...</div>
      <button class="danger" onclick="clearAll()">Clear Schedule</button>
    </div>

    <div class="card">
      <h2>LoRa Test</h2>
      <button onclick="testLoRa()">Send Test G:5</button>
    </div>

    <div class="card">
      <h2>WiFi Settings</h2>
      <input id="newssid" placeholder="New SSID">
      <input id="newpass" placeholder="New Password">
      <button onclick="setWiFi()">Save WiFi Settings</button>
    </div>

    <div class="card">
      <h2>Firmware Update</h2>
      <p>Install a compiled ESP32 firmware .bin file over WiFi.</p>
      <button onclick="location.href='/update'">Open OTA Updater</button>
    </div>

  </div>

<script>
function updateTime() {
  fetch('/time')
    .then(r => r.json())
    .then(d => {
      document.getElementById('time').innerText =
        'Time: ' +
        String(d.hour).padStart(2,'0') + ':' +
        String(d.minute).padStart(2,'0') + ':' +
        String(d.second).padStart(2,'0');

      document.getElementById('status').innerText =
        'LoRa: ' + d.lora + ' | RTC: ' + d.rtc;

      document.getElementById('tempBox').innerText =
        'Temperature: ' + d.temp.toFixed(2) + ' °C';

      document.getElementById('actBox').innerText =
        'Activity: ' + d.activity.toFixed(2);

      document.getElementById('rawStatus').innerText =
        'Raw feeder status: ' + d.feeder;
    });
}

function syncTime() {
  const now = new Date();

  fetch('/set', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
      year: now.getFullYear(),
      month: now.getMonth() + 1,
      day: now.getDate(),
      hour: now.getHours(),
      minute: now.getMinutes(),
      second: now.getSeconds()
    })
  }).then(() => alert('Time synced'));
}

function manualFeed() {
  const grams = Number(document.getElementById('manualGrams').value);

  if (grams <= 0) {
    alert('Enter valid grams');
    return;
  }

  fetch('/feed', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
      grams: grams,
      fish: 'Manual',
      pond: 'Pond 1'
    })
  }).then(() => alert('Sent G:' + grams));
}

function saveSchedule() {
  const pond = document.getElementById('pond').value;
  const fish = document.getElementById('fish').value;
  const time = document.getElementById('feedTime').value;
  const grams = Number(document.getElementById('grams').value);

  if (!pond || !fish || !time || grams <= 0) {
    alert('Fill all fields');
    return;
  }

  const parts = time.split(':');

  fetch('/add', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
      pond: pond,
      fish: fish,
      hour: Number(parts[0]),
      minute: Number(parts[1]),
      grams: grams
    })
  }).then(() => {
    alert('Schedule saved');
    loadSchedules();
  });
}

function addFeeder() {
  alert('Only 1 feeder is supported in this demo.');
}

function loadSchedules() {
  fetch('/list')
    .then(r => r.json())
    .then(data => {
      let html = '';

      if (data.length === 0) {
        html = '<p>No schedule saved.</p>';
      }

      data.forEach(s => {
        html += '<div class="schedule">';
        html += '<b>' + s.pond + '</b><br>';
        html += s.fish + '<br>';
        html += String(s.hour).padStart(2,'0') + ':' + String(s.minute).padStart(2,'0');
        html += ' - ' + s.grams + 'g<br>';
        html += '</div>';
      });

      document.getElementById('scheduleList').innerHTML = html;
    });
}

function clearAll() {
  if (!confirm('Clear schedule?')) return;

  fetch('/clearall')
    .then(() => loadSchedules());
}

function testLoRa() {
  fetch('/testlora')
    .then(() => alert('Sent G:5'));
}

function setWiFi() {
  const ssid = document.getElementById('newssid').value;
  const pass = document.getElementById('newpass').value;

  fetch('/setwifi', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
      ssid: ssid,
      pass: pass
    })
  }).then(() => alert('WiFi saved. Restart ESP32.'));
}

setInterval(updateTime, 1000);
updateTime();
loadSchedules();
</script>
</body>
</html>
)rawliteral";

// ================= WEB ROUTES =================
void setupWebServer() {
  server.on("/", []() {
    server.send(200, "text/html", index_html);
  });

  server.on("/time", HTTP_GET, []() {
    DateTime n = rtc.now();

    String j = "{";
    j += "\"hour\":" + String(n.hour()) + ",";
    j += "\"minute\":" + String(n.minute()) + ",";
    j += "\"second\":" + String(n.second()) + ",";
    j += "\"rtc\":\"" + String(rtcOK ? "OK" : "ERR") + "\",";
    j += "\"lora\":\"" + String(loraOK ? "OK" : "ERR") + "\",";
    j += "\"feeder\":\"" + lastFeederStatus + "\",";
    j += "\"temp\":" + String(pondTemp, 2) + ",";
    j += "\"activity\":" + String(pondActivity, 2);
    j += "}";

    server.send(200, "application/json", j);
  });

  server.on("/set", HTTP_POST, []() {
    String b = server.arg("plain");

    rtc.adjust(DateTime(
      extract(b, "year"),
      extract(b, "month"),
      extract(b, "day"),
      extract(b, "hour"),
      extract(b, "minute"),
      extract(b, "second")
    ));

    server.send(200, "text/plain", "OK");
  });

  server.on("/feed", HTTP_POST, []() {
    String b = server.arg("plain");

    int grams = extract(b, "grams");

    char fish[20];
    char pond[20];

    extractStr(b, "fish", fish);
    extractStr(b, "pond", pond);

    feedFish(grams, fish, pond);

    server.send(200, "text/plain", "Fed");
  });

  server.on("/add", HTTP_POST, []() {
    String b = server.arg("plain");

    clearScheduleData(0);

    schedules[0].hour = extract(b, "hour");
    schedules[0].minute = extract(b, "minute");
    schedules[0].grams = extract(b, "grams");

    extractStr(b, "fish", schedules[0].fish);
    extractStr(b, "pond", schedules[0].pond);

    schedules[0].active = true;
    schedules[0].fedToday = false;

    saveEEPROM();

    server.send(200, "text/plain", "OK");
  });

  server.on("/list", HTTP_GET, []() {
    String json = "[";

    if (schedules[0].active) {
      json += "{";
      json += "\"hour\":" + String(schedules[0].hour) + ",";
      json += "\"minute\":" + String(schedules[0].minute) + ",";
      json += "\"grams\":" + String(schedules[0].grams) + ",";
      json += "\"fish\":\"" + String(schedules[0].fish) + "\",";
      json += "\"pond\":\"" + String(schedules[0].pond) + "\",";
      json += "\"index\":0";
      json += "}";
    }

    json += "]";

    server.send(200, "application/json", json);
  });

  server.on("/clearall", []() {
    clearAllSchedules();
    server.send(200, "text/plain", "OK");
  });

  server.on("/testlora", []() {
    sendFeedGrams(5);
    server.send(200, "text/plain", "Sent G:5");
  });

  server.on("/setwifi", HTTP_POST, []() {
    String b = server.arg("plain");

    extractStr(b, "ssid", ssid);
    extractStr(b, "pass", password);

    saveEEPROM();

    server.send(200, "text/plain", "Saved");
  });

  server.on("/update", HTTP_GET, []() {
    if (!server.authenticate("admin", password)) {
      server.requestAuthentication();
      return;
    }

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>IKAN Hub OTA</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 560px; margin: 40px auto; padding: 0 18px; background: #f4f7fb; color: #172033; }
    .card { background: white; padding: 24px; border-radius: 14px; box-shadow: 0 8px 24px #0002; }
    input, button { width: 100%; box-sizing: border-box; margin-top: 14px; padding: 12px; }
    button { border: 0; border-radius: 8px; background: #1769e0; color: white; font-weight: 700; cursor: pointer; }
    progress { width: 100%; margin-top: 18px; }
    #status { min-height: 1.5em; margin-top: 12px; }
  </style>
</head>
<body>
  <div class="card">
    <h2>IKAN Hub Firmware Update</h2>
    <p>Select the sketch firmware <strong>.bin</strong>. Keep power connected until the hub restarts.</p>
    <form id="otaForm">
      <input id="firmware" type="file" name="firmware" accept=".bin,application/octet-stream" required>
      <button type="submit">Install Firmware</button>
    </form>
    <progress id="progress" value="0" max="100"></progress>
    <div id="status"></div>
  </div>
  <script>
    const form = document.getElementById('otaForm');
    const fileInput = document.getElementById('firmware');
    const progress = document.getElementById('progress');
    const statusBox = document.getElementById('status');

    form.addEventListener('submit', event => {
      event.preventDefault();
      if (!fileInput.files.length) return;

      const data = new FormData();
      data.append('firmware', fileInput.files[0]);

      const request = new XMLHttpRequest();
      request.open('POST', '/update');
      request.upload.onprogress = event => {
        if (event.lengthComputable) progress.value = Math.round(event.loaded * 100 / event.total);
      };
      request.onload = () => {
        statusBox.textContent = request.responseText;
        if (request.status === 200) progress.value = 100;
      };
      request.onerror = () => { statusBox.textContent = 'Upload connection failed.'; };
      statusBox.textContent = 'Uploading. Do not remove power...';
      request.send(data);
    });
  </script>
</body>
</html>
)rawliteral");
  });

  server.on(
    "/update",
    HTTP_POST,
    []() {
      if (!server.authenticate("admin", password)) {
        server.requestAuthentication();
        otaInProgress = false;
        return;
      }

      bool failed = otaUploadRejected || !otaUploadSucceeded || Update.hasError();
      server.sendHeader("Connection", "close");
      server.send(
        failed ? 500 : 200,
        "text/plain",
        failed ? "Firmware update failed. Check the serial log."
               : "Firmware installed. Hub is restarting..."
      );

      otaInProgress = false;
      otaUploadSucceeded = false;
      if (!failed) {
        delay(750);
        ESP.restart();
      }
    },
    []() {
      if (!server.authenticate("admin", password)) return;

      HTTPUpload &upload = server.upload();

      if (upload.status == UPLOAD_FILE_START) {
        otaInProgress = true;
        otaUploadRejected = false;
        otaUploadSucceeded = false;
        Serial.printf("[OTA] Starting: %s\n", upload.filename.c_str());

        if (!upload.filename.endsWith(".bin")) {
          Serial.println("[OTA] ERROR: File must end in .bin");
          otaUploadRejected = true;
          return;
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!otaUploadRejected && !Update.hasError() &&
            Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (!otaUploadRejected && Update.end(true)) {
          otaUploadSucceeded = true;
          Serial.printf("[OTA] Complete: %u bytes\n", upload.totalSize);
        } else if (!otaUploadRejected) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        otaInProgress = false;
        otaUploadRejected = true;
        otaUploadSucceeded = false;
        Serial.println("[OTA] Upload aborted");
      }
    }
  );

  server.begin();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("IKAN TRANSMITTER HUB STARTING");

  EEPROM.begin(EEPROM_SIZE);
  loadEEPROM();

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!rtc.begin()) {
    rtcOK = false;
    Serial.println("[RTC] ERROR");
  } else {
    rtcOK = true;
    Serial.println("[RTC] OK");
  }

  setupOLED();
  setupLoRa();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  Serial.print("[WIFI] SSID: ");
  Serial.println(ssid);

  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.softAPIP());

  dnsServer.start(53, "*", WiFi.softAPIP());

  setupWebServer();

  Serial.println("READY");
}

// ================= LOOP =================
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (!otaInProgress) {
    checkSchedules();
    readLoRaStatus();
    updateDisplay();
  }

  delay(10);
}
