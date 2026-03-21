/*
 * ============================================
 *   FaceGuard IoT - ESP32 MQTT Client
 *   Broker : HiveMQ Public
 *   Author : quang_1307
 * ============================================
 *
 * TOPIC LAYOUT:
 *   SUB  smartlock/quang_1307/command   <- nhận lệnh từ backend
 *   PUB  smartlock/quang_1307/status    <- gửi trạng thái lên backend
 *   PUB  smartlock/quang_1307/heartbeat <- ping định kỳ
 *
 * LỆNH backend gửi xuống (JSON):
 *   {"action":"unlock"}   -> mở khóa 3 giây
 *   {"action":"lock"}     -> khoá ngay
 *   {"action":"ping"}     -> kiểm tra online
 *
 * TRẠNG THÁI ESP gửi lên (JSON):
 *   {"event":"unlocked","trigger":"web"}
 *   {"event":"locked"}
 *   {"event":"online"}
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ──────────────────────────────────────────
//  CẤU HÌNH WiFi
// ──────────────────────────────────────────
const char *ssid     = "Xoi Banh My Chi Nga";
const char *password = "13071982";

// ──────────────────────────────────────────
//  CẤU HÌNH MQTT
// ──────────────────────────────────────────
const char *mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;
const char *mqtt_prefix = "smartlock/quang_1307";

// Topics
const char *TOPIC_COMMAND   = "smartlock/quang_1307/command";
const char *TOPIC_STATUS    = "smartlock/quang_1307/status";
const char *TOPIC_HEARTBEAT = "smartlock/quang_1307/heartbeat";

// Client ID phải unique trên broker public
const char *CLIENT_ID = "esp32-smartlock-quang1307";

// ──────────────────────────────────────────
//  PHẦN CỨNG
// ──────────────────────────────────────────
const int RELAY_PIN   = 26;   // relay điều khiển khóa (HIGH = mở)
const int LED_GREEN   = 27;   // LED xanh = granted
const int LED_RED     = 14;   // LED đỏ  = locked / denied
const int LOCK_DURATION_MS = 3000;  // giữ mở 3 giây

// ──────────────────────────────────────────
//  BIẾN TOÀN CỤC
// ──────────────────────────────────────────
WiFiClient   espClient;
PubSubClient mqtt(espClient);

bool     isLocked        = true;
unsigned long unlockTime = 0;     // thời điểm mở khóa
unsigned long lastHeartbeat = 0;
const long HEARTBEAT_INTERVAL = 15000; // 15 giây

// ──────────────────────────────────────────
//  KẾT NỐI WiFi
// ──────────────────────────────────────────
void connectWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++attempt > 40) {
      Serial.println("\n[WiFi] TIMEOUT – restarting...");
      ESP.restart();
    }
  }

  Serial.println();
  Serial.print("[WiFi] Connected! IP: ");
  Serial.println(WiFi.localIP());
}

// ──────────────────────────────────────────
//  CALLBACK: Nhận message từ broker
// ──────────────────────────────────────────
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  // Chuyển payload sang String
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("[MQTT] Received on [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(msg);

  // Parse JSON
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) {
    Serial.println("[MQTT] JSON parse error!");
    return;
  }

  const char *action = doc["action"];
  if (!action) return;
  
  const char *user = doc["user"] | "Unknown";
  const char *time_str = doc["time"] | "";

  // ── Xử lý lệnh ──
  if (strcmp(action, "unlock") == 0) {
    doUnlock(user, time_str);
  }
  else if (strcmp(action, "lock") == 0) {
    doLock();
  }
  else if (strcmp(action, "ping") == 0) {
    publishStatus("online", "pong");
  }
}

// ──────────────────────────────────────────
//  KẾT NỐI MQTT
// ──────────────────────────────────────────
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("[MQTT] Connecting...");

    if (mqtt.connect(CLIENT_ID)) {
      Serial.println(" OK!");

      // Subscribe topic lệnh
      mqtt.subscribe(TOPIC_COMMAND);
      Serial.print("[MQTT] Subscribed: ");
      Serial.println(TOPIC_COMMAND);

      // Thông báo online
      publishStatus("online", "boot");
    }
    else {
      Serial.print(" Failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" – retry in 3s");
      delay(3000);
    }
  }
}

// ──────────────────────────────────────────
//  PUBLISH TRẠNG THÁI
// ──────────────────────────────────────────
void publishStatus(const char *event, const char *trigger) {
  StaticJsonDocument<128> doc;
  doc["event"]   = event;
  doc["trigger"] = trigger;

  char buf[128];
  serializeJson(doc, buf);

  mqtt.publish(TOPIC_STATUS, buf, true);  // retain = true
  Serial.print("[MQTT] Published status: ");
  Serial.println(buf);
}


void doUnlock(const char *user, const char *time_str) {
  if (strlen(time_str) > 0) {
    Serial.print(time_str);
    Serial.print(" ");
  }
  Serial.println(user);
  Serial.println("[LOCK] >>> UNLOCKED <<<");
  
  isLocked   = false;
  unlockTime = millis();

  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED,   LOW);

  publishStatus("unlocked", user);
}

// ──────────────────────────────────────────
//  KHÓA LẠI
// ──────────────────────────────────────────
void doLock() {
  Serial.println("[LOCK] >>> LOCKED <<<");
  isLocked = true;

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED,   HIGH);

  publishStatus("locked", "auto");
}

// ──────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== FaceGuard ESP32 MQTT ===");

  // GPIO
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED,   OUTPUT);
  doLock();  // bắt đầu ở trạng thái khóa

  // Kết nối
  connectWiFi();
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);
  connectMQTT();
}

// ──────────────────────────────────────────
//  LOOP
// ──────────────────────────────────────────
void loop() {
  // Đảm bảo MQTT luôn kết nối
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();

  // Tự khóa lại sau LOCK_DURATION_MS
  if (!isLocked && (millis() - unlockTime >= LOCK_DURATION_MS)) {
    doLock();
  }

  // Heartbeat định kỳ
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();

    StaticJsonDocument<64> hb;
    hb["status"] = "alive";
    hb["uptime"] = millis() / 1000;
    char buf[64];
    serializeJson(hb, buf);
    mqtt.publish(TOPIC_HEARTBEAT, buf);
    Serial.print("[HB] ");
    Serial.println(buf);
  }
}
