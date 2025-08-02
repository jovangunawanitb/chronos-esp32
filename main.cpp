#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <ChronosESP32.h>

// Deep Sleep Configuration
#define BUTTON_PIN GPIO_NUM_2
#define SLEEP_TIME_MS 30000  // Sleep after 30 seconds of inactivity
#define DEEP_SLEEP_TIME_US 60000000ULL  // Deep sleep for 60 seconds (60 * 1000000 microseconds)

ChronosESP32 chronos("ESP32-C3");
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 9, /* data=*/ 8);

// Time
String currentTime = "";
String currentDate = "";

// Notification
bool hasNotif = false;
unsigned long notifStart = 0;
String notifText = "";

// Navigation
bool hasNav = false;
Navigation navData;
bool navChanged = false;

// Battery
int batteryLevel = 0;
bool isCharging = false;

// Sleep Management
unsigned long lastActivity = 0;
bool sleepEnabled = true;
volatile bool buttonPressed = false;

// Button interrupt handler
void IRAM_ATTR buttonISR() {
  buttonPressed = true;
  lastActivity = millis();
}

// Check if device should go to sleep
void checkSleep() {
  // Don't sleep if charging, has notifications, or navigation is active
  if (isCharging || hasNotif || (hasNav && navData.active)) {
    lastActivity = millis();
    return;
  }
  
  // Check for inactivity timeout
  if (sleepEnabled && (millis() - lastActivity > SLEEP_TIME_MS)) {
    goToSleep();
  }
}

// Enter deep sleep mode
void goToSleep() {
  Serial.println("Going to deep sleep...");
  
  // Clear display before sleep
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(30, 32, "Sleeping...");
  u8g2.sendBuffer();
  delay(1000);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  
  // Configure wake-up source
  esp_sleep_enable_ext0_wakeup(BUTTON_PIN, 0); // Wake on button press (LOW)
  esp_sleep_enable_timer_wakeup(DEEP_SLEEP_TIME_US); // Wake after timeout
  
  // Enter deep sleep
  esp_deep_sleep_start();
}

// Handle wake-up from deep sleep
void handleWakeUp() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Woke up from button press");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Woke up from timer");
      break;
    default:
      Serial.println("Woke up from other source");
      break;
  }
  
  lastActivity = millis();
}

// draw icon from navData.icon (48x48)
void drawNavIcon(const uint8_t *bitmap) {
  for (int y = 0; y < 48; y++) {
    for (int x = 0; x < 48; x++) {
      int byteIndex = (y * 48 + x) / 8;
      uint8_t bit = 7 - (x % 8);
      if (bitmap[byteIndex] & (1 << bit)) {
        if (y + 16 < 64 && x < 48) {
          u8g2.drawPixel(x, y + 16);
        }
      }
    }
  }
}

// draw battery level bar
void drawBattery(int x, int y, int level) {
  u8g2.drawFrame(x, y, 20, 8);
  u8g2.drawBox(x + 20, y + 2, 2, 4);
  int fill = (level * 18) / 100;
  u8g2.drawBox(x + 1, y + 1, fill, 6);
}

// draw notification screen
void drawNotification() {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 12, "Notifikasi:");
  u8g2.drawStr(0, 28, notifText.c_str());
}

// draw navigation screen
void drawNavigation() {
  if (navData.hasIcon) {
    drawNavIcon(navData.icon);
    u8g2.setFont(u8g2_font_7x14B_tr);
    int y = 10;
    String direction = navData.directions;
    if (u8g2.getStrWidth(direction.c_str()) > (128 - 52)) {
      int split = direction.lastIndexOf(' ', direction.length() / 2);
      if (split != -1) {
        String line1 = direction.substring(0, split);
        String line2 = direction.substring(split + 1);
        u8g2.drawStr(52, y + 8, line1.c_str());
        u8g2.drawStr(52, y + 20, line2.c_str());
      } else {
        u8g2.drawStr(52, y + 12, direction.c_str());
      }
    } else {
      u8g2.drawStr(52, y + 12, direction.c_str());
    }
    u8g2.setFont(u8g2_font_fub14_tr);
    u8g2.drawStr(52, y + 40, navData.distance.c_str());
  }
}

// draw clock screen with sleep indicator
void drawClock() {
  u8g2.setFont(u8g2_font_logisoso32_tr);
  u8g2.drawStr(0, 42, currentTime.c_str());

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(90, 10, currentDate.c_str());

  drawBattery(100, 52, batteryLevel);
  
  // Show sleep countdown if approaching sleep time
  if (!isCharging && !hasNotif && (!hasNav || !navData.active)) {
    unsigned long timeToSleep = SLEEP_TIME_MS - (millis() - lastActivity);
    if (timeToSleep < 10000) { // Show countdown in last 10 seconds
      u8g2.setFont(u8g2_font_6x10_tr);
      String sleepMsg = "Sleep in " + String(timeToSleep / 1000) + "s";
      u8g2.drawStr(0, 64, sleepMsg.c_str());
    }
  }
}

// master draw
void drawScreen() {
  u8g2.clearBuffer();

  if (hasNotif && millis() - notifStart < 1500) {
    drawNotification();
  } else if (hasNav && navData.active) {
    drawNavigation();
  } else {
    drawClock();
  }

  u8g2.sendBuffer();
}

// CALLBACKS
void onConnection(bool state) {
  lastActivity = millis(); // Reset sleep timer on connection changes
}

void onNotification(Notification notif) {
  notifText = notif.title + ": " + notif.message;
  if (notifText.length() > 20) {
    notifText = notifText.substring(0, 20) + "...";
  }
  notifStart = millis();
  hasNotif = true;
  lastActivity = millis(); // Reset sleep timer on notification
}

void onConfig(Config type, uint32_t a, uint32_t b) {
  if (type == CF_TIME) {
    currentTime = chronos.getHourZ() + chronos.getTime(":%M");
    currentDate = chronos.getDate();
  } else if (type == CF_PBAT) {
    batteryLevel = b;
    isCharging = (a == 1);
    lastActivity = millis(); // Reset sleep timer on battery updates
  } else if (type == CF_NAV_DATA) {
    navData = chronos.getNavigation();
    hasNav = navData.active;
    if (navData.active) {
      lastActivity = millis(); // Reset sleep timer when navigation becomes active
    }
  } else if (type == CF_NAV_ICON) {
    Navigation temp = chronos.getNavigation();
    memcpy(navData.icon, temp.icon, sizeof(navData.icon));
    navData.hasIcon = true;
  }
}

void setup() {
  Serial.begin(115200);
  
  // Handle wake-up from deep sleep
  handleWakeUp();
  
  // Configure button pin with internal pullup
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);
  
  // Initialize display
  u8g2.begin();
  
  // Show wake-up message briefly
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(30, 32, "Starting...");
  u8g2.sendBuffer();
  delay(1000);

  // Initialize ChronosESP32
  chronos.setNotificationCallback(onNotification);
  chronos.setConfigurationCallback(onConfig);
  chronos.setConnectionCallback(onConnection);

  chronos.begin();
  chronos.set24Hour(false);
  
  // Initialize activity timer
  lastActivity = millis();
  
  Serial.println("ESP32-C3 started with deep sleep enabled");
}

void loop() {
  chronos.loop();

  // Handle button press
  if (buttonPressed) {
    buttonPressed = false;
    lastActivity = millis();
    Serial.println("Button pressed - activity reset");
  }

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 1000) {
    lastUpdate = millis();

    currentTime = chronos.getHourZ() + chronos.getTime(":%M");
    currentDate = chronos.getDate();
    drawScreen();
  }

  if (hasNotif && millis() - notifStart > 1500) {
    hasNotif = false;
  }
  
  // Check if device should go to sleep
  checkSleep();
  
  // Small delay to prevent excessive CPU usage
  delay(10);
}