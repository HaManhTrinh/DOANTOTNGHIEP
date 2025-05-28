#include <WiFi.h>
#include "time.h"
#include <esp_sntp.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <HTTPClient.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// === Cấu hình phần cứng ===
#define TFT_CS   22   
#define TFT_DC   21   
#define TFT_RST  18   
#define TFT_MOSI 23    
#define TFT_SCK  19   
#define TFT_MISO -1   
// === Chân nút nhấn ===
const uint8_t MODE_PIN  = 15;
const uint8_t LIGHT_PIN = 32;
const uint8_t PUMP_PIN  = 33;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// === Cấu hình WiFi và NTP ===
const char* ssid       = "Trieu Ninh";
const char* password   = "12344321";
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov"; 
const char* timeZone   = "ICT-7";

// === URL Google Sheets ===
const char* scriptURL = "https://script.google.com/macros/s/AKfycbxSbGQWOAquv8zb2OPfXuWV3jSzU5gIB9bKX8k1kyIrrpSzMdbw-cyAgNJ0yn6vlRcvSQ/exec";

// === Cấu hình Firebase ===
#define API_KEY "AIzaSyDhBxST74r0kr-CB3XbrQHrWmchANPIIwI"
#define DATABASE_URL "https://datn-22181-default-rtdb.firebaseio.com/"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// === Biến trạng thái ===
bool mode = false;    // false: MANUAL, true: AUTO
bool light = false;   // Trạng thái đèn
bool pump = false;    // Trạng thái bơm
float temperature = 0; // Nhiệt độ (°C)
float humidity = 0;    // Độ ẩm không khí (%)
float soilValue = 0;   // Độ ẩm đất (%)
String lastRcv = "";
String rcv = "";
bool zigbeeSynced = false; // Trạng thái đồng bộ Zigbee
float N = 0, P = 0, K = 0;

// === Biến lưu giá trị cũ để tránh cập nhật TFT thừa ===
bool lastMode = false;
bool lastLight = false, lastPump = false;
float lastTemp = -1000, lastHum = -1000, lastSoil = -1000;
float lastN = -1000, lastP = -1000, lastK = -1000;
String lastTime = "";

// === Biến quản lý đồng bộ Firebase ===
bool firebaseCommandProcessing = false; // Cờ để tránh xung đột

// === Giới hạn tần suất thay đổi trạng thái ===
unsigned long lastLightChange = 0;
unsigned long lastPumpChange = 0;
const unsigned long MIN_CHANGE_INTERVAL = 1000; // 1 giây giữa các thay đổi trạng thái

// === FreeRTOS handles ===
TaskHandle_t TaskTFT_Handle;
TaskHandle_t TaskSerial_Handle;
TaskHandle_t TaskSheet_Handle;
TaskHandle_t ModeButton_Handle;
TaskHandle_t LightButton_Handle;
TaskHandle_t PumpButton_Handle;
TaskHandle_t TaskFirebase_Handle;
QueueHandle_t displayQueue;
SemaphoreHandle_t firebaseMutex;
QueueHandle_t firebaseQueue;
SemaphoreHandle_t dataMutex;

// === Cấu trúc dữ liệu cho hàng đợi ===
struct UpdateData {
  bool modeChanged;
  bool lightChanged;
  bool pumpChanged;
  bool sensorChanged;
  bool timeChanged;
};

// === Vẽ giao diện tĩnh trên TFT ===
void drawStaticLayout() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 10);  tft.println("Truong DH Van Hien");
  tft.setCursor(10, 30);  tft.println("NGHIEN CUU KHOA HOC:");
  tft.setCursor(10, 50);  tft.println("He Thong Dieu khien");
  tft.setCursor(10, 70);  tft.println("va tu dong");
  tft.setCursor(10, 90); tft.println("Cho vuon Thanh Long");
  tft.setCursor(10, 110); tft.println("Mode:");
  tft.setCursor(10, 130); tft.println("Temp:");
  tft.setCursor(10, 150); tft.println("Hum :");
  tft.setCursor(10, 170); tft.println("Soil:");
  tft.setCursor(10, 190); tft.println("Light:");
  tft.setCursor(10, 210); tft.println("Pump :");
  tft.setCursor(10, 230); tft.println("N    :");
  tft.setCursor(10, 250); tft.println("P    :");
  tft.setCursor(10, 270); tft.println("K    :");
}

