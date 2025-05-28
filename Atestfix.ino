#include <SPI.h>
#include <DHT.h>
#include <HardwareSerial.h>

#define SOIL_PIN   12
#define LED_PIN    26
#define PUMP_PIN   27 
#define LIGHT_PIN  34

#define DHTPIN   15
#define DHTTYPE  DHT11
DHT dht(DHTPIN, DHTTYPE);

// NPK sensor pins
#define RE_PIN 13
#define DE_PIN 14
HardwareSerial RS485Serial(1); // Use Serial1 for NPK sensor

unsigned long prev_send = 0;
const uint16_t interval_send = 3000;

unsigned long prev_nhan = 0;
const uint16_t interval_nhan = 500; 

unsigned long prev_lightCheck = 0;
const uint16_t interval_light = 500;

unsigned long prev_pumpCheck = 0;
const uint16_t interval_pump = 500; 

bool dataAvailable = false;
String rcv = "";
String currentMode = "AUTO";

bool ledState = false;
bool pumpState = false;

const int soil_dry = 4095;
const int soil_wet = 1500;

const float soil_low_threshold = 50.0;
const float soil_high_threshold = 70.0;

// NPK variables
float N = 0.0; // Nitrogen
float P = 0.0; // Phosphorus
float K = 0.0; // Potassium 

// NPK sensor commands
uint8_t command_read_N[] = {0x01, 0x03, 0x00, 0x1E, 0x00, 0x01, 0xE4, 0x0C};
uint8_t command_read_P[] = {0x01, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xB5, 0xCC};
uint8_t command_read_K[] = {0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xC0};

