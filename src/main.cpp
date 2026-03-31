#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <SPIFFS.h>

// ==================== ОПРЕДЕЛЕНИЯ ПИНОВ ====================
#define DHT_PIN 17
#define SOIL_SENSOR_PIN 34
#define LED_PIN 12
#define FAN_PWM_PIN 18
#define BUTTON_LED_PIN 16
#define BUTTON_FAN_PIN 27
#define SERVO_PIN 23
#define BUZZER_PIN 25

// I2C для LCD
#define I2C_SDA 21
#define I2C_SCL 22

// ==================== НАСТРОЙКИ ====================
// Wi-Fi (значения по умолчанию, перезаписываются из config.json)
String cfgWifiSsid = "Ivan";
String cfgWifiPassword = "1234567000";
String cfgApSsid = "ESP32_SmartHome";
String cfgApPassword = "12345678";

#define DHT_TYPE DHT11
#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

#define FAN_PWM_FREQ 5000
#define FAN_PWM_RESOLUTION 8
#define FAN_PWM_CHANNEL 2
#define BUZZER_PWM_CHANNEL 4

// Настраиваемые из config.json (значения по умолчанию)
float cfgTempThreshold = 27.0;
int cfgFanMinSpeed = 30;
int cfgFanSpeedStep = 50;
int cfgWindowOpenAngle = 90;
int cfgWindowClosedAngle = 0;

#define SOIL_READ_INTERVAL 2000
#define DHT_READ_INTERVAL 2000
#define BUTTON_DEBOUNCE_DELAY 50
#define WINDOW_CHECK_INTERVAL 5000

// НАСТРОЙКИ СЕРВОПРИВОДА
#define SERVO_MIN_PULSE_WIDTH 544   // Минимальная ширина импульса (мкс) - стандарт
#define SERVO_MAX_PULSE_WIDTH 2400  // Максимальная ширина импульса (мкс) - стандарт
#define SERVO_FREQUENCY 50          // Частота 50 Гц

// ==================== ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ====================
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
DHT dht(DHT_PIN, DHT_TYPE);
WebServer server(80);
Servo windowServo;

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
bool ledState = false;
int fanSpeed = 0;

float temperature = 0.0;
float humidity = 0.0;
int soilMoisture = 0;
String deviceIP = "";
String connectionStatus = "Отключаемся...";

// Переменные для окна
bool windowOpen = false;
int currentWindowAngle = 0;

// Переменные для DJ/Buzzer
bool melodyPlaying = false;
int melodyIndex = 0;
int currentMelody = -1;
unsigned long nextNoteTime = 0;
int melodyTempo = 120; // BPM

// Ноты (частоты в Гц)
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_REST 0

// Мелодия: {частота, длительность (в долях, 4=четверть, 8=восьмая)}
// Mario
const int marioMelody[][2] = {
    {NOTE_E5,8},{NOTE_E5,8},{NOTE_REST,8},{NOTE_E5,8},{NOTE_REST,8},{NOTE_C5,8},{NOTE_E5,4},
    {NOTE_G5,4},{NOTE_REST,4},{NOTE_G4,4},{NOTE_REST,4},
    {NOTE_C5,4},{NOTE_REST,8},{NOTE_G4,4},{NOTE_REST,8},{NOTE_E4,4},
    {NOTE_REST,8},{NOTE_A4,4},{NOTE_B4,4},{NOTE_AS4,8},{NOTE_A4,4},
    {NOTE_G4,3},{NOTE_E5,3},{NOTE_G5,3},{NOTE_A5,4},{NOTE_F5,8},{NOTE_G5,8},
    {NOTE_REST,8},{NOTE_E5,4},{NOTE_C5,8},{NOTE_D5,8},{NOTE_B4,4}
};
const int marioLen = sizeof(marioMelody) / sizeof(marioMelody[0]);

