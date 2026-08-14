/*
BarakatTime — прошивка для Waveshare ESP32-S3-Touch-LCD-1.69
Дизайн: премиальный тёмно-зелёный интерфейс с золотыми акцентами,
карточками и векторной графикой (без растровых картинок)
*/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <math.h>
#include "HWCDC.h"
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "SensorPCF85063.hpp"
#include "pin_config.h"

// ---------- Векторные шрифты (встроены в Arduino_GFX, ничего конвертировать не нужно) ----------
#include <Fonts/FreeSansBold24pt7b.h>   // крупные цифры (счётчик тасбиха, время до молитвы)
#include <Fonts/FreeSansBold12pt7b.h>   // подзаголовки, время в списке молитв
#include <Fonts/FreeSans9pt7b.h>        // подписи, мелкий текст
#include <Fonts/FreeSansBold9pt7b.h>    // жирные мелкие подписи (BarakatTime, RAMADAN)

HWCDC USBSerial;

// ---------- Экран ----------
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0 /* rotation */, true /* IPS */,
                                       LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

// ---------- Тач ----------
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);

std::unique_ptr<Arduino_IIC> CST816T(new Arduino_CST816x(
    IIC_Bus, CST816T_DEVICE_ADDRESS, TP_RST, TP_INT, Arduino_IIC_Touch_Interrupt));

void Arduino_IIC_Touch_Interrupt(void) {
  CST816T->IIC_Interrupt_Flag = true;
}

bool touchReady = false;

// ---------- RTC ----------
SensorPCF85063 rtc;
bool rtcReady = false;

// ---------- Настройки и WiFi ----------
Preferences preferences;
WebServer server(80);

String ssid = "";
String password = "";
String city = "Moscow";
String country = "Russia";
int gmtOffsetSec = 10800;

// ---------- Состояние приложения ----------
enum AppScreen { SCREEN_TASBEEH, SCREEN_PRAYER_TIMES, SCREEN_RAMADAN, SCREEN_SETUP_MODE };
AppScreen currentScreen = SCREEN_TASBEEH;

// ---------- Палитра ----------
#define COLOR_BG          0x0120  // глубокий тёмно-зелёный
#define COLOR_BG_LIGHT    0x0221  // чуть светлее фон для карточек
#define COLOR_GOLD        0xFD20  // премиальный золотой
#define COLOR_GOLD_DIM    0x9440  // приглушённое золото (фоновое кольцо)
#define COLOR_CARD_BG     0x1A42  // карточка
#define COLOR_CARD_BORDER 0x3AA6  // тонкая окантовка карточки
#define COLOR_CARD_BORDER_DIM 0x2124 // внутренняя тёмная окантовка (эффект глубины)
#define COLOR_TEXT_DIM    0x7BEF  // приглушённый серо-зелёный текст
#define COLOR_TEXT_WHITE  0xFFFF

// ---------- Зикры ----------
const char *zikrNames[] = {"SubhanAllah", "Alhamdulillah", "AllahuAkbar", "LaIlahaIllaAllah"};
const uint8_t zikrCount = sizeof(zikrNames) / sizeof(zikrNames[0]);
uint8_t currentZikrIndex = 0;
uint32_t tasbeehCount = 0;

// ---------- Молитвы ----------
struct PrayerTimes {
  String fajr = "--:--";
  String dhuhr = "--:--";
  String asr = "--:--";
  String maghrib = "--:--";
  String isha = "--:--";
  String nextPrayerName = "Maghrib";
  String timeLeft = "--:--:--";
};
PrayerTimes prayers;
unsigned long lastPrayerFetch = 0;

// ---------- Кнопка PWR (SYS_OUT) ----------
struct PwrButton {
  bool lastRaw = HIGH;
  unsigned long pressStart = 0;
  bool longPressFired = false;
  bool waitingSecondClick = false;
  unsigned long firstClickTime = 0;
};
PwrButton pwrBtn;

const unsigned long DEBOUNCE_MS = 30;
const unsigned long LONG_PRESS_MS = 1000;
const unsigned long DOUBLE_CLICK_WINDOW_MS = 350;
unsigned long lastDebounceTime = 0;
bool debouncedState = HIGH;