uint8_t rawN[2] = {0xFF, 0xFF};
uint8_t rawP[2] = {0xFF, 0xFF};
uint8_t rawK[2] = {0xFF, 0xFF};

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  RS485Serial.begin(9600, SERIAL_8N1, 32, 33); // NPK on pins 32 (RX), 33 (TX)
  dht.begin();
  pinMode(LED_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(RE_PIN, OUTPUT);
  pinMode(DE_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(RE_PIN, LOW);
  digitalWrite(DE_PIN, LOW);
}

void readNPK() {
  // Read Nitrogen
  digitalWrite(RE_PIN, HIGH);
  digitalWrite(DE_PIN, HIGH);
  RS485Serial.write(command_read_N, 8);
  RS485Serial.flush();
  digitalWrite(RE_PIN, LOW);
  digitalWrite(DE_PIN, LOW);
  delay(300);
  if (RS485Serial.available() > 0 && RS485Serial.find(0x01)) {
    for (int i = 1; i < 7; i++) {
      uint8_t ch = RS485Serial.read();
      if (i == 3) rawN[0] = ch;
      if (i == 4) rawN[1] = ch;
    }
    N = (float)(rawN[0] * 256 + rawN[1]);
  }
  RS485Serial.flush();
  delay(300);

  // Read Phosphorus
  digitalWrite(RE_PIN, HIGH);
  digitalWrite(DE_PIN, HIGH);
  RS485Serial.write(command_read_P, 8);
  RS485Serial.flush();
  digitalWrite(RE_PIN, LOW);
  digitalWrite(DE_PIN, LOW);
  delay(300);
  if (RS485Serial.available() > 0 && RS485Serial.find(0x01)) {
    for (int i = 1; i < 7; i++) {
      uint8_t ch = RS485Serial.read();
      if (i == 3) rawP[0] = ch;
      if (i == 4) rawP[1] = ch;
    }
    P = (float)(rawP[0] * 256 + rawP[1]);
  }
  RS485Serial.flush();
  delay(300);

  // Read Potassium
  digitalWrite(RE_PIN, HIGH);
  digitalWrite(DE_PIN, HIGH);
  RS485Serial.write(command_read_K, 8);
  RS485Serial.flush();
  digitalWrite(RE_PIN, LOW);
  digitalWrite(DE_PIN, LOW);
  delay(300);
  if (RS485Serial.available() > 0 && RS485Serial.find(0x01)) {
    for (int i = 1; i < 7; i++) {
      uint8_t ch = RS485Serial.read();
      if (i == 3) rawK[0] = ch;
      if (i == 4) rawK[1] = ch;
    }
    K = (float)(rawK[0] * 256 + rawK[1]);
  }
  RS485Serial.flush();
}

void chedo() {
  if (rcv == "AUTO" || rcv == "MANUAL") {
    currentMode = rcv;
    Serial.println("Chuyển mode sang: " + currentMode);
    Serial2.println(ledState ? "LIGHT ON" : "LIGHT OFF");
    Serial2.println(pumpState ? "PUMP ON" : "PUMP OFF");
  // } else if (rcv == "STATUS_REQUEST") {
  //   Serial2.println(ledState ? "LIGHT ON" : "LIGHT OFF");
  //   Serial2.println(pumpState ? "PUMP ON" : "PUMP OFF");
  } else if (rcv == "LIGHT ON" && currentMode == "MANUAL") {
    digitalWrite(LED_PIN, HIGH);
    ledState = true;
    Serial.println("Thủ công: BẬT đèn");
    Serial2.println("LIGHT ON");
  } else if (rcv == "LIGHT OFF" && currentMode == "MANUAL") {
    digitalWrite(LED_PIN, LOW);
    ledState = false;
    Serial.println("Thủ công: TẮT đèn");
    Serial2.println("LIGHT OFF");
  } else if (rcv == "PUMP ON" && currentMode == "MANUAL") {
    digitalWrite(PUMP_PIN, HIGH);
    pumpState = true;
    Serial.println("Thủ công: BẬT máy bơm");
    Serial2.println("PUMP ON");
  } else if (rcv == "PUMP OFF" && currentMode == "MANUAL") {
    digitalWrite(PUMP_PIN, LOW);
    pumpState = false;
    Serial.println("Thủ công: TẮT máy bơm");
    Serial2.println("PUMP OFF");
  }
}

void nhan(unsigned long now) {
  if (now - prev_nhan > interval_nhan) {
    if (Serial2.available()) {
      rcv = Serial2.readStringUntil('\n');
      rcv.trim();
      dataAvailable = true;
      Serial.println("Received from Zigbee A: " + rcv);
    }
    prev_nhan = now;
  }
}

void autoLightControl(unsigned long now) {
  if (now - prev_lightCheck > interval_light && currentMode == "AUTO") {
    int light = analogRead(LIGHT_PIN);
    if (light < 2000 && ledState != false) {
      digitalWrite(LED_PIN, LOW);
      Serial2.println("LIGHT OFF");
      Serial.println("Tự động: TẮT đèn");
      dataAvailable = true;
      ledState = false;
    } else if (light >= 2000 && ledState != true) {
      digitalWrite(LED_PIN, HIGH);
      Serial2.println("LIGHT ON");
      Serial.println("Tự động: BẬT đèn");
      dataAvailable = true;
      ledState = true;
    }
    prev_lightCheck = now;
  }
}

void autoPumpControl(unsigned long now, float soilPercent) {
  if (now - prev_pumpCheck > interval_pump && currentMode == "AUTO") {
    if (soilPercent < soil_low_threshold && pumpState != true) {
      digitalWrite(PUMP_PIN, HIGH);
      Serial2.println("PUMP ON");
      Serial.println("Tự động: BẬT máy bơm");
      dataAvailable = true;
      pumpState = true;
    } else if (soilPercent >= soil_high_threshold && pumpState != false) {
      digitalWrite(PUMP_PIN, LOW);
      Serial2.println("PUMP OFF");
      Serial.println("Tự động: TẮT máy bơm");
      dataAvailable = true;
      pumpState = false;
    }
    prev_pumpCheck = now;
  }
}

void loop() {
  unsigned long now = millis();
  nhan(now);
  if (dataAvailable) {
    chedo();
    dataAvailable = false;
  }
  autoLightControl(now);
  float soilPercent = 0.0;
  if (now - prev_send > interval_send) {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    if (!isnan(temp) && !isnan(hum)) {
      String data = "TEMP=" + String(temp) + " HUM=" + String(hum);
      Serial.println("Sending: " + data);
      Serial2.println(data);
    } else {
      Serial.println("Lỗi đọc DHT11!");
    }
    int soilValueRaw = analogRead(SOIL_PIN);
    soilPercent = ((float)(soil_dry - soilValueRaw)) * 100.0 / (soil_dry - soil_wet);
    soilPercent = constrain(soilPercent, 0, 100);
    String soilData = "SOIL=" + String(soilPercent, 1);
    Serial.println("Sending: " + soilData);
    Serial2.println(soilData);

    // Read and send NPK data
    readNPK();
    String npkData = "N=" + String(N, 1) + " P=" + String(P, 1) + " K=" + String(K, 1);
    Serial.println("Sending: " + npkData);
    Serial2.println(npkData);

    prev_send = now;
  }
  autoPumpControl(now, soilPercent);
}