// Imperial March
const int imperialMelody[][2] = {
    {NOTE_A4,4},{NOTE_A4,4},{NOTE_A4,4},{NOTE_F4,6},{NOTE_C5,8},
    {NOTE_A4,4},{NOTE_F4,6},{NOTE_C5,8},{NOTE_A4,2},
    {NOTE_E5,4},{NOTE_E5,4},{NOTE_E5,4},{NOTE_F5,6},{NOTE_C5,8},
    {NOTE_GS4,4},{NOTE_F4,6},{NOTE_C5,8},{NOTE_A4,2}
};
const int imperialLen = sizeof(imperialMelody) / sizeof(imperialMelody[0]);

// Nokia tune
const int nokiaMelody[][2] = {
    {NOTE_E5,8},{NOTE_D5,8},{NOTE_FS4,4},{NOTE_GS4,4},
    {NOTE_CS4,8},{NOTE_B4,8},{NOTE_D4,4},{NOTE_E4,4},
    {NOTE_B4,8},{NOTE_A4,8},{NOTE_CS4,4},{NOTE_E4,4},
    {NOTE_A4,2}
};
const int nokiaLen = sizeof(nokiaMelody) / sizeof(nokiaMelody[0]);

// Переменные для кнопок
int lastButtonLedState = HIGH;
int buttonLedState = HIGH;
unsigned long lastDebounceTimeLed = 0;

int lastButtonFanState = HIGH;
int buttonFanState = HIGH;
unsigned long lastDebounceTimeFan = 0;

unsigned long lastSoilReadTime = 0;
unsigned long lastDHTReadTime = 0;
unsigned long lastLCDUpdateTime = 0;

bool lcdNeedsUpdate = false;
String lcdMessage = "";
// ==================== ЧТЕНИЕ ДАННЫХ ИЗ SPIFFS ====================
void readDataFiles() {
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ Ошибка монтирования SPIFFS!");
        return;
    }
    Serial.println("✅ SPIFFS смонтирован");

    // Вывод списка всех файлов
    Serial.println("\n=== ФАЙЛЫ В SPIFFS ===");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
        Serial.print("  📄 ");
        Serial.print(file.name());
        Serial.print("  (");
        Serial.print(file.size());
        Serial.println(" байт)");
        file = root.openNextFile();
    }

    // Чтение и парсинг config.json
    if (SPIFFS.exists("/config.json")) {
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, configFile);
            configFile.close();

            if (error) {
                Serial.print("❌ Ошибка парсинга JSON: ");
                Serial.println(error.c_str());
                return;
            }

            // Присваиваем значения из файла
            if (doc.containsKey("wifi_ssid"))          cfgWifiSsid = doc["wifi_ssid"].as<String>();
            if (doc.containsKey("wifi_password"))      cfgWifiPassword = doc["wifi_password"].as<String>();
            if (doc.containsKey("ap_ssid"))            cfgApSsid = doc["ap_ssid"].as<String>();
            if (doc.containsKey("ap_password"))        cfgApPassword = doc["ap_password"].as<String>();
            if (doc.containsKey("temp_threshold"))     cfgTempThreshold = doc["temp_threshold"].as<float>();
            if (doc.containsKey("fan_min_speed"))      cfgFanMinSpeed = doc["fan_min_speed"].as<int>();
            if (doc.containsKey("fan_speed_step"))     cfgFanSpeedStep = doc["fan_speed_step"].as<int>();
            if (doc.containsKey("window_open_angle"))  cfgWindowOpenAngle = doc["window_open_angle"].as<int>();
            if (doc.containsKey("window_closed_angle"))cfgWindowClosedAngle = doc["window_closed_angle"].as<int>();

            // Вывод загруженных значений в консоль
            Serial.println("\n=== ЗАГРУЖЕНО ИЗ config.json ===");
            Serial.println("WiFi SSID: " + cfgWifiSsid);
            Serial.println("WiFi Password: " + cfgWifiPassword);
            Serial.println("AP SSID: " + cfgApSsid);
            Serial.println("AP Password: " + cfgApPassword);
            Serial.print("Порог температуры: "); Serial.println(cfgTempThreshold);
            Serial.print("Мин. скорость вент.: "); Serial.println(cfgFanMinSpeed);
            Serial.print("Шаг скорости вент.: "); Serial.println(cfgFanSpeedStep);
            Serial.print("Угол открытия окна: "); Serial.println(cfgWindowOpenAngle);
            Serial.print("Угол закрытия окна: "); Serial.println(cfgWindowClosedAngle);
            Serial.println("================================");
        }
    } else {
        Serial.println("⚠️ /config.json не найден, используются значения по умолчанию");
    }
}

