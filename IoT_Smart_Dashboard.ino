#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DHT.h>
#include <Firebase_ESP_Client.h>

// เพิ่ม TokenHelper เพื่อให้ Firebase Client ทำงานได้สมบูรณ์
#include "addons/TokenHelper.h"

// ---------- WiFi Credentials ----------
const char* ssid = "KKICT-STAFF";
const char* password = "0024102549";

// ---------- Firebase Credentials ----------
#define API_KEY "AIzaSyCU6JtAoHWJi0oqdXsxjgJZJG_hYNi1XrY"   // <-- ใส่ Web API Key ของคุณ
#define DATABASE_URL "https://smart-iot-dashboard-c7f44-default-rtdb.asia-southeast1.firebasedatabase.app"

// ---------- Pin Definitions ----------
#define DHTPIN 5
#define DHTTYPE DHT22
#define LDR_PIN 32
#define RELAY1_PIN 18
#define RELAY2_PIN 19

// ---------- Global Variables ----------
DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);

// Sensor readings
float temperature = 0;
float humidity = 0;
int ldrPercent = 0;

// Relay states
bool relay1State = LOW;
bool relay2State = LOW;

// Flags สำหรับการอัปเดต Firebase จากหน้าเว็บ (ป้องกัน Task ชนกัน)
bool reqRelay1Update = false;
bool reqRelay2Update = false;

// Timing
unsigned long lastReadTime = 0;
const unsigned long readInterval = 2000;      // read sensors every 2 seconds
unsigned long lastFirebaseUpdate = 0;
const unsigned long firebaseInterval = 5000;  // send to Firebase every 5 seconds

// Firebase objects
FirebaseData fbdo;          // สำหรับส่งข้อมูล (Set/Get)
FirebaseData streamFbdo;    // แยกตัวแปร สำหรับดักฟังข้อมูล (Stream) โดยเฉพาะ
FirebaseAuth auth;
FirebaseConfig config;

// Connection status
bool firebaseReady = false;