// === Cập nhật giá trị trên TFT ===
void updateValue(int x, int y, String value, uint16_t color = ILI9341_WHITE) {
  tft.fillRect(x, y, 140, 20, ILI9341_BLACK);
  tft.setCursor(x, y);
  tft.setTextColor(color);
  tft.setTextSize(2);
  tft.print(value);
}

// === Cập nhật trạng thái trên TFT ===
void updateTFT(UpdateData update) {
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (update.modeChanged && mode != lastMode) {
      String modeStr = mode ? "AUTO" : "MANUAL";
      updateValue(80, 110, modeStr, mode ? ILI9341_YELLOW : ILI9341_CYAN);
      lastMode = mode;
    }
    
    if (update.lightChanged && light != lastLight) {
      String lightStr = light ? "ON" : "OFF";
      updateValue(80, 190, lightStr, light ? ILI9341_GREEN : ILI9341_RED);
      lastLight = light;
    }
    
    if (update.pumpChanged && pump != lastPump) {
      String pumpStr = pump ? "ON" : "OFF";
      updateValue(80, 210, pumpStr, pump ? ILI9341_GREEN : ILI9341_RED);
      lastPump = pump;
    }
    
    if (update.sensorChanged) {
      if (abs(temperature - lastTemp) > 0.1) {
        updateValue(80, 130, String(temperature, 1) + " C");
        lastTemp = temperature;
      }
      if (abs(humidity - lastHum) > 0.1) {
        updateValue(80, 150, String(humidity, 1) + " %");
        lastHum = humidity;
      }
      if (abs(soilValue - lastSoil) > 0.1) {
        updateValue(80, 170, String(soilValue, 1) + " %");
        lastSoil = soilValue;
      }
      if (abs(N - lastN) > 0.1) {
        updateValue(80, 230, String(N, 1) + " mg/kg");
        lastN = N;
      }
      if (abs(P - lastP) > 0.1) {
        updateValue(80, 250, String(P, 1) + " mg/kg");
        lastP = P;
      }
      if (abs(K - lastK) > 0.1) {
        updateValue(80, 270, String(K, 1) + " mg/kg");
        lastK = K;
      }
    }
    
    if (update.timeChanged) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &timeinfo);
        String timeStr = String(buf);
        if (timeStr != lastTime) {
          tft.fillRect(10, 300, 220, 20, ILI9341_BLACK);
          tft.setCursor(10, 300);
          tft.setTextColor(ILI9341_GREEN);
          tft.setTextSize(2);
          tft.println(timeStr);
          lastTime = timeStr;
        }
      }
    }
    
    xSemaphoreGive(dataMutex);
  }
}