// ==================== НАСТРОЙКА WI-FI ====================
void setupWiFi() {
    Serial.println();
    Serial.println("=== НАСТРОЙКА Wi-Fi ===");
    
    Serial.print("Подключение к сети: ");
    Serial.println(cfgWifiSsid.c_str());
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(cfgWifiSsid.c_str(), cfgWifiPassword.c_str());
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    lcd.setCursor(0, 1);
    lcd.print(cfgWifiSsid.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
        
        lcd.setCursor(attempts % 16, 1);
        lcd.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println("✅ WiFi подключен!");
        Serial.print("IP адрес: ");
        Serial.println(WiFi.localIP());
        
        deviceIP = WiFi.localIP().toString();
        connectionStatus = "Подключено к " + cfgWifiSsid;
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Connected");
        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP().toString().substring(0, 16));
    } else {
        Serial.println();
        Serial.println("❌ Не удалось подключиться к WiFi");
        Serial.println("Запускаем точку доступа...");
        
        WiFi.softAP(cfgApSsid.c_str(), cfgApPassword.c_str());
        
        deviceIP = WiFi.softAPIP().toString();
        connectionStatus = "Режим AP: " + cfgApSsid;
        
        Serial.print("Точка доступа запущена. IP: ");
        Serial.println(deviceIP);
        Serial.print("SSID: ");
        Serial.println(cfgApSsid.c_str());
        Serial.print("Пароль: ");
        Serial.println(cfgApPassword.c_str());
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("AP Mode");
        lcd.setCursor(0, 1);
        lcd.print(deviceIP);
    }
    
    delay(2000);
}

// ==================== ОБРАБОТЧИКИ ВЕБ-СЕРВЕРА ====================
void handleRoot() {
    File file = SPIFFS.open("/index.html", "r");
    if (file) {
        server.streamFile(file, "text/html");
        file.close();
    } else {
        server.send(500, "text/plain", "index.html not found in SPIFFS");
    }
}

void handleAPI() {
    StaticJsonDocument<512> doc;
    
    doc["temperature"] = temperature;
    doc["humidity"] = humidity;
    doc["soil"] = soilMoisture;
    doc["led"] = ledState;
    doc["fan"] = fanSpeed;
    doc["ip"] = deviceIP;
    doc["wifiStatus"] = connectionStatus;
    doc["windowOpen"] = windowOpen;
    doc["windowAngle"] = currentWindowAngle;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleLED() {
    if (server.hasArg("state")) {
        String state = server.arg("state");
        
        if (state == "on") {
            ledState = true;
            digitalWrite(LED_PIN, HIGH);
            lcdNeedsUpdate = true;
            lcdMessage = "LED: ON (Web)";
            Serial.println("LED включен через веб");
        } else if (state == "off") {
            ledState = false;
            digitalWrite(LED_PIN, LOW);
            lcdNeedsUpdate = true;
            lcdMessage = "LED: OFF (Web)";
            Serial.println("LED выключен через веб");
        }
        
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}

void handleFan() {
    if (server.hasArg("speed")) {
        int speed = server.arg("speed").toInt();
        
        if (speed < 0) speed = 0;
        if (speed > 255) speed = 255;
        
        fanSpeed = speed;
        
        if (fanSpeed == 0) {
            ledcWrite(FAN_PWM_CHANNEL, 0);
            lcdMessage = "Fan: OFF (Web)";
        } else {
            if (fanSpeed < cfgFanMinSpeed && fanSpeed > 0) {
                fanSpeed = cfgFanMinSpeed;
            }
            ledcWrite(FAN_PWM_CHANNEL, fanSpeed);
            lcdMessage = "Fan: " + String(map(fanSpeed, 0, 255, 0, 100)) + "% (Web)";
        }
        
        lcdNeedsUpdate = true;
        Serial.print("Скорость вентилятора (веб): ");
        Serial.println(fanSpeed);
        
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}
void handleStyle() {
    File file = SPIFFS.open("/style.css", "r");
    if (file) {
        server.streamFile(file, "text/css");
        file.close();
    } else {
        server.send(404, "text/plain", "style.css not found");
    }
}

void handleScript() {
    File file = SPIFFS.open("/app.js", "r");
    if (file) {
        server.streamFile(file, "application/javascript");
        file.close();
    } else {
        server.send(404, "text/plain", "app.js not found");
    }
}

// ==================== DJ / BUZZER ====================
void buzzerTone(int freq, int duration) {
    if (freq == 0) {
        ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
    } else {
        ledcWriteTone(BUZZER_PWM_CHANNEL, freq);
    }
}

void buzzerStop() {
    ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
    melodyPlaying = false;
    currentMelody = -1;
    melodyIndex = 0;
}

void startMelody(int id) {
    buzzerStop();
    currentMelody = id;
    melodyIndex = 0;
    melodyPlaying = true;
    nextNoteTime = 0;
}

void updateMelody() {
    if (!melodyPlaying) return;
    if (millis() < nextNoteTime) return;

    const int (*melody)[2] = NULL;
    int len = 0;

    switch (currentMelody) {
        case 0: melody = marioMelody; len = marioLen; break;
        case 1: melody = imperialMelody; len = imperialLen; break;
        case 2: melody = nokiaMelody; len = nokiaLen; break;
        default: buzzerStop(); return;
    }

    if (melodyIndex >= len) {
        buzzerStop();
        return;
    }

    int freq = melody[melodyIndex][0];
    int divider = melody[melodyIndex][1];
    int noteDuration = (60000 / melodyTempo) * 4 / divider;

    buzzerTone(freq, noteDuration);
    nextNoteTime = millis() + noteDuration * 0.9;

    // Пауза между нотами
    if (freq != NOTE_REST) {
        nextNoteTime = millis() + noteDuration;
    } else {
        ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
        nextNoteTime = millis() + noteDuration;
    }

    melodyIndex++;

    if (melodyIndex >= len) {
        // Через длительность последней ноты — стоп
        nextNoteTime = millis() + noteDuration;
    }
}

void handleBuzzer() {
    if (server.hasArg("action")) {
        String action = server.arg("action");

        if (action == "note" && server.hasArg("freq")) {
            int freq = server.arg("freq").toInt();
            int dur = server.hasArg("dur") ? server.arg("dur").toInt() : 200;
            buzzerStop();
            buzzerTone(freq, dur);
            // Авто-стоп через dur мс
            lcdNeedsUpdate = true;
            lcdMessage = "DJ: " + String(freq) + "Hz";
        } else if (action == "melody" && server.hasArg("id")) {
            int id = server.arg("id").toInt();
            startMelody(id);
            lcdNeedsUpdate = true;
            lcdMessage = "DJ: Melody #" + String(id);
        } else if (action == "stop") {
            buzzerStop();
            lcdNeedsUpdate = true;
            lcdMessage = "DJ: Stop";
        } else if (action == "tempo" && server.hasArg("bpm")) {
            melodyTempo = server.arg("bpm").toInt();
            if (melodyTempo < 40) melodyTempo = 40;
            if (melodyTempo > 300) melodyTempo = 300;
        }

        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}

// ==================== УПРАВЛЕНИЕ ОКНОМ ====================
void setWindow(bool open) {
    int angle = open ? cfgWindowOpenAngle : cfgWindowClosedAngle;
    windowServo.write(angle);
    currentWindowAngle = angle;
    windowOpen = open;

    Serial.print("Окно: ");
    Serial.print(open ? "ОТКРЫТО" : "ЗАКРЫТО");
    Serial.print(" (угол=");
    Serial.print(angle);
    Serial.println("°)");

    lcdNeedsUpdate = true;
    lcdMessage = open ? "Window: OPEN" : "Window: CLOSED";
}

void handleWindow() {
    if (server.hasArg("action")) {
        String action = server.arg("action");
        if (action == "open") {
            setWindow(true);
        } else if (action == "close") {
            setWindow(false);
        }
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}

void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/api", handleAPI);
    server.on("/led", handleLED);
    server.on("/fan", handleFan);
    server.on("/window", handleWindow);
    server.on("/buzzer", handleBuzzer);
    server.serveStatic("/", SPIFFS, "/");

    server.begin();
    Serial.println("HTTP сервер запущен");
    Serial.print("Для подключения открой браузер: http://");
    Serial.println(deviceIP);
}

// ==================== ОБРАБОТКА КНОПОК ====================
void handleLedButton() {
    int reading = digitalRead(BUTTON_LED_PIN);
    
    if (reading != lastButtonLedState) {
        lastDebounceTimeLed = millis();
    }
    
    if ((millis() - lastDebounceTimeLed) > BUTTON_DEBOUNCE_DELAY) {
        if (reading != buttonLedState) {
            buttonLedState = reading;
            
            if (buttonLedState == LOW) {
                ledState = !ledState;
                digitalWrite(LED_PIN, ledState ? HIGH : LOW);
                
                lcdNeedsUpdate = true;
                lcdMessage = "LED: " + String(ledState ? "ON" : "OFF") + " (Button)";
                
                Serial.print("Кнопка LED нажата! LED ");
                Serial.println(ledState ? "включен" : "выключен");
                
                delay(200);
            }
        }
    }
    
    lastButtonLedState = reading;
}

void handleFanButton() {
    int reading = digitalRead(BUTTON_FAN_PIN);
    
    if (reading != lastButtonFanState) {
        lastDebounceTimeFan = millis();
    }
    
    if ((millis() - lastDebounceTimeFan) > BUTTON_DEBOUNCE_DELAY) {
        if (reading != buttonFanState) {
            buttonFanState = reading;
            
            if (buttonFanState == LOW) {
                if (fanSpeed == 0) {
                    fanSpeed = cfgFanMinSpeed;
                } else {
                    fanSpeed += cfgFanSpeedStep;
                    if (fanSpeed > 255) {
                        fanSpeed = 0;
                    }
                }
                
                ledcWrite(FAN_PWM_CHANNEL, fanSpeed);
                
                lcdNeedsUpdate = true;
                lcdMessage = "Fan: " + String(map(fanSpeed, 0, 255, 0, 100)) + "% (Button)";
                
                Serial.print("Кнопка вентилятора нажата! Скорость: ");
                Serial.print(map(fanSpeed, 0, 255, 0, 100));
                Serial.println("%");
                
                delay(200);
            }
        }
    }
    
    lastButtonFanState = reading;
}

// ==================== РАБОТА С LCD ====================
void updateLCD(String line1, String line2) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1.substring(0, 16));
    lcd.setCursor(0, 1);
    lcd.print(line2.substring(0, 16));
}

void updateLCDAuto() {
    if (!lcdNeedsUpdate && millis() - lastLCDUpdateTime < 3000) {
        return;
    }
    
    if (lcdNeedsUpdate) {
        updateLCD("Action:", lcdMessage);
        lcdNeedsUpdate = false;
        lastLCDUpdateTime = millis();
    } else {
        String line1 = "T:" + String(temperature, 1) + "C";
        String line2 = "Soil:" + String(soilMoisture) + "%";
        
        if (windowOpen) {
            line1 += " Win:OPEN";
        } else {
            line1 += " Win:CLSD";
        }
        
        line2 += " L:" + String(digitalRead(BUTTON_LED_PIN) == LOW ? "1" : "0");
        line2 += " F:" + String(digitalRead(BUTTON_FAN_PIN) == LOW ? "1" : "0");
        
        updateLCD(line1, line2);
        lastLCDUpdateTime = millis();
    }
}
// ==================== ЧТЕНИЕ ДАТЧИКОВ ====================
void readSensors() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastDHTReadTime >= DHT_READ_INTERVAL) {
        float newTemp = dht.readTemperature();
        float newHum = dht.readHumidity();
        
        if (!isnan(newTemp) && !isnan(newHum)) {
            temperature = newTemp;
            humidity = newHum;
            Serial.print("Температура: ");
            Serial.print(temperature);
            Serial.print("°C, Влажность: ");
            Serial.print(humidity);
            Serial.println("%");
        } else {
            Serial.println("Ошибка чтения DHT11");
        }
        
        lastDHTReadTime = currentTime;
    }
    
    if (currentTime - lastSoilReadTime >= SOIL_READ_INTERVAL) {
        int rawValue = analogRead(SOIL_SENSOR_PIN);
        soilMoisture = map(rawValue, 0, 4095, 0, 100);
        
        Serial.print("Влажность почвы (RAW): ");
        Serial.print(rawValue);
        Serial.print(", (%): ");
        Serial.println(soilMoisture);
        
        lastSoilReadTime = currentTime;
    }
    
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== ESP32 Умный дом запуск ===");

    // Чтение данных из SPIFFS и вывод в консоль
    readDataFiles();

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    pinMode(BUTTON_LED_PIN, INPUT_PULLUP);
    pinMode(BUTTON_FAN_PIN, INPUT_PULLUP);
    
    Serial.println("Кнопки настроены:");
    Serial.println("  - LED: GPIO 16");
    Serial.println("  - Вентилятор: GPIO 27");
    
    // НАСТРОЙКА СЕРВОПРИВОДА
    Serial.println("\n=== НАСТРОЙКА СЕРВОПРИВОДА ===");
    windowServo.setPeriodHertz(SERVO_FREQUENCY);
    windowServo.attach(SERVO_PIN, SERVO_MIN_PULSE_WIDTH, SERVO_MAX_PULSE_WIDTH);
    setWindow(false);
    delay(500);
    Serial.println("Сервопривод настроен");
    
    ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RESOLUTION);
    ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CHANNEL);
    ledcWrite(FAN_PWM_CHANNEL, 0);

    // НАСТРОЙКА BUZZER
    ledcSetup(BUZZER_PWM_CHANNEL, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_PWM_CHANNEL);
    ledcWriteTone(BUZZER_PWM_CHANNEL, 0);
    Serial.println("Buzzer настроен на GPIO 25");
    
    Wire.begin(I2C_SDA, I2C_SCL);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.print("ESP32 System");
    lcd.setCursor(0, 1);
    lcd.print("Initializing...");
    
    dht.begin();
    
    setupWiFi();
    setupWebServer();
    
    lcd.clear();
    lcd.print("System Ready!");
    lcd.setCursor(0, 1);
    lcd.print(deviceIP);
    
    Serial.println("\n=== СИСТЕМА ГОТОВА ===");
    Serial.println("=== УПРАВЛЕНИЕ ОКНОМ ===");
    Serial.print("Температура открытия: >");
    Serial.print(cfgTempThreshold);
    Serial.println("°C");
    Serial.println("Сервопривод: готов");
    Serial.println("===================\n");
}

// ==================== LOOP ====================
void loop() {
    server.handleClient();
    readSensors();
    handleLedButton();
    handleFanButton();
    updateMelody();
    updateLCDAuto();
    delay(10);
}