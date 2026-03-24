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
// Cửa và thời gian chờ được thực hiện bên STM32. ESP32 không còn xử lý Relay/Timer đóng dóng.

// ──────────────────────────────────────────
//  UART GIAO TIẾP VỚI STM32
//  STM32 USART1 (Remap): PB6=TX, PB7=RX
//  ESP32 TX2 (GPIO17) → STM32 PB7 (USART1 RX)
//  ESP32 RX2 (GPIO16) ← STM32 PB6 (USART1 TX)
//  Nối chung GND!
// ──────────────────────────────────────────
#define STM32_RX_PIN  16   // ESP32 nhận từ STM32 PB6
#define STM32_TX_PIN  17   // ESP32 gửi  đến STM32 PB7
HardwareSerial stm32Serial(2);  // UART2 của ESP32

// ──────────────────────────────────────────
//  BIẾN TOÀN CỤC
// ──────────────────────────────────────────
WiFiClient   espClient;
PubSubClient mqtt(espClient);

bool     isLocked        = true;
unsigned long lastHeartbeat = 0;
const long HEARTBEAT_INTERVAL = 15000;

// ── Face ID Layer 2 ──
bool faceVerifyActive = false;  // STM32 đang chờ kết quả face
int  faceFailCount    = 0;      // Số lần nhận diện thất bại liên tiếp

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
    if (faceVerifyActive) {
      faceVerifyActive = false;
      faceFailCount    = 0;
      Serial.println("[FACE] Layer2 OK -> Forwarding Unlock to STM32");
    }
    doUnlock(user, time_str);
  }
  else if (strcmp(action, "lock") == 0) {
    doLock();
  }
  else if (strcmp(action, "temp_lock") == 0) {
    // Thất bại 3 lần -> báo STM32 khoá tạm
    stm32Serial.write('X');
    faceVerifyActive = false;
    faceFailCount    = 0;
    publishStatus("temp_locked", "face_fail_3x");
    Serial.println("[FACE] 3x fail -> STM32 'X' temp_lock");
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
  Serial.println("[LOCK] >>> COMMAND: UNLOCK <<<");

  // ── Gửi lệnh mở khoá sang STM32 qua UART ──
  stm32Serial.write('O');
  Serial.println("[STM32] Sent: O (Open)");

  publishStatus("unlocked", user);
}

// ──────────────────────────────────────────
//  KHÓA LẠI
// ──────────────────────────────────────────
void doLock() {
  Serial.println("[LOCK] >>> COMMAND: LOCK <<<");
  
  // STM32 quản lý toàn bộ quá trình đóng, ESP32 chỉ làm nhiệm vụ đồng bộ trạng thái.
  publishStatus("locked", "auto");
}

// ──────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== FaceGuard ESP32 MQTT ===");

  // ── Khởi động UART2 giao tiếp STM32 ──
  // STM32 USART1 chạy 115200 baud - phải khớp!
  stm32Serial.begin(115200, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
  Serial.println("[STM32] UART2 initialized (115200 baud)");

  // GPIO: Đã bỏ Relay và đèn trên ESP, nhường toàn quyền cho STM32
  // Kết nối WiFi + MQTT
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
  // Đảm bảo Wifi luôn kết nối
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Rớt mạng! Đang kết nối lại...");
    WiFi.disconnect();
    WiFi.reconnect();
    unsigned long startM = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startM < 10000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[WiFi] Đã kết nối lại!");
    } else {
      Serial.println("\n[WiFi] Kết nối thất bại, Khởi động lại ESP...");
      ESP.restart();
    }
  }

  // Đảm bảo MQTT luôn kết nối
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();

  // ── Đọc phản hồi từ STM32 ──
  while (stm32Serial.available()) {
    char c = (char)stm32Serial.read();
    Serial.print("[STM32] Received: '");
    Serial.print(c);
    Serial.println("'");

    if (c == 'F') {
      // STM32 mat khau dung -> yeu cau Face ID
      faceVerifyActive = true;
      faceFailCount = 0;
      publishStatus("request_face", "stm32");
      Serial.println("[STM32] Layer 1 OK -> Requesting Face ID...");
    }
    else if (c == 'U') {
      // STM32 báo đã mở cửa
      isLocked   = false;
      publishStatus("unlocked", "stm32");
      Serial.println("[STM32] Door opened by STM32");
    }
    else if (c == 'L') {
      // STM32 báo đã khoá lại
      isLocked = true;
      publishStatus("locked", "stm32");
      Serial.println("[STM32] Door locked by STM32");
    }
    else if (c == 'A') {
      // STM32 alive (ping response)
      Serial.println("[STM32] Alive / Online");
    }
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