// === Task: Cập nhật màn hình TFT ===
void TaskTFT(void *pvParameters) {
  UpdateData update;
  unsigned long lastTimeUpdate = 0;
  
  for (;;) {
    bool hasUpdate = false;
    
    // Kiểm tra cập nhật từ queue
    if (xQueueReceive(displayQueue, &update, pdMS_TO_TICKS(10)) == pdTRUE) {
      updateTFT(update);
      hasUpdate = true;
    }
    
    // Cập nhật thời gian mỗi giây
    if (millis() - lastTimeUpdate >= 1000) {
      UpdateData timeUpdate = {false, false, false, false, true};
      updateTFT(timeUpdate);
      lastTimeUpdate = millis();
      hasUpdate = true;
    }
    
    // Nếu không có cập nhật nào, delay để tránh hogging CPU
    if (!hasUpdate) {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

// === Gửi dữ liệu cảm biến lên Google Sheets ===
void guiLenGoogleSheet(float t, float h, float s, float n, float p, float k) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(scriptURL)
      + "?temp=" + String(t)
      + "&hum="  + String(h)
      + "&soil=" + String(s)
      + "&nitro=" + String(n)
      + "&phos=" + String(p)
      + "&pota=" + String(k);
    http.begin(url);
    http.setTimeout(10000);
    int code = http.GET();
    if (code > 0) {
      Serial.println("Gửi dữ liệu lên Google Sheets thành công");
    } else {
      Serial.println("Lỗi gửi dữ liệu: " + http.errorToString(code));
    }
    http.end();
  } else {
    Serial.println("Mất WiFi");
  }
}

// === Task: Nhận dữ liệu từ Serial2 ===
void TaskSerial(void *pvParameters) {
  for (;;) {
    nhanDuLieu();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// === Nhận và xử lý dữ liệu từ Zigbee qua Serial2 ===
void nhanDuLieu() {
  static String buf = "";
  static bool receivedLight = false, receivedPump = false;
  
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      rcv = buf;
      buf = "";
      rcv.trim();
      
      if (rcv != lastRcv && rcv.length() > 0) {
        lastRcv = rcv;
        UpdateData update = {false, false, false, false, false};
        bool needUpdate = false;
        
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          // Xử lý lệnh đèn
          if (rcv == "LIGHT ON" && mode) {
            if (!light) {
              light = true;
              receivedLight = true;
              Serial.println("Đèn (AUTO) BẬT");
              update.lightChanged = true;
              needUpdate = true;
            }
          } else if (rcv == "LIGHT OFF" && mode) {
            if (light) {
              light = false;
              receivedLight = true;
              Serial.println("Đèn (AUTO) TẮT");
              update.lightChanged = true;
              needUpdate = true;
            }
          }
          
          // Xử lý lệnh bơm
          if (rcv == "PUMP ON" && mode) {
            if (!pump) {
              pump = true;
              receivedPump = true;
              Serial.println("Bơm (AUTO) BẬT");
              update.pumpChanged = true;
              needUpdate = true;
            }
          } else if (rcv == "PUMP OFF" && mode) {
            if (pump) {
              pump = false;
              receivedPump = true;
              Serial.println("Bơm (AUTO) TẮT");
              update.pumpChanged = true;
              needUpdate = true;
            }
          }
          
          // Xử lý dữ liệu cảm biến nhiệt độ và độ ẩm
          if (rcv.startsWith("TEMP=") && rcv.indexOf(" HUM=") != -1) {
            int tempStart = 5;
            int humStart = rcv.indexOf(" HUM=") + 5;
            float newTemp = rcv.substring(tempStart, rcv.indexOf(" HUM=")).toFloat();
            float newHum = rcv.substring(humStart).toFloat();
            
            if (abs(newTemp - temperature) > 0.1 || abs(newHum - humidity) > 0.1) {
              temperature = newTemp;
              humidity = newHum;
              update.sensorChanged = true;
              needUpdate = true;
            }
          } 
          // Xử lý dữ liệu độ ẩm đất
          else if (rcv.startsWith("SOIL=")) {
            float newSoil = rcv.substring(5).toFloat();
            if (abs(newSoil - soilValue) > 0.1) {
              soilValue = newSoil;
              update.sensorChanged = true;
              needUpdate = true;
            }
          }
          // Xử lý dữ liệu NPK
          else if (rcv.startsWith("N=") && rcv.indexOf(" P=") != -1 && rcv.indexOf(" K=") != -1) {
            int nStart = 2;
            int pStart = rcv.indexOf(" P=") + 3;
            int kStart = rcv.indexOf(" K=") + 3;
            float newN = rcv.substring(nStart, rcv.indexOf(" P=")).toFloat();
            float newP = rcv.substring(pStart, rcv.indexOf(" K=")).toFloat();
            float newK = rcv.substring(kStart).toFloat();
            
            if (abs(newN - N) > 0.1 || abs(newP - P) > 0.1 || abs(newK - K) > 0.1) {
              N = newN;
              P = newP;
              K = newK;
              update.sensorChanged = true;
              needUpdate = true;
            }
          }
          
          if (receivedLight && receivedPump) {
            zigbeeSynced = true;
          }
          
          xSemaphoreGive(dataMutex);
        }
        
        // Gửi cập nhật nếu có thay đổi
        if (needUpdate) {
          xQueueSend(displayQueue, &update, 0);
          if (signupOK) {
            xQueueSend(firebaseQueue, &update, 0);
          }
        }
      }
    } else {
      buf += c;
    }
  }
}