// ---------- HTML Dashboard (embedded) ----------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="th">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>Smart IoT Dashboard</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; }
    body { background: linear-gradient(135deg, #0f2027, #203a43, #2c5364); min-height: 100vh; padding: 30px 20px; }
    .dashboard { max-width: 1300px; margin: 0 auto; background: rgba(255, 255, 255, 0.1); backdrop-filter: blur(12px); border-radius: 40px; padding: 25px; box-shadow: 0 25px 45px rgba(0, 0, 0, 0.2); border: 1px solid rgba(255, 255, 255, 0.2); }
    .header { text-align: center; margin-bottom: 30px; }
    .header h1 { font-size: 2.2rem; color: #fff; text-shadow: 2px 2px 4px rgba(0,0,0,0.3); letter-spacing: 1px; }
    .header p { color: #e0e0e0; font-size: 1rem; }
    .sensor-panel { display: flex; flex-wrap: wrap; gap: 20px; justify-content: center; margin-bottom: 40px; }
    .card { flex: 1; min-width: 180px; background: rgba(255, 255, 255, 0.2); backdrop-filter: blur(4px); border-radius: 28px; padding: 20px; text-align: center; transition: transform 0.3s ease; border: 1px solid rgba(255,255,255,0.3); }
    .card:hover { transform: translateY(-5px); background: rgba(255, 255, 255, 0.25); }
    .card h3 { font-size: 1.2rem; color: #f1f1f1; margin-bottom: 10px; }
    .card .value { font-size: 2.5rem; font-weight: bold; color: #fff; }
    .card .unit { font-size: 1rem; color: #ddd; }
    .charts-section { margin-bottom: 40px; }
    .charts-section h2 { text-align: center; color: #fff; margin-bottom: 20px; font-weight: 500; letter-spacing: 1px; }
    .charts-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 25px; }
    .chart-card { background: rgba(0, 0, 0, 0.3); backdrop-filter: blur(4px); border-radius: 28px; padding: 15px; border: 1px solid rgba(255,255,255,0.2); }
    .chart-card canvas { max-height: 280px; width: 100%; }
    .control-panel { display: flex; flex-wrap: wrap; gap: 20px; justify-content: center; margin-bottom: 20px; }
    .relay { flex: 1; min-width: 240px; background: rgba(0, 0, 0, 0.4); backdrop-filter: blur(4px); border-radius: 28px; padding: 15px 20px; display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; }
    .relay h3 { color: #fff; font-size: 1.2rem; margin: 0; }
    .relay-status { font-weight: bold; padding: 6px 16px; border-radius: 40px; background: #bdc3c7; color: #2c3e50; font-size: 0.9rem; }
    .relay-status.on { background: #2ecc71; color: white; box-shadow: 0 0 8px #2ecc71; }
    .relay-status.off { background: #e74c3c; color: white; }
    button { background: linear-gradient(135deg, #667eea, #764ba2); border: none; color: white; padding: 8px 20px; border-radius: 40px; cursor: pointer; font-weight: bold; transition: 0.2s; box-shadow: 0 2px 5px rgba(0,0,0,0.2); }
    button:hover { transform: scale(1.02); opacity: 0.9; }
    .footer { text-align: center; padding: 15px; color: #ccc; font-size: 0.8rem; border-top: 1px solid rgba(255,255,255,0.1); margin-top: 20px; }
    @media (max-width: 768px) { .dashboard { padding: 15px; } .card .value { font-size: 1.8rem; } .charts-grid { grid-template-columns: 1fr; } .relay { flex-direction: column; align-items: flex-start; } }
  </style>
</head>
<body>
<div class="dashboard">
  <div class="header">
    <h1>📡 Smart IoT Dashboard</h1>
    <p>Real‑time Monitoring & Control | ESP32 Web Server + Firebase</p>
  </div>
  <div class="sensor-panel">
    <div class="card"><h3>🌡️ อุณหภูมิ</h3><p class="value"><span id="temp">--</span><span class="unit"> °C</span></p></div>
    <div class="card"><h3>💧 ความชื้น</h3><p class="value"><span id="hum">--</span><span class="unit"> %</span></p></div>
    <div class="card"><h3>💡 ความสว่าง</h3><p class="value"><span id="ldr">--</span><span class="unit"> %</span></p></div>
  </div>
  <div class="charts-section">
    <h2>📈 แนวโน้มข้อมูล (20 จุดล่าสุด)</h2>
    <div class="charts-grid">
      <div class="chart-card"><canvas id="tempChart"></canvas></div>
      <div class="chart-card"><canvas id="humChart"></canvas></div>
      <div class="chart-card"><canvas id="ldrChart"></canvas></div>
    </div>
  </div>
  <div class="control-panel">
    <div class="relay"><h3>🔌 รีเลย์ 1</h3><span id="relay1Status" class="relay-status off">ปิด</span><button onclick="toggleRelay(1)">สลับสถานะ</button></div>
    <div class="relay"><h3>🔌 รีเลย์ 2</h3><span id="relay2Status" class="relay-status off">ปิด</span><button onclick="toggleRelay(2)">สลับสถานะ</button></div>
  </div>
  <div class="footer">อัปเดตล่าสุด: <span id="lastUpdate">--:--:--</span></div>
</div>
<script>
  let tempHistory = [], humHistory = [], ldrHistory = [], timeLabels = [];
  const MAX_POINTS = 20;
  let tempChart, humChart, ldrChart;

  function initCharts() {
    const commonOptions = { responsive: true, maintainAspectRatio: true, plugins: { legend: { labels: { color: '#fff' } }, tooltip: { mode: 'index', intersect: false } }, scales: { y: { grid: { color: 'rgba(255,255,255,0.1)' }, ticks: { color: '#fff' } }, x: { grid: { color: 'rgba(255,255,255,0.1)' }, ticks: { color: '#fff' } } } };
    tempChart = new Chart(document.getElementById('tempChart'), { type: 'line', data: { labels: timeLabels, datasets: [{ label: 'อุณหภูมิ (°C)', data: tempHistory, borderColor: '#ff9f4a', backgroundColor: 'rgba(255,159,74,0.1)', tension: 0.2, fill: true, pointRadius: 3 }] }, options: commonOptions });
    humChart = new Chart(document.getElementById('humChart'), { type: 'line', data: { labels: timeLabels, datasets: [{ label: 'ความชื้น (%)', data: humHistory, borderColor: '#4bc0c0', backgroundColor: 'rgba(75,192,192,0.1)', tension: 0.2, fill: true, pointRadius: 3 }] }, options: commonOptions });
    ldrChart = new Chart(document.getElementById('ldrChart'), { type: 'line', data: { labels: timeLabels, datasets: [{ label: 'ความสว่าง (%)', data: ldrHistory, borderColor: '#f7b32b', backgroundColor: 'rgba(247,179,43,0.1)', tension: 0.2, fill: true, pointRadius: 3 }] }, options: commonOptions });
  }
  function updateCharts(temp, hum, ldr) {
    const now = new Date(); const timeStr = now.toLocaleTimeString('th-TH', { hour: '2-digit', minute:'2-digit', second:'2-digit' });
    tempHistory.push(temp); humHistory.push(hum); ldrHistory.push(ldr); timeLabels.push(timeStr);
    while (tempHistory.length > MAX_POINTS) tempHistory.shift(); while (humHistory.length > MAX_POINTS) humHistory.shift(); while (ldrHistory.length > MAX_POINTS) ldrHistory.shift(); while (timeLabels.length > MAX_POINTS) timeLabels.shift();
    tempChart.data.labels = timeLabels; tempChart.data.datasets[0].data = tempHistory; tempChart.update();
    humChart.data.labels = timeLabels; humChart.data.datasets[0].data = humHistory; humChart.update();
    ldrChart.data.labels = timeLabels; ldrChart.data.datasets[0].data = ldrHistory; ldrChart.update();
  }
  async function fetchData() {
    try {
      const response = await fetch('/api/sensors');
      if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
      const data = await response.json();
      document.getElementById('temp').innerText = data.temperature.toFixed(1);
      document.getElementById('hum').innerText = data.humidity.toFixed(1);
      document.getElementById('ldr').innerText = data.ldr;
      document.getElementById('lastUpdate').innerText = new Date().toLocaleTimeString('th-TH');
      document.getElementById('relay1Status').innerText = data.relay1 ? 'เปิด' : 'ปิด';
      document.getElementById('relay1Status').className = 'relay-status ' + (data.relay1 ? 'on' : 'off');
      document.getElementById('relay2Status').innerText = data.relay2 ? 'เปิด' : 'ปิด';
      document.getElementById('relay2Status').className = 'relay-status ' + (data.relay2 ? 'on' : 'off');
      updateCharts(data.temperature, data.humidity, data.ldr);
    } catch (error) { console.error('Error fetching data:', error); }
  }
  async function toggleRelay(relayNumber) {
    try {
      const response = await fetch(`/api/relay?relay=${relayNumber}`);
      const result = await response.json();
      if (result.success) fetchData(); else alert('เกิดข้อผิดพลาด');
    } catch (error) { console.error('Error toggling relay:', error); }
  }
  initCharts(); fetchData(); setInterval(fetchData, 2000);
</script>
</body>
</html>
)rawliteral";

// ---------- Helper Functions ----------
void updateRelay(int relayPin, bool state) {
  digitalWrite(relayPin, state ? HIGH : LOW);
}

void readSensors() {
  float newTemp = dht.readTemperature();
  float newHum = dht.readHumidity();
  if (!isnan(newTemp) && !isnan(newHum)) {
    temperature = newTemp;
    humidity = newHum;
    Serial.printf("DHT - Temp: %.2f °C, Hum: %.2f %%\n", temperature, humidity);
  } else {
    Serial.println("DHT read error!");
  }
  int rawLdr = analogRead(LDR_PIN);
  ldrPercent = map(rawLdr, 0, 4095, 0, 100);
  ldrPercent = constrain(ldrPercent, 0, 100);
}

// ---------- Firebase Functions ----------
void sendToFirebase() {
  if (!firebaseReady) return;

  // ใช้ fbdo ตัวปกติสำหรับการส่งข้อมูล
  Firebase.RTDB.setFloat(&fbdo, "/sensor/temperature", temperature);
  Firebase.RTDB.setFloat(&fbdo, "/sensor/humidity", humidity);
  Firebase.RTDB.setInt(&fbdo, "/sensor/ldr", ldrPercent);
  Firebase.RTDB.setBool(&fbdo, "/device/relay1", relay1State);
  Firebase.RTDB.setBool(&fbdo, "/device/relay2", relay2State);
}

// Firebase Stream Callbacks
void streamCallback(FirebaseStream data) {
  if (data.dataPath() == "/relay1") {
    if (data.dataType() == "boolean") {
      bool newState = data.boolData();
      if (newState != relay1State) {
        relay1State = newState;
        updateRelay(RELAY1_PIN, relay1State);
        Serial.printf("Firebase: relay1 changed to %d\n", relay1State);
      }
    }
  } else if (data.dataPath() == "/relay2") {
    if (data.dataType() == "boolean") {
      bool newState = data.boolData();
      if (newState != relay2State) {
        relay2State = newState;
        updateRelay(RELAY2_PIN, relay2State);
        Serial.printf("Firebase: relay2 changed to %d\n", relay2State);
      }
    }
  }
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("Firebase stream timeout, reconnecting...");
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 Smart IoT Dashboard Starting...");

  // Initialize pins
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  updateRelay(RELAY1_PIN, relay1State);
  updateRelay(RELAY2_PIN, relay2State);

  dht.begin();
  
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // ---------- Firebase Configuration ----------
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  
  // สร้างการเข้าสู่ระบบแบบไม่ระบุตัวตน (ต้องเปิด Anonymous ใน Firebase Auth ด้วย)
  Serial.print("Signing in to Firebase... ");
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("OK");
    firebaseReady = true;
  } else {
    Serial.printf("FAILED: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback; // จัดการ Token อัตโนมัติ
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (firebaseReady) {
    // ใช้ streamFbdo สำหรับรับข้อมูล Stream เท่านั้น
    if (!Firebase.RTDB.beginStream(&streamFbdo, "/device")) {
      Serial.println("Stream Failed: " + streamFbdo.errorReason());
    } else {
      Firebase.RTDB.setStreamCallback(&streamFbdo, streamCallback, streamTimeoutCallback);
      Serial.println("Firebase stream started");
    }

    // Read initial state (ใช้ fbdo ตัวปกติ)
    if (Firebase.RTDB.getBool(&fbdo, "/device/relay1")) {
      relay1State = fbdo.boolData();
      updateRelay(RELAY1_PIN, relay1State);
    }
    if (Firebase.RTDB.getBool(&fbdo, "/device/relay2")) {
      relay2State = fbdo.boolData();
      updateRelay(RELAY2_PIN, relay2State);
    }
  }

  // ---------- Web Server Endpoints ----------
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"temperature\":" + String(temperature) + ",";
    json += "\"humidity\":" + String(humidity) + ",";
    json += "\"ldr\":" + String(ldrPercent) + ",";
    json += "\"relay1\":" + String(relay1State ? 1 : 0) + ",";
    json += "\"relay2\":" + String(relay2State ? 1 : 0);
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/relay", HTTP_GET, [](AsyncWebServerRequest *request) {
    int relayNum = 0;
    if (request->hasParam("relay")) {
      relayNum = request->getParam("relay")->value().toInt();
    }
    bool success = false;
    
    // ตั้งค่าตัวแปร Flag แล้วปล่อยให้ loop() เป็นคนจัดการส่ง Firebase 
    // (ห้ามเรียก Firebase.RTDB ในนี้เด็ดขาดเพราะจะเกิด Thread Crash)
    if (relayNum == 1) {
      relay1State = !relay1State;
      updateRelay(RELAY1_PIN, relay1State);
      reqRelay1Update = true; 
      success = true;
    } else if (relayNum == 2) {
      relay2State = !relay2State;
      updateRelay(RELAY2_PIN, relay2State);
      reqRelay2Update = true;
      success = true;
    }
    String json = "{\"success\":" + String(success ? "true" : "false") + "}";
    request->send(200, "application/json", json);
  });

  server.begin();
  Serial.println("HTTP server started.");
}

// ---------- Main Loop ----------
void loop() {
  
  // อัปเดต Relay1 ขึ้น Firebase (หากมีการกดจาก Web Server)
  if (reqRelay1Update) {
    reqRelay1Update = false;
    if (firebaseReady) Firebase.RTDB.setBool(&fbdo, "/device/relay1", relay1State);
  }

  // อัปเดต Relay2 ขึ้น Firebase (หากมีการกดจาก Web Server)
  if (reqRelay2Update) {
    reqRelay2Update = false;
    if (firebaseReady) Firebase.RTDB.setBool(&fbdo, "/device/relay2", relay2State);
  }

  // Read sensors every readInterval
  if (millis() - lastReadTime >= readInterval) {
    readSensors();
    lastReadTime = millis();
  }

  // Send data to Firebase every firebaseInterval
  if (firebaseReady && (millis() - lastFirebaseUpdate >= firebaseInterval)) {
    sendToFirebase();
    lastFirebaseUpdate = millis();
  }

  delay(10);
}