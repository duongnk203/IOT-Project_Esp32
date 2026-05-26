#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ================== ĐỊNH NGHĨA CHÂN XE ==================
#define TRIG_L 16
#define ECHO_L 17
#define TRIG_C 18
#define ECHO_C 19
#define TRIG_R 23
#define ECHO_R 22

#define ENA_L 32  
#define IN1_L 33  
#define IN2_L 25  
#define ENB_R 14  
#define IN3_R 27  
#define IN4_R 26  

// ================== ĐỊNH NGHĨA CHÂN CẢM BIẾN & RELAY ==================
#define SMOKE_PIN 35    
#define DUST_RX 34      
#define DHTPIN 21       
#define DHTTYPE DHT11   

#define RELAY_1 13       
#define RELAY_2 4      

// ================== THÔNG SỐ CÀI ĐẶT ==================
#define MAX_SPEED 255       
#define DANGER_DIST_C 25    
#define MAX_READ_DIST 80    

volatile int baseSpeed = 100;
volatile float Kp = 1.5;  
volatile float Kd = 1.0;  
volatile float Ki = 0.1;
float previousError = 0, I = 0;

volatile float currentTemp = 0.0;
volatile float currentHum = 0.0;
volatile int currentPM25 = 0;
volatile bool hasSmoke = false;

// BIẾN NGƯỠNG ĐIỀU CHỈNH ĐƯỢC TỪ WEB
volatile int thresholdPM25 = 400;
volatile float thresholdHum = 40.0;
volatile bool relay1State = false; 
volatile bool relay2State = false;

volatile bool envAlarm = false; 
volatile bool dryAlarm = false;

// Chế độ điều khiển
enum CarState { STOPPED, RUNNING };
enum ControlMode { AUTO_MODE, MANUAL_MODE };
enum ManualMoveCommand { MOVE_STOP, MOVE_FORWARD, MOVE_BACKWARD, MOVE_LEFT, MOVE_RIGHT };

volatile CarState currentState = STOPPED;
volatile ControlMode controlMode = AUTO_MODE;
volatile ManualMoveCommand manualMoveCommand = MOVE_STOP;
volatile int manualSpeed = 120;
volatile unsigned long manualUntilMs = 0;

// Override relay thủ công
volatile bool manualMistOverrideEnabled = false;
volatile bool manualFilterOverrideEnabled = false;
volatile bool manualMistState = false;
volatile bool manualFilterState = false;

WebServer server(80);
DHT dht(DHTPIN, DHTTYPE);

// ================== WIFI ==========================

const char* ssid = "Tang 2";
const char* password = "123456789";

// ================== Bộ nhớ ========================

Preferences preferences;