// === Task: Gửi dữ liệu lên Google Sheets ===
void TaskSheet(void *pvParameters) {
  unsigned long lastSheetUpdate = 0;
  const unsigned long SHEET_UPDATE_INTERVAL = 30000; // 30 giây
  
  for (;;) {
    if (millis() - lastSheetUpdate >= SHEET_UPDATE_INTERVAL) {
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        float t = temperature, h = humidity, s = soilValue;
        float n = N, p = P, k = K;
        xSemaphoreGive(dataMutex);
        
        Serial.println("Gửi dữ liệu lên Google Sheets: temp=" + String(t) + 
                       ", hum=" + String(h) + ", soil=" + String(s));
        guiLenGoogleSheet(t, h, s, n, p, k);
        lastSheetUpdate = millis();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// === Callback khi đồng bộ NTP ===
void timeavailable(struct timeval *tv) {
  Serial.println("Đồng bộ thời gian NTP thành công");
}

// === Task: Xử lý Firebase ===
void TaskFirebase(void *pvParameters) {
  UpdateData update;
  unsigned long lastFirebaseUpdate = 0;
  unsigned long lastFirebaseRead = 0;
  const unsigned long FIREBASE_UPDATE_INTERVAL = 2000;
  const unsigned long FIREBASE_READ_INTERVAL = 1000;
  
  for (;;) {
    // Xử lý gửi dữ liệu lên Firebase
    if (xQueueReceive(firebaseQueue, &update, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (xSemaphoreTake(firebaseMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (Firebase.ready() && millis() - lastFirebaseUpdate >= FIREBASE_UPDATE_INTERVAL) {
          if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            FirebaseJson json;
            json.set("mode", mode ? "1" : "0");
            json.set("light", light ? "1" : "0");
            json.set("pump", pump ? "1" : "0");
            json.set("temperature", temperature);
            json.set("humidity", humidity);
            json.set("soilValue", soilValue);
            json.set("N", N);
            json.set("P", P);
            json.set("K", K);
            xSemaphoreGive(dataMutex);

            if (Firebase.RTDB.setJSON(&fbdo, "/", &json)) {
              Serial.println("Cập nhật Firebase thành công");
              lastFirebaseUpdate = millis();
            } else {
              Serial.println("Lỗi cập nhật Firebase: " + fbdo.errorReason());
            }
          }
        }
        xSemaphoreGive(firebaseMutex);
      }
    }

    // Đọc lệnh từ Firebase
    if (millis() - lastFirebaseRead >= FIREBASE_READ_INTERVAL) {
      if (xSemaphoreTake(firebaseMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (Firebase.ready() && !firebaseCommandProcessing) {
          // Đọc mode
          if (Firebase.RTDB.getString(&fbdo, "/mode")) {
            String modeCmd = fbdo.stringData();
            bool newModeState = (modeCmd == "1");
            if (newModeState != mode) {
              if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                mode = newModeState;
                Serial.println(mode ? "Chuyển sang AUTO từ Firebase" : "Chuyển sang MANUAL từ Firebase");
                Serial2.println(mode ? "AUTO" : "MANUAL");
                UpdateData modeUpdate = {true, false, false, false, false};
                xQueueSend(displayQueue, &modeUpdate, 0);
                xSemaphoreGive(dataMutex);
              }
            }
          }
          
          // Chỉ điều khiển đèn và bơm khi ở chế độ MANUAL
          if (!mode) {
            if (Firebase.RTDB.getString(&fbdo, "/light")) {
              String lightCmd = fbdo.stringData();
              bool newLightState = (lightCmd == "1");
              if (newLightState != light && (millis() - lastLightChange >= MIN_CHANGE_INTERVAL)) {
                if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                  light = newLightState;
                  lastLightChange = millis();
                  Serial.println(light ? "Đèn BẬT từ Firebase (MANUAL)" : "Đèn TẮT từ Firebase (MANUAL)");
                  Serial2.println(light ? "LIGHT ON" : "LIGHT OFF");
                  UpdateData lightUpdate = {false, true, false, false, false};
                  xQueueSend(displayQueue, &lightUpdate, 0);
                  xSemaphoreGive(dataMutex);
                }
              }
            }
            
            // Đọc pump
            if (Firebase.RTDB.getString(&fbdo, "/pump")) {
              String pumpCmd = fbdo.stringData();
              bool newPumpState = (pumpCmd == "1");
              if (newPumpState != pump && (millis() - lastPumpChange >= MIN_CHANGE_INTERVAL)) {
                if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                  pump = newPumpState;
                  lastPumpChange = millis();
                  Serial.println(pump ? "Bơm BẬT từ Firebase (MANUAL)" : "Bơm TẮT từ Firebase (MANUAL)");
                  Serial2.println(pump ? "PUMP ON" : "PUMP OFF");
                  UpdateData pumpUpdate = {false, false, true, false, false};
                  xQueueSend(displayQueue, &pumpUpdate, 0);
                  xSemaphoreGive(dataMutex);
                }
              }
            }
          }
        }
        
        lastFirebaseRead = millis();
        xSemaphoreGive(firebaseMutex);
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// === Các task button ===
void ModeButton(void *pvParameters) {
  bool lastState = HIGH;
  unsigned long lastDebounce = 0;
  
  for (;;) {
    bool currentState = digitalRead(MODE_PIN);
    if (currentState != lastState && millis() - lastDebounce > 100) {
      if (currentState == LOW) {
        firebaseCommandProcessing = true;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          mode = !mode;
          Serial.println(mode ? "Chuyển sang AUTO (Nút nhấn)" : "Chuyển sang MANUAL (Nút nhấn)");
          Serial2.println(mode ? "AUTO" : "MANUAL");
          UpdateData update = {true, false, false, false, false};
          xQueueSend(displayQueue, &update, 0);
          if (signupOK) {
            xQueueSend(firebaseQueue, &update, 0);
          }
          xSemaphoreGive(dataMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(400));
        firebaseCommandProcessing = false;
      }
      lastDebounce = millis();
    }
    lastState = currentState;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// === Task: Xử lý nút đèn (đã sửa) ===
void LightButton(void *pvParameters) {
  bool lastState = HIGH;
  unsigned long lastDebounce = 0;
  
  for (;;) {
    bool currentState = digitalRead(LIGHT_PIN);
    if (currentState != lastState && millis() - lastDebounce > 100) {
      vTaskDelay(pdMS_TO_TICKS(10));
      if (currentState == LOW && !mode) {
        // Đặt cờ để tạm dừng xử lý lệnh từ Firebase
        firebaseCommandProcessing = true;
        
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          light = !light;
          Serial.println(light ? "Đèn (MANUAL) BẬT" : "Đèn (MANUAL) TẮT");
          Serial2.println(light ? "LIGHT ON" : "LIGHT OFF");
          UpdateData update = {false, true, false, false, false};
          xQueueSend(displayQueue, &update, 0);
          if (signupOK) {
            xQueueSend(firebaseQueue, &update, 0);
          }
          xSemaphoreGive(dataMutex);
        }
        
        // Delay để đảm bảo lệnh được gửi và xử lý
        vTaskDelay(pdMS_TO_TICKS(400));
        
        // Reset cờ để cho phép Firebase hoạt động trở lại
        firebaseCommandProcessing = false;
      }
      lastDebounce = millis();
    }
    lastState = currentState;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// === Task: Xử lý nút bơm (đã sửa) ===
void PumpButton(void *pvParameters) {
  bool lastState = HIGH;
  unsigned long lastDebounce = 0;
  
  for (;;) {
    bool currentState = digitalRead(PUMP_PIN);
    if (currentState != lastState && millis() - lastDebounce > 100) {
      vTaskDelay(pdMS_TO_TICKS(10));  
      if (currentState == LOW && !mode) {
        // Đặt cờ để tạm dừng xử lý lệnh từ Firebase
        firebaseCommandProcessing = true;
        
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          pump = !pump;
          Serial.println(pump ? "Bơm (MANUAL) BẬT" : "Bơm (MANUAL) TẮT");
          Serial2.println(pump ? "PUMP ON" : "PUMP OFF");
          UpdateData update = {false, false, true, false, false};
          xQueueSend(displayQueue, &update, 0);
          if (signupOK) {
            xQueueSend(firebaseQueue, &update, 0);
          }
          xSemaphoreGive(dataMutex);
        }
        
        // Delay để đảm bảo lệnh được gửi và xử lý
        vTaskDelay(pdMS_TO_TICKS(400));
        
        // Reset cờ để cho phép Firebase hoạt động trở lại
        firebaseCommandProcessing = false;
      }
      lastDebounce = millis();
    }
    lastState = currentState;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
// === Thiết lập ban đầu ===
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  Serial.println("Khởi động Zigbee A...");

  pinMode(MODE_PIN, INPUT_PULLUP);
  pinMode(LIGHT_PIN, INPUT_PULLUP);
  pinMode(PUMP_PIN, INPUT_PULLUP);

  // Tạo các mutex và queue trước khi sử dụng
  firebaseMutex = xSemaphoreCreateMutex();
  dataMutex = xSemaphoreCreateMutex();
  displayQueue = xQueueCreate(50, sizeof(UpdateData));
  firebaseQueue = xQueueCreate(50, sizeof(UpdateData));

  WiFi.begin(ssid, password);
  unsigned long wifiTimeout = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi đã kết nối");
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    config.timeout.serverResponse = 10000;
    config.timeout.wifiReconnect = 10000;
    
    if (Firebase.signUp(&config, &auth, "", "")) {
      Serial.println("Kết nối Firebase thành công!");
      signupOK = true;
    } else {
      Serial.printf("Lỗi kết nối Firebase: %s\n", config.signer.signupError.message.c_str());
    }
    
    config.token_status_callback = tokenStatusCallback;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
  } else {
    Serial.println("\nKhông thể kết nối WiFi!");
  }

  configTzTime(timeZone, ntpServer1, ntpServer2);
  sntp_set_time_sync_notification_cb(timeavailable);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(2);
  drawStaticLayout();
// Tạo các task
  xTaskCreatePinnedToCore(TaskTFT, "TFT", 8192, NULL, 2, &TaskTFT_Handle, 1);
  xTaskCreatePinnedToCore(TaskSerial, "Serial2", 8192, NULL, 1, &TaskSerial_Handle, 1);
  xTaskCreatePinnedToCore(ModeButton, "ModeBtn", 4096, NULL, 1, &ModeButton_Handle, 1);
  xTaskCreatePinnedToCore(LightButton, "LightBtn", 4096, NULL, 1, &LightButton_Handle, 1);
  xTaskCreatePinnedToCore(PumpButton, "PumpBtn", 4096, NULL, 1, &PumpButton_Handle, 1);
  xTaskCreatePinnedToCore(TaskSheet, "Sheet", 8192, NULL, 1, &TaskSheet_Handle, 1);
  xTaskCreatePinnedToCore(TaskFirebase, "Firebase", 12288, NULL, 2, &TaskFirebase_Handle, 1);

  // Khởi tạo trạng thái ban đầu và hiển thị
  Serial2.println(light ? "LIGHT ON" : "LIGHT OFF");
  Serial2.println(pump ? "PUMP ON" : "PUMP OFF");
  
  // Cập nhật hiển thị ban đầu
  UpdateData initialUpdate = {true, true, true, true, true};
  xQueueSend(displayQueue, &initialUpdate, pdMS_TO_TICKS(1000));
  
  Serial.println("Hệ thống đã khởi động hoàn tất!");
}

void loop() {
  // Loop trống - tất cả xử lý được thực hiện trong các task
}