// ---------- Прототипы ----------
void setupBuzzerSafe();
void setupPowerLatch();
void setupDisplay();
void setupTouch();
void setupRtc();
void loadSettings();
void connectWiFiOrStartAP();
void startConfigServer();
void fetchPrayerTimes();
void updateScreenContent();
void drawTasbeehScreen();
void drawPrayerScreen();
void drawRamadanScreen();
void drawSetupScreen();
void drawPageDots(uint8_t activePage);
void handleTouch();
void handlePwrButton();
void onSingleClick();
void onDoubleClick();
void onLongPress();

// ---------- Утилиты отрисовки ----------

// Карточка с эффектом глубины: тёмная внутренняя рамка + светлая внешняя
void drawCard(int x, int y, int w, int h, int r, bool withBorder = true) {
  gfx->fillRoundRect(x, y, w, h, r, COLOR_CARD_BG);
  if (withBorder) {
    gfx->drawRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, COLOR_CARD_BORDER_DIM);
    gfx->drawRoundRect(x, y, w, h, r, COLOR_CARD_BORDER);
  }
}

// Текст по центру относительно заданной ширины области — работает с любым векторным шрифтом
void drawCenteredText(const char *text, int areaX, int areaW, int baselineY,
                       const GFXfont *font, uint16_t color) {
  gfx->setFont(font);
  gfx->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int x = areaX + (areaW - (int)w) / 2 - x1;
  gfx->setCursor(x, baselineY);
  gfx->print(text);
}

// Текст по левому/правому краю с заданным шрифтом (для строк списка молитв)
void drawText(const char *text, int x, int baselineY, const GFXfont *font, uint16_t color) {
  gfx->setFont(font);
  gfx->setTextColor(color);
  gfx->setCursor(x, baselineY);
  gfx->print(text);
}

// Кольцевой сегмент (arc) между двумя радиусами и углами (градусы, 0 = вправо, по часовой)
void drawRingArc(int cx, int cy, int rOuter, int rInner, float startDeg, float endDeg, uint16_t color) {
  for (float a = startDeg; a <= endDeg; a += 0.8f) {
    float rad = a * PI / 180.0f;
    int x1 = cx + cos(rad) * rOuter;
    int y1 = cy + sin(rad) * rOuter;
    int x2 = cx + cos(rad) * rInner;
    int y2 = cy + sin(rad) * rInner;
    gfx->drawLine(x1, y1, x2, y2, color);
  }
}

// Утолщённое кольцо прогресса (двойной проход для более "жирной" линии без сглаживания)
void drawProgressRing(int cx, int cy, int rOuter, int rInner, float progress) {
  drawRingArc(cx, cy, rOuter, rInner, -90, 270, COLOR_GOLD_DIM);
  drawRingArc(cx, cy - 1, rOuter, rInner, -90, 270, COLOR_GOLD_DIM);

  float endDeg = -90 + 360.0f * progress;
  drawRingArc(cx, cy, rOuter, rInner, -90, endDeg, COLOR_GOLD);
  drawRingArc(cx, cy - 1, rOuter, rInner, -90, endDeg, COLOR_GOLD);
}

// Полумесяц (для экрана Рамадана)
void drawCrescent(int cx, int cy, int r) {
  gfx->fillCircle(cx, cy, r, COLOR_GOLD);
  gfx->fillCircle(cx + r / 2, cy - r / 6, r, COLOR_CARD_BG);
}

void setup() {
  setupBuzzerSafe();
  setupPowerLatch();

  pinMode(SYS_OUT, INPUT);

  USBSerial.begin(115200);
  delay(200);
  USBSerial.println("BarakatTime boot");

  setupDisplay();
  setupTouch();
  setupRtc();
  loadSettings();

  connectWiFiOrStartAP();

  currentScreen = SCREEN_TASBEEH;
  drawTasbeehScreen();
}

void loop() {
  if (currentScreen == SCREEN_SETUP_MODE) {
    server.handleClient();
    handlePwrButton();
    delay(5);
    return;
  }

  handleTouch();
  handlePwrButton();

  if (WiFi.status() == WL_CONNECTED && (millis() - lastPrayerFetch > 3600000 || lastPrayerFetch == 0)) {
    fetchPrayerTimes();
  }

  static unsigned long lastSecTick = 0;
  if (millis() - lastSecTick >= 1000) {
    lastSecTick = millis();
    if (currentScreen == SCREEN_PRAYER_TIMES || currentScreen == SCREEN_RAMADAN) {
      updateScreenContent();
    }
  }

  delay(5);
}

// =================== Инициализация ===================

void setupBuzzerSafe() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

void setupPowerLatch() {
  pinMode(SYS_EN, OUTPUT);
  digitalWrite(SYS_EN, HIGH);
}

void setupDisplay() {
  if (!gfx->begin()) {
    USBSerial.println("gfx->begin() failed!");
  }
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->fillScreen(COLOR_BG);
}

void setupTouch() {
  for (uint8_t attempt = 0; attempt < 5; attempt++) {
    if (CST816T->begin(400000)) {
      touchReady = true;
      break;
    }
    delay(300);
  }
  if (touchReady) {
    CST816T->IIC_Write_Device_State(
        CST816T->Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
        CST816T->Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);
    CST816T->IIC_Interrupt_Flag = false;
  }
}

void setupRtc() {
  rtcReady = rtc.begin(Wire, IIC_SDA, IIC_SCL);
}

void loadSettings() {
  preferences.begin("barakat", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  city = preferences.getString("city", "Moscow");
  country = preferences.getString("country", "Russia");
  gmtOffsetSec = preferences.getInt("gmt", 10800);
  tasbeehCount = preferences.getUInt("tasbeeh", 0);
  preferences.end();
}

void connectWiFiOrStartAP() {
  if (ssid.length() == 0) {
    startConfigServer();
    return;
  }

  USBSerial.print("Connecting to WiFi: ");
  USBSerial.println(ssid);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    USBSerial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    USBSerial.println("\nWiFi connected!");
    configTime(gmtOffsetSec, 0, "pool.ntp.org", "time.nist.gov");
    fetchPrayerTimes();
  } else {
    USBSerial.println("\nWiFi connection failed, running offline.");
  }
}

void startConfigServer() {
  currentScreen = SCREEN_SETUP_MODE;
  WiFi.softAP("BarakatTime-Setup", "12345678");
  IPAddress IP = WiFi.softAPIP();
  USBSerial.print("AP IP address: ");
  USBSerial.println(IP);

  server.on("/", HTTP_GET, []() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                   "<style>body{font-family:sans-serif;padding:20px;background:#0b1a13;color:#fff;text-align:center;}"
                   "input{padding:12px;margin:8px;width:85%;max-width:300px;background:#162e22;color:#fff;border:1px solid #c5a059;border-radius:8px;}"
                   "input[type=submit]{background:#c5a059;color:#000;border:none;font-weight:bold;cursor:pointer;}"
                   "</style></head><body>"
                   "<h2>BarakatTime Setup</h2>"
                   "<form action='/save' method='POST'>"
                   "WiFi SSID:<br><input type='text' name='ssid'><br>"
                   "WiFi Password:<br><input type='password' name='pass'><br>"
                   "City:<br><input type='text' name='city' value='Moscow'><br>"
                   "Country:<br><input type='text' name='country' value='Russia'><br>"
                   "GMT Offset (sec):<br><input type='text' name='gmt' value='10800'><br><br>"
                   "<input type='submit' value='Save & Restart'>"
                   "</form></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {
    preferences.begin("barakat", false);
    preferences.putString("ssid", server.arg("ssid"));
    preferences.putString("pass", server.arg("pass"));
    preferences.putString("city", server.arg("city"));
    preferences.putString("country", server.arg("country"));
    preferences.putInt("gmt", server.arg("gmt").toInt());
    preferences.end();

    server.send(200, "text/html", "<h3>Saved! Rebooting...</h3>");
    delay(1000);
    ESP.restart();
  });

  server.begin();
  drawSetupScreen();
}

void fetchPrayerTimes() {
  if (WiFi.status() != WL_CONNECTED) return;
  String url = "http://api.aladhan.com/v1/timingsByCity?city=" + city + "&country=" + country + "&method=3";
  HTTPClient http;
  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      JsonObject timings = doc["data"]["timings"];
      prayers.fajr = timings["Fajr"].as<String>();
      prayers.dhuhr = timings["Dhuhr"].as<String>();
      prayers.asr = timings["Asr"].as<String>();
      prayers.maghrib = timings["Maghrib"].as<String>();
      prayers.isha = timings["Isha"].as<String>();
      lastPrayerFetch = millis();
    }
  }
  http.end();
}

// =================== Премиальные экраны ===================

void drawTasbeehScreen() {
  gfx->fillScreen(COLOR_BG);

  int cx = LCD_WIDTH / 2;
  int cy = LCD_HEIGHT / 2 - 6;

  drawCenteredText("TASBEEH", 0, LCD_WIDTH, 26, &FreeSansBold9pt7b, COLOR_TEXT_DIM);

  float progress = (tasbeehCount % 33) / 33.0f;
  drawProgressRing(cx, cy, 88, 76, progress);

  gfx->fillCircle(cx, cy, 66, COLOR_CARD_BG);
  gfx->drawCircle(cx, cy, 66, COLOR_CARD_BORDER_DIM);
  gfx->drawCircle(cx, cy, 67, COLOR_CARD_BORDER);

  char buf[12];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)tasbeehCount);
  drawCenteredText(buf, 0, LCD_WIDTH, cy + 12, &FreeSansBold24pt7b, COLOR_GOLD);

  drawCenteredText(zikrNames[currentZikrIndex], 0, LCD_WIDTH, cy + 46, &FreeSans9pt7b, COLOR_TEXT_DIM);

  drawPageDots(0);
}

void drawPrayerScreen() {
  gfx->fillScreen(COLOR_BG);

  drawCenteredText("BarakatTime", 0, LCD_WIDTH, 32, &FreeSansBold12pt7b, COLOR_GOLD);

  // hero-карточка со следующей молитвой
  drawCard(15, 45, 210, 78, 14);
  drawCenteredText(("Next \xE2\x80\x94 " + prayers.nextPrayerName).c_str(), 15, 210, 72, &FreeSans9pt7b, COLOR_TEXT_DIM);
  drawCenteredText(prayers.timeLeft.c_str(), 15, 210, 108, &FreeSansBold24pt7b, COLOR_GOLD);

  // карточка со списком молитв
  drawCard(15, 135, 210, 108, 14);

  int y = 160;
  auto row = [&](String name, String time, bool active) {
    uint16_t c = active ? COLOR_GOLD : COLOR_TEXT_DIM;
    drawText(name.c_str(), 32, y, &FreeSans9pt7b, c);
    int16_t x1, y1; uint16_t w, h;
    gfx->setFont(&FreeSansBold12pt7b);
    gfx->getTextBounds(time.c_str(), 0, 0, &x1, &y1, &w, &h);
    drawText(time.c_str(), 195 - w, y, &FreeSansBold12pt7b, COLOR_GOLD);
    y += 24;
  };

  row("Fajr", prayers.fajr, prayers.nextPrayerName == "Fajr");
  row("Dhuhr", prayers.dhuhr, prayers.nextPrayerName == "Dhuhr");
  row("Asr", prayers.asr, prayers.nextPrayerName == "Asr");
  row("Maghrib", prayers.maghrib, prayers.nextPrayerName == "Maghrib");

  drawPageDots(1);
}

void drawRamadanScreen() {
  gfx->fillScreen(COLOR_BG);

  drawCard(15, 25, 210, 190, 16);

  drawCrescent(50, 60, 18);

  drawCenteredText("RAMADAN", 15, 210, 100, &FreeSansBold9pt7b, COLOR_GOLD);
  drawCenteredText("127", 15, 210, 150, &FreeSansBold24pt7b, COLOR_TEXT_WHITE);
  drawCenteredText("days left", 15, 210, 170, &FreeSans9pt7b, COLOR_TEXT_DIM);

  gfx->fillRoundRect(30, 182, 180, 26, 8, COLOR_BG_LIGHT);
  drawCenteredText(("City: " + city).c_str(), 30, 180, 200, &FreeSans9pt7b, COLOR_GOLD);

  drawPageDots(2);
}

void drawSetupScreen() {
  gfx->fillScreen(COLOR_BG);

  drawCenteredText("WiFi Setup", 0, LCD_WIDTH, 40, &FreeSansBold12pt7b, COLOR_GOLD);

  drawCard(15, 70, 210, 130, 14);

  drawCenteredText("Connect to:", 15, 210, 100, &FreeSans9pt7b, COLOR_TEXT_DIM);
  drawCenteredText("BarakatTime-Setup", 15, 210, 122, &FreeSansBold9pt7b, COLOR_GOLD);

  drawCenteredText("Open in browser:", 15, 210, 155, &FreeSans9pt7b, COLOR_TEXT_DIM);
  drawCenteredText("192.168.4.1", 15, 210, 180, &FreeSansBold12pt7b, COLOR_GOLD);
}

void drawPageDots(uint8_t activePage) {
  int startX = LCD_WIDTH / 2 - 20;
  int y = LCD_HEIGHT - 18;
  for (int i = 0; i < 3; i++) {
    if (i == activePage) {
      gfx->drawCircle(startX + (i * 20), y, 5, COLOR_GOLD_DIM);
      gfx->fillCircle(startX + (i * 20), y, 3, COLOR_GOLD);
    } else {
      gfx->fillCircle(startX + (i * 20), y, 3, COLOR_TEXT_DIM);
    }
  }
}

void updateScreenContent() {
  if (currentScreen == SCREEN_PRAYER_TIMES) {
    drawPrayerScreen();
  } else if (currentScreen == SCREEN_RAMADAN) {
    drawRamadanScreen();
  }
}

// =================== Тач ===================

void handleTouch() {
  if (!touchReady) return;
  if (!CST816T->IIC_Interrupt_Flag) return;
  CST816T->IIC_Interrupt_Flag = false;

  int32_t fingers = CST816T->IIC_Read_Device_Value(
      CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
  if (fingers <= 0) return;

  if (currentScreen == SCREEN_TASBEEH) {
    tasbeehCount++;
    preferences.begin("barakat", false);
    preferences.putUInt("tasbeeh", tasbeehCount);
    preferences.end();

    drawTasbeehScreen();
  }
}

// =================== Кнопка PWR ===================

void handlePwrButton() {
  bool raw = digitalRead(SYS_OUT);

  if (raw != pwrBtn.lastRaw) {
    lastDebounceTime = millis();
  }
  pwrBtn.lastRaw = raw;

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS && raw != debouncedState) {
    debouncedState = raw;

    if (debouncedState == LOW) {
      pwrBtn.pressStart = millis();
      pwrBtn.longPressFired = false;
    } else {
      if (!pwrBtn.longPressFired) {
        if (pwrBtn.waitingSecondClick &&
            (millis() - pwrBtn.firstClickTime) < DOUBLE_CLICK_WINDOW_MS) {
          pwrBtn.waitingSecondClick = false;
          onDoubleClick();
        } else {
          pwrBtn.waitingSecondClick = true;
          pwrBtn.firstClickTime = millis();
        }
      }
    }
  }

  if (debouncedState == LOW && !pwrBtn.longPressFired &&
      (millis() - pwrBtn.pressStart >= LONG_PRESS_MS)) {
    pwrBtn.longPressFired = true;
    pwrBtn.waitingSecondClick = false;
    onLongPress();
  }

  if (pwrBtn.waitingSecondClick &&
      (millis() - pwrBtn.firstClickTime) >= DOUBLE_CLICK_WINDOW_MS) {
    pwrBtn.waitingSecondClick = false;
    onSingleClick();
  }
}

void onSingleClick() {
  if (currentScreen == SCREEN_SETUP_MODE) return;

  if (currentScreen == SCREEN_TASBEEH) {
    currentScreen = SCREEN_PRAYER_TIMES;
    drawPrayerScreen();
  } else if (currentScreen == SCREEN_PRAYER_TIMES) {
    currentScreen = SCREEN_RAMADAN;
    drawRamadanScreen();
  } else if (currentScreen == SCREEN_RAMADAN) {
    currentScreen = SCREEN_TASBEEH;
    drawTasbeehScreen();
  }
}

void onDoubleClick() {
  if (currentScreen == SCREEN_TASBEEH) {
    currentZikrIndex = (currentZikrIndex + 1) % zikrCount;
    drawTasbeehScreen();
  } else if (currentScreen == SCREEN_PRAYER_TIMES) {
    fetchPrayerTimes();
    drawPrayerScreen();
  }
}

void onLongPress() {
  startConfigServer();
}