// ================== GIAO DIỆN WEB ==================
const char* html_page = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
  <title>Control Panel & Sensors</title>
  <style>
    body { font-family: Arial; text-align: center; margin-top: 20px; background-color: #f4f4f9; }
    h2 { color: #333; }
    .container { display: flex; flex-wrap: wrap; justify-content: center; gap: 20px; }
    .card { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); width: 340px; text-align: left;}
    .card-center { text-align: center; }
    input { width: 70px; font-size: 16px; text-align: center; margin: 5px; padding: 5px; }
    button { font-size: 16px; margin: 10px auto; padding: 10px; border: none; border-radius: 5px; cursor: pointer; font-weight: bold; width: 90%; display: block;}
    .btn-save { background: #ffc107; color: black; }
    .btn-run { background: #28a745; color: white; }
    .btn-stop { background: #dc3545; color: white; }
    .label { display: inline-block; width: 130px; font-weight: bold;}
    .sensor-data { font-size: 18px; color: #0056b3; font-weight: bold;}
    .alert-text { color: red; font-weight: bold; }
    .relay-on { color: #28a745; font-weight: bold; }
    .relay-off { color: gray; }
    #alarmBanner { display: none; background: #dc3545; color: white; padding: 10px; font-weight: bold; margin-bottom: 20px; border-radius: 5px; animation: blinker 1s linear infinite; }
    @keyframes blinker { 50% { opacity: 0; } }
  </style>
  <script>
    function sendReq(url) { fetch(url).then(r => r.text()).then(t => console.log(t)); }
    function setParams() { 
      let p = document.getElementById('kp').value;
      let i = document.getElementById('ki').value;
      let d = document.getElementById('kd').value;
      let s = document.getElementById('speed').value;
      let t_pm = document.getElementById('th_pm').value;
      let t_hum = document.getElementById('th_hum').value;
      sendReq('/setParams?kp='+p+'&ki='+i+'&kd='+d+'&speed='+s+'&th_pm='+t_pm+'&th_hum='+t_hum);
      alert("Đã lưu thông số xe và môi trường!");
    }
    
    window.onload = () => {
      fetch('/getParams').then(r => r.json()).then(d => {
        document.getElementById('kp').value = d.kp;
        document.getElementById('ki').value = d.ki;
        document.getElementById('kd').value = d.kd;
        document.getElementById('speed').value = d.speed;
        document.getElementById('th_pm').value = d.th_pm;
        document.getElementById('th_hum').value = d.th_hum;
      });
      setInterval(fetchSensors, 2000); 
    }

    function fetchSensors() {
      fetch('/getSensors').then(r => r.json()).then(d => {
        document.getElementById('temp').innerText = d.temp + " °C";
        document.getElementById('hum').innerText = d.hum + " %";
        document.getElementById('pm25').innerText = d.pm25 + " µg/m³";
        
        let smokeEl = document.getElementById('smoke');
        smokeEl.innerText = d.smoke ? "CÓ KHÓI!" : "Bình thường";
        smokeEl.className = d.smoke ? "sensor-data alert-text" : "sensor-data";

        let r1 = document.getElementById('relay1');
        r1.innerText = d.r1 ? "ĐANG BẬT" : "Tắt";
        r1.className = d.r1 ? "relay-on" : "relay-off";

        let r2 = document.getElementById('relay2');
        r2.innerText = d.r2 ? "ĐANG BẬT" : "Tắt";
        r2.className = d.r2 ? "relay-on" : "relay-off";

        let banner = document.getElementById('alarmBanner');
        banner.style.display = d.alarm ? "block" : "none";
      });
    }
    function scanWifi() {
      fetch('/scanWifi')
        .then(r => r.json())
        .then(data => {
          let list = document.getElementById('wifiList');
          list.innerHTML = "";
          data.forEach(w => {
            let opt = document.createElement("option");
            opt.value = w.ssid;
            opt.text = w.ssid + " (" + w.rssi + ")";
            list.appendChild(opt);
          });
        });
    }

    function connectWifi() {
      let ssid = document.getElementById('wifiList').value;
      let pass = document.getElementById('wifiPass').value;

      fetch('/connectWifi?ssid=' + ssid + '&pass=' + pass)
        .then(r => r.text())
        .then(ip => {
          if (ip === "FAIL") {
            alert("Kết nối thất bại!");
          } else {
            document.getElementById('ip').innerText = ip;
            alert("Kết nối thành công!");
          }
        });
    }
  </script>
</head>
<body>
  <h2>BẢNG ĐIỀU KHIỂN TRUNG TÂM</h2>
  <div id="alarmBanner">⚠️ MÔI TRƯỜNG BẤT THƯỜNG (KHÓI/BỤI/KHÔNG KHÍ KHÔ) - XE TẠM DỪNG ĐỂ XỬ LÝ! ⚠️</div>
  <div class="container">
    <div class="card card-center">
      <h2>⚙️ THÔNG SỐ XE & CẢM BIẾN</h2>
      <span class="label">Tốc độ xe:</span> <input id="speed" type="number" step="1" min="0" max="255"><br>
      <span class="label">Ngưỡng PM2.5:</span> <input id="th_pm" type="number" step="1"><br>
      <span class="label">Ngưỡng Ẩm (%):</span> <input id="th_hum" type="number" step="0.1"><br>
      <span class="label">Kp:</span> <input id="kp" type="number" step="0.1"><br>
      <span class="label">Ki:</span> <input id="ki" type="number" step="0.01"><br>
      <span class="label">Kd:</span> <input id="kd" type="number" step="0.1"><br><br>
      
      <button class="btn-save" onclick="setParams()">LƯU THÔNG SỐ</button>
      <button class="btn-run" onclick="sendReq('/run')">CHẠY LIÊN TỤC</button>
      <button class="btn-stop" onclick="sendReq('/stop')">DỪNG KHẨN CẤP</button>
    </div>
    <div class="card">
      <h2 style="text-align: center;">🌱 MÔI TRƯỜNG & THIẾT BỊ</h2>
      <p><span class="label">Nhiệt độ:</span> <span id="temp" class="sensor-data">--</span></p>
      <p><span class="label">Độ ẩm:</span> <span id="hum" class="sensor-data">--</span></p>
      <p><span class="label">Bụi PM2.5:</span> <span id="pm25" class="sensor-data">--</span></p>
      <p><span class="label">Trạng thái:</span> <span id="smoke" class="sensor-data">--</span></p>
      <hr>
      <p><span class="label">Relay 1 (Khí):</span> <span id="relay1" class="relay-off">--</span></p>
      <p><span class="label">Relay 2 (Ẩm):</span> <span id="relay2" class="relay-off">--</span></p>
    </div>
    <div class="card">
      <h2 style="text-align:center;">📡 KẾT NỐI WIFI</h2>

      <button onclick="scanWifi()">🔍 Quét WiFi</button>

      <select id="wifiList" style="width:100%; padding:5px;"></select>

      <input id="wifiPass" type="password" placeholder="Nhập mật khẩu" style="width:100%; margin-top:10px;">

      <button onclick="connectWifi()">Kết nối</button>

      <p>IP ESP32: <span id="ip">--</span></p>
    </div>
  </div>
</body>
</html>
)=====";

// Khai báo prototype
float readUltrasonic(int trigPin, int echoPin);
float getReliableDistance(int trigPin, int echoPin);
void driveMotors(int speedLeft, int speedRight);

/*
  TASK WEB SERVER
  - Chạy web server local trên ESP32 (port 80)
  - Trả giao diện HTML để người dùng theo dõi sensor / điều khiển cơ bản
  - Cung cấp endpoint đọc/ghi tham số, quét wifi, kết nối wifi
  - Task này chạy liên tục, gọi server.handleClient() theo chu kỳ ngắn
*/
 // ================== TASK 1: WIFI & WEB SERVER ==================
void TaskWeb(void *pvParameters) {
  server.on("/", []() { server.send(200, "text/html", html_page); });
  
  server.on("/getParams", []() {
    String json = "{\"kp\":" + String(Kp) + ",\"ki\":" + String(Ki) + ",\"kd\":" + String(Kd) + 
                  ",\"speed\":" + String(baseSpeed) + ",\"th_pm\":" + String(thresholdPM25) + 
                  ",\"th_hum\":" + String(thresholdHum) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/getSensors", []() {
    bool anyAlarm = envAlarm || dryAlarm;
    String json = "{\"temp\":" + String(currentTemp) + ",\"hum\":" + String(currentHum) + 
                  ",\"pm25\":" + String(currentPM25) + ",\"smoke\":" + (hasSmoke ? "true" : "false") + 
                  ",\"r1\":" + (relay1State ? "true" : "false") + ",\"r2\":" + (relay2State ? "true" : "false") + 
                  ",\"alarm\":" + (anyAlarm ? "true" : "false") + "}";
    server.send(200, "application/json", json);
  });

  server.on("/setParams", []() {
    if (server.hasArg("kp")) Kp = server.arg("kp").toFloat();
    if (server.hasArg("ki")) Ki = server.arg("ki").toFloat();
    if (server.hasArg("kd")) Kd = server.arg("kd").toFloat();
    if (server.hasArg("speed")) baseSpeed = server.arg("speed").toInt();
    if (server.hasArg("th_pm")) thresholdPM25 = server.arg("th_pm").toInt();
    if (server.hasArg("th_hum")) thresholdHum = server.arg("th_hum").toFloat(); 
    previousError = 0; I = 0; 
    server.send(200, "text/plain", "OK");
  });

  
  server.on("/run", []() { currentState = RUNNING; server.send(200, "text/plain", "OK"); });
  server.on("/stop", []() { currentState = STOPPED; server.send(200, "text/plain", "OK"); });
  server.on("/scanWifi", []() { 
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });
  server.on("/connectWifi", []() {
    if (server.hasArg("ssid") && server.hasArg("pass")) {
      String ssid = server.arg("ssid");
      String pass = server.arg("pass");

      WiFi.begin(ssid.c_str(), pass.c_str());

      int timeout = 0;
      while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        timeout++;
      }

      if (WiFi.status() == WL_CONNECTED) {

        preferences.begin("wifi", false);
        preferences.putString("ssid", ssid);
        preferences.putString("pass", pass);
        preferences.end();

        server.send(200, "text/plain", WiFi.localIP().toString());
      } else {
        server.send(200, "text/plain", "FAIL");
      }
    }
  });
  
  server.begin();

  while (1) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10)); 
  }
}

/*
  TASK PM2.5 (UART)
  - Đọc dữ liệu bụi từ cảm biến qua Serial2
  - Đồng bộ theo header khung dữ liệu 0x42 0x4D
  - Cập nhật currentPM25 dùng chung cho toàn hệ thống
*/
 // ================== TASK 2: ĐỌC PM2.5 (UART) ==================
void TaskPM25(void *pvParameters) {
  Serial2.begin(9600, SERIAL_8N1, DUST_RX, -1);
  uint8_t buffer[32]; // Tăng buffer để an toàn đọc header

  while (1) {
    // Xóa rác, đồng bộ header 0x42
    while (Serial2.available() > 0 && Serial2.peek() != 0x42) {
      Serial2.read();
    }

    if (Serial2.available() >= 24) {
      if (Serial2.read() == 0x42) {          
        if (Serial2.read() == 0x4D) {        
          Serial2.readBytes(&buffer[2], 22);
          currentPM25 = (buffer[6] << 8) | buffer[7];
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/*
  TASK ENV
  - Đọc DHT11 (nhiệt độ/độ ẩm) + cảm biến khói
  - Tính toán điều kiện cảnh báo envAlarm / dryAlarm
  - Điều khiển relay tự động hoặc theo chế độ manual override
  - Gửi dữ liệu sensor lên API theo chu kỳ
*/
 // ================== TASK 3: ĐỌC MÔI TRƯỜNG & LOGIC RELAY ==================
void TaskEnv(void *pvParameters) {
  dht.begin();
  pinMode(SMOKE_PIN, INPUT);
  
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  
  // Khởi tạo trạng thái tắt ban đầu
  digitalWrite(RELAY_1, HIGH); 
  digitalWrite(RELAY_2, LOW);
  
  int dhtErrorCount = 0; 

  while (1) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (!isnan(h) && !isnan(t)) {
      currentHum = h;
      currentTemp = t;
      dhtErrorCount = 0; 
    } else {
      dhtErrorCount++;
      if (dhtErrorCount > 5) {
        dht.begin();
      }
    }
    
    hasSmoke = (digitalRead(SMOKE_PIN) == LOW);

    bool badAirCondition = (currentPM25 > thresholdPM25 || hasSmoke);
    bool dryAirCondition = (currentHum > 0.0 && currentHum < thresholdHum);

    // --- LOGIC XỬ LÝ KHÍ ĐỘC / KHÓI ---
    envAlarm = badAirCondition;
    if (manualMistOverrideEnabled) {
      digitalWrite(RELAY_1, manualMistState ? LOW : HIGH);
      relay1State = manualMistState;
    } else if (badAirCondition) {
      vTaskDelay(pdMS_TO_TICKS(100)); 
      digitalWrite(RELAY_1, LOW); 
      relay1State = true;
    } else {
      digitalWrite(RELAY_1, HIGH);
      relay1State = false;
    }

    // --- LOGIC XỬ LÝ ĐỘ ẨM THẤP ---
    dryAlarm = dryAirCondition;
    if (manualFilterOverrideEnabled) {
      digitalWrite(RELAY_2, manualFilterState ? HIGH : LOW);
      relay2State = manualFilterState;
    } else if (dryAirCondition) {
      vTaskDelay(pdMS_TO_TICKS(100)); 
      digitalWrite(RELAY_2, HIGH); 
      relay2State = true;
    } else {
      digitalWrite(RELAY_2, LOW);
      relay2State = false;
    }
    sendDataToAPI();
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

/*
  TASK CONTROL (TRÁI TIM CỦA XE)
  Thứ tự ưu tiên logic:
  1) Nếu có alarm môi trường -> dừng xe ngay
  2) Nếu ở MANUAL_MODE -> chạy theo manualMoveCommand, KHÔNG chạy auto PID
  3) Nếu ở AUTO_MODE:
     - currentState == STOPPED -> dừng
     - currentState == RUNNING -> đọc siêu âm, né vật cản + PID giữ hướng
*/
 // ================== TASK 4: ĐIỀU KHIỂN XE & PID ==================
void TaskControl(void *pvParameters) {
  pinMode(TRIG_L, OUTPUT); pinMode(ECHO_L, INPUT);
  pinMode(TRIG_C, OUTPUT); pinMode(ECHO_C, INPUT);
  pinMode(TRIG_R, OUTPUT); pinMode(ECHO_R, INPUT);
  pinMode(ENA_L, OUTPUT); pinMode(IN1_L, OUTPUT); pinMode(IN2_L, OUTPUT);
  pinMode(ENB_R, OUTPUT); pinMode(IN3_R, OUTPUT); pinMode(IN4_R, OUTPUT);

  driveMotors(0, 0);
  bool wasAlarm = false; 

  while (1) {
    if (envAlarm == true || dryAlarm == true) {
      driveMotors(0, 0);
      wasAlarm = true; 
      vTaskDelay(pdMS_TO_TICKS(50));
      continue; 
    }

    if (wasAlarm) {
      previousError = 0; 
      I = 0;
      wasAlarm = false;
    }

    if (controlMode == MANUAL_MODE) {
      // Nếu lệnh manual có thời hạn và đã hết hạn -> tự dừng
      if (manualUntilMs > 0 && millis() > manualUntilMs) {
        manualMoveCommand = MOVE_STOP;
        manualUntilMs = 0;
      }

      // Giới hạn tốc độ manual trong dải an toàn PWM
      int moveSpeed = constrain(manualSpeed, 0, MAX_SPEED);

      // Ánh xạ lệnh manual thành chuyển động bánh trái/phải
      switch (manualMoveCommand) {
        case MOVE_FORWARD:
          driveMotors(moveSpeed, moveSpeed);
          break;
        case MOVE_BACKWARD:
          driveMotors(-moveSpeed, -moveSpeed);
          break;
        case MOVE_LEFT:
          driveMotors(-moveSpeed, moveSpeed);
          break;
        case MOVE_RIGHT:
          driveMotors(moveSpeed, -moveSpeed);
          break;
        case MOVE_STOP:
        default:
          driveMotors(0, 0);
          break;
      }

      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }

    if (currentState == STOPPED) {
      driveMotors(0, 0);
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    float distL = getReliableDistance(TRIG_L, ECHO_L);
    float distC = getReliableDistance(TRIG_C, ECHO_C);
    float distR = getReliableDistance(TRIG_R, ECHO_R);

    if (distC < DANGER_DIST_C) {
      // Vật cản trước mặt quá gần:
      // 1) lùi ngắn để thoát điểm nguy hiểm
      driveMotors(-120, -120);
      vTaskDelay(pdMS_TO_TICKS(300));
      // 2) xoay về bên thoáng hơn (so sánh trái/phải)
      if (distL > distR) driveMotors(-180, 180);
      else driveMotors(180, -180);
      vTaskDelay(pdMS_TO_TICKS(400));
      previousError = 0; I = 0;
      continue;
    }

    // PID bám hướng: cân bằng khoảng cách trái/phải
    float error = distL - distR;
    float P = error;
    I = constrain(I + error, -100, 100); 
    float D = error - previousError;
    float PID_Value = (Kp * P) + (Ki * I) + (Kd * D);
    previousError = error;

    // Khi gần vật cản phía trước, giảm tốc nền để xe phản ứng mượt hơn
    int currentBaseSpeed = baseSpeed;
    if (distC < 60) currentBaseSpeed = map(distC, DANGER_DIST_C, 60, 100, baseSpeed);
    
    int speedL = constrain(currentBaseSpeed - PID_Value, -MAX_SPEED, MAX_SPEED);
    int speedR = constrain(currentBaseSpeed + PID_Value, -MAX_SPEED, MAX_SPEED);

    driveMotors(speedL, speedR);
    
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

void setup() {
  Serial.begin(115200);
  setupWiFi();

  xTaskCreatePinnedToCore(TaskControl, "TaskControl", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskWeb, "TaskWeb", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskPM25, "TaskPM25", 2048, NULL, 1, NULL, 0); 
  xTaskCreatePinnedToCore(TaskEnv, "TaskEnv", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskAPI, "TaskAPI", 4096, NULL, 1, NULL, 0);   
}

void loop() { 
  vTaskDelete(NULL);
}

float readUltrasonic(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 25000); 
  if (duration == 0) return 0; 
  return duration * 0.0343 / 2.0;
}

float getReliableDistance(int trigPin, int echoPin) {
  float d1 = readUltrasonic(trigPin, echoPin);
  if (d1 == 0 || d1 > MAX_READ_DIST) d1 = MAX_READ_DIST;
  if (d1 < DANGER_DIST_C + 10) {
      vTaskDelay(pdMS_TO_TICKS(5));
      float d2 = readUltrasonic(trigPin, echoPin);
      if (d2 == 0 || d2 > MAX_READ_DIST) d2 = MAX_READ_DIST;
      return max(d1, d2);
  }
  return d1;
}

/*
  driveMotors(speedLeft, speedRight)
  - speed dương: quay thuận, speed âm: quay ngược
  - Hàm tự đảo chiều chân IN1/IN2/IN3/IN4 theo dấu tốc độ
  - PWM được ghi qua ENA_L/ENB_R
*/
void driveMotors(int speedLeft, int speedRight) {
  if (speedLeft >= 0) {
    digitalWrite(IN1_L, HIGH);
    digitalWrite(IN2_L, LOW);
  } else {
    digitalWrite(IN1_L, LOW);  
    digitalWrite(IN2_L, HIGH);
    speedLeft = -speedLeft;
  }
  
  if (speedRight >= 0) {
    digitalWrite(IN3_R, LOW);
    digitalWrite(IN4_R, HIGH);
  } else {
    digitalWrite(IN3_R, HIGH);
    digitalWrite(IN4_R, LOW);
    speedRight = -speedRight;  
  }

  analogWrite(ENA_L, speedLeft);
  analogWrite(ENB_R, speedRight);
}

void sendDataToAPI() {
  static unsigned long lastSend = 0;
  unsigned long now = millis();

  if (now - lastSend < 60000) return; // 60s
  lastSend = now;

  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, "https://iot-project-web-sensor.onrender.com/api/home/sensor-data");

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  String json = "{";
  json += "\"deviceId\":\"car_001\",";
  json += "\"temperature\":" + String(currentTemp) + ",";
  json += "\"humidity\":" + String(currentHum) + ",";
  json += "\"pm1\":0,";
  json += "\"pm25\":" + String(currentPM25) + ",";
  json += "\"pm10\":0,";
  json += "\"smoke\":" + String(hasSmoke ? "true" : "false") + ",";
  json += "\"relay1\":" + String(relay1State ? "true" : "false") + ",";
  json += "\"relay2\":" + String(relay2State ? "true" : "false") + ",";
  json += "\"envAlarm\":" + String(envAlarm ? "true" : "false") + ",";
  json += "\"dryAlarm\":" + String(dryAlarm ? "true" : "false") + ",";
  json += "\"carState\":\"" + String(currentState == RUNNING ? "RUNNING" : "STOPPED") + "\",";
  json += "\"thresholdPm25\":" + String(thresholdPM25) + ",";
  json += "\"thresholdHum\":" + String(thresholdHum) + ",";
  json += "\"speed\":" + String(baseSpeed) + ",";
  json += "\"kp\":" + String(Kp) + ",";
  json += "\"ki\":" + String(Ki) + ",";
  json += "\"kd\":" + String(Kd);
  json += "}";

  Serial.println("SEND API:");
  Serial.println(json);

  int code = http.POST(json);
  Serial.println("HTTP CODE: " + String(code));

  http.end();
}

void setupWiFi() {
  WiFi.mode(WIFI_STA); // chỉ dùng STA, bỏ AP cho gọn

  const char* ssid = "lenovo";
  const char* password = "123456789";

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 30) {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ Connect failed");
  }
}

/*
  fetchConfigFromAPI
  - Lấy cấu hình mới nhất từ backend: threshold, speed, Kp/Ki/Kd
  - Chỉ cập nhật field nếu key tồn tại để tránh ghi đè giá trị sai
  - QUAN TRỌNG: chỉ cập nhật currentState từ API khi đang AUTO_MODE
    => tránh trường hợp đang MANUAL mà bị backend ghi đè trạng thái chạy/dừng
*/
// void fetchConfigFromAPI() {
//   if (WiFi.status() != WL_CONNECTED) return;

//   WiFiClientSecure client;
//   client.setInsecure();

//   HTTPClient http;

//   String url = "https://iot-project-web-sensor.onrender.com/api/home/device-config";
//   http.begin(client, url);

//   Serial.println("=== CALL DEVICE CONFIG API ===");

//   int code = http.GET();

//   Serial.println("HTTP CODE: " + String(code));

//   if (code == 200) {
//     String payload = http.getString();
//     Serial.println("PAYLOAD:");
//     Serial.println(payload);
//   } else {
//     Serial.println("CALL API FAILED!");
//   }

//   if (code == 200) {
//     String payload = http.getString();
//     Serial.println(payload);
    
//     DynamicJsonDocument doc(512);
//     deserializeJson(doc, payload);

//     thresholdPM25 = doc["thresholdPm25"];
//     thresholdHum = doc["thresholdHum"];
//     baseSpeed = doc["speed"];
//     Kp = doc["kp"];
//     Ki = doc["ki"];
//     Kd = doc["kd"];

//     String state = doc["carState"];
//     if (state == "RUNNING") currentState = RUNNING;
//     else currentState = STOPPED;
//   }

//   http.end();
// }

/*
  fetchConfigFromAPI (phiên bản đang dùng)
  - Đây là hàm thực thi thật (được TaskAPI gọi định kỳ)
*/
void fetchConfigFromAPI() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = "https://iot-project-web-sensor.onrender.com/api/home/device-config";
  http.begin(client, url);

  int code = http.GET();
  if (code == 200) {
    // CHỈ GỌI getString() MỘT LẦN DUY NHẤT
    String payload = http.getString(); 
    Serial.println("--- Dữ liệu nhận được ---");
    Serial.println(payload);

    // Giải mã JSON
    DynamicJsonDocument doc(1024); // Tăng lên 1024 để an toàn nếu JSON dài
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      // Chỉ cập nhật nếu các key tồn tại trong JSON để tránh bị ghi đè 0
      if (doc.containsKey("thresholdPm25")) thresholdPM25 = doc["thresholdPm25"];
      if (doc.containsKey("thresholdHum"))  thresholdHum  = doc["thresholdHum"];
      if (doc.containsKey("speed"))         baseSpeed     = doc["speed"];
      if (doc.containsKey("kp"))            Kp            = doc["kp"];
      if (doc.containsKey("ki"))            Ki            = doc["ki"];
      if (doc.containsKey("kd"))            Kd            = doc["kd"];

      // CHỈ cập nhật currentState (Auto Mode) nếu đang ở AUTO_MODE để tránh ghi đè Manual
      if (doc.containsKey("carState") && controlMode == AUTO_MODE) {
        String state = doc["carState"];
        currentState = (state == "RUNNING") ? RUNNING : STOPPED;
        Serial.print("Auto State Update: "); Serial.println(state);
      }
      Serial.println("Cập nhật thông số thành công!");
    } else {
      Serial.print("Lỗi giải mã JSON: ");
      Serial.println(error.f_str());
    }
  } else {
    Serial.printf("Lỗi kết nối API: %d\n", code);
  }
  http.end();
}

/*
  fetchCommand
  - Poll lệnh điều khiển mới nhất từ backend
  - Chỉ đổi mode khi payload thật sự có key "mode" (tránh fallback AUTO ngoài ý muốn)
  - Nếu MANUAL_MODE: xử lý FORWARD/BACKWARD/LEFT/RIGHT/STOP + durationMs
  - Nếu AUTO_MODE: chỉ xử lý RUNNING/STOP cho currentState
  - Lệnh relay (MIST/FILTER) xử lý độc lập với mode di chuyển
*/
void fetchCommand() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url = "https://iot-project-web-sensor.onrender.com/api/home/device-command";
  http.begin(client, url);

  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    Serial.println(payload);

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.print("Lỗi parse command JSON: ");
      Serial.println(error.f_str());
      http.end();
      return;
    }

    // CHỈ cập nhật nếu có key "mode" từ server, không dùng default "AUTO" để tránh fallback vô ý
    if (doc.containsKey("mode")) {
      String modeStr = doc["mode"];
      modeStr.toUpperCase();
      if (modeStr == "MANUAL") {
        if (controlMode == AUTO_MODE) {
          Serial.println("Switching to MANUAL_MODE");
          previousError = 0; I = 0; // Reset PID
          currentState = STOPPED;    // Tạm dừng auto logic
        }
        controlMode = MANUAL_MODE;
      } else if (modeStr == "AUTO") {
        if (controlMode == MANUAL_MODE) {
          Serial.println("Switching to AUTO_MODE");
          manualMoveCommand = MOVE_STOP;
        }
        controlMode = AUTO_MODE;
      }
    }

    String cmd = doc["command"] | "";
    cmd.toUpperCase();

    if (controlMode == MANUAL_MODE) {
      int cmdSpeed = doc["speed"] | manualSpeed;
      int durationMs = doc["durationMs"] | 0;
      manualSpeed = constrain(cmdSpeed, 0, MAX_SPEED);

      if (durationMs > 0) manualUntilMs = millis() + (unsigned long)durationMs;
      else if (cmd != "") manualUntilMs = 0; // Nếu có command mới mà không có duration, coi như vô hạn hoặc tới lệnh tiếp theo

      if (cmd == "FORWARD") manualMoveCommand = MOVE_FORWARD;
      else if (cmd == "BACKWARD") manualMoveCommand = MOVE_BACKWARD;
      else if (cmd == "LEFT") manualMoveCommand = MOVE_LEFT;
      else if (cmd == "RIGHT") manualMoveCommand = MOVE_RIGHT;
      else if (cmd == "STOP") manualMoveCommand = MOVE_STOP;
      // Giữ nguyên command cũ nếu cmd rỗng và chưa hết hạn duration
    } else {
      // Trong AUTO_MODE, command chỉ xử lý RUNNING/STOP cho state
      if (cmd == "RUNNING") currentState = RUNNING;
      else if (cmd == "STOP") currentState = STOPPED;
    }

    // Xử lý lệnh Relay độc lập với Mode di chuyển
    if (cmd == "MIST_ON") {
      manualMistOverrideEnabled = true;
      manualMistState = true;
    } else if (cmd == "MIST_OFF") {
      manualMistOverrideEnabled = true;
      manualMistState = false;
    }

    if (cmd == "FILTER_ON") {
      manualFilterOverrideEnabled = true;
      manualFilterState = true;
    } else if (cmd == "FILTER_OFF") {
      manualFilterOverrideEnabled = true;
      manualFilterState = false;
    }
  }

  http.end();
}

void TaskAPI(void *pvParameters) {
  while (1) {
    fetchConfigFromAPI();
    fetchCommand();
    vTaskDelay(pdMS_TO_TICKS(2000)); // gọi mỗi 3s
  }
}
