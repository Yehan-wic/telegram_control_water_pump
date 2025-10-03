#include <Wire.h>
#include "RTClib.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// ====== WiFi & Telegram Config ======
const char* ssid     = "Your Wifi Name";
const char* password = "Your Wifi Password";
#define BOT_TOKEN    "Your Telegram Bot Token"
#define CHAT_ID      "Your Telegram Chat ID"

// ====== Telegram Bot ======
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// ====== RTC Config ======
RTC_DS3231 rtc;

// ====== Relay Config ======
#define RELAY_PIN  23   // Relay connected to GPIO23
bool relayState = false;

// ====== Wakeup Pin ======
#define RTC_INT_PIN 13   // DS3231 INT pin connected here

// ====== Telegram Polling ======
unsigned long lastCheckTime = 0;
const long checkInterval = 2000; // check every 2 sec

// ====== Time Window ======
const int START_HOUR = 10;        // irrigation start
const int STOP_HOUR  = 15;        // irrigation stop
const int MORNING_SYNC_HOUR = 8;  // morning RTC sync

// ====== NTP Config ======
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);
const long gmtOffset = 5 * 3600 + 30 * 60; // Sri Lanka = +5h30m
bool rtcSynced = false;

// ====== WiFi Retry Config ======
#define WIFI_RETRY_INTERVAL 120000       // 120s = 2 minutes
#define MORNING_SYNC_TIMEOUT (5 * 60 * 1000) // 5 minutes max retry

// ====== Buffered Messages Section ======
#define MAX_BUFFERED_MSGS 10
String msgBuffer[MAX_BUFFERED_MSGS];
int msgCount = 0;

void bufferMessage(const String &msg) {
  if (msgCount < MAX_BUFFERED_MSGS) {
    msgBuffer[msgCount++] = msg;
    Serial.println("📩 Message buffered: " + msg);
  } else {
    Serial.println("⚠️ Buffer full, dropping message: " + msg);
  }
}

void flushBufferedMessages() {
  if (WiFi.status() != WL_CONNECTED) return;
  for (int i = 0; i < msgCount; i++) {
    if (bot.sendMessage(CHAT_ID, msgBuffer[i], "")) {
      Serial.println("✅ Sent buffered message: " + msgBuffer[i]);
    } else {
      Serial.println("⚠️ Failed to send buffered message, keeping in buffer.");
      return; // stop flushing if send fails
    }
  }
  msgCount = 0; // clear buffer after success
}

void sendMessageSafe(const String &msg) {
  if (WiFi.status() == WL_CONNECTED) {
    if (bot.sendMessage(CHAT_ID, msg, "")) {
      Serial.println("✅ Sent: " + msg);
    } else {
      Serial.println("⚠️ Send failed, buffering message.");
      bufferMessage(msg);
    }
  } else {
    bufferMessage(msg);
  }
}

// ====== Setup ======
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // ====== Init I2C with custom pins ======
  Wire.begin(27, 19); // SDA = GPIO27, SCL = GPIO19
  Serial.println("I2C started on SDA=GPIO27, SCL=GPIO19");

  // Init RTC
  if (!rtc.begin(&Wire)) {
    Serial.println("Couldn't find RTC");
    while (1);
  }
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // fallback compile time
  }

  // Clear alarms
  rtc.disableAlarm(1);
  rtc.disableAlarm(2);
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);

  // Wake reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("🔔 Woke up from RTC alarm");
  } else {
    Serial.println("Normal boot");
  }

  DateTime now = rtc.now();
  Serial.printf("⏱ Current RTC time: %02d:%02d:%02d\n", now.hour(), now.minute(), now.second());

  // ====== Morning Sync at 8 AM ======
  if (now.hour() == MORNING_SYNC_HOUR) {
    Serial.println("🌅 Morning 8:00 AM wake: trying NTP sync...");
    unsigned long start = millis();
    bool synced = false;

    while (millis() - start < MORNING_SYNC_TIMEOUT) {
      if (connectWiFi()) {
        if (syncRTCwithNTP()) {
          Serial.println("🎯 RTC synced successfully at 8:00 AM.");
          sendMessageSafe("⏱ RTC corrected with NTP at 8:00 AM.");
          synced = true;
          break;
        }
      }
      Serial.println("⏳ Retry in 30s...");
      delay(30000);
    }

    if (!synced) {
      Serial.println("⚠️ Failed to sync RTC at 8:00 AM within 5 minutes.");
      sendMessageSafe("⚠️ Failed to sync RTC at 8:00 AM.");
    }

    // Always schedule next wake at 10:00 AM
    setRTCAlarmFor(START_HOUR, 0);
    goToDeepSleep();
  }

  // ====== Main Irrigation Logic ======
  if (now.hour() >= START_HOUR && now.hour() < STOP_HOUR) {
    // ✅ Start irrigation immediately
    if (!relayState) {
      relayState = true;
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("💧 Irrigation started (failsafe check).");
      sendMessageSafe("💧 Irrigation started (failsafe recovery).");
    }

    // 🌐 Try NTP sync after relay is ON
    syncRTCwithNTP_retry();

    // Stay awake until STOP_HOUR
    stayAwakeUntilStop();
    relayState = false;
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("⏹️ Irrigation stopped at 3:00 PM.");
    sendMessageSafe("⏹️ Irrigation stopped at 3:00 PM.");

    setRTCAlarmFor(MORNING_SYNC_HOUR, 0); // Next wakeup tomorrow 8 AM for sync
    goToDeepSleep();
  }
  else if (now.hour() >= STOP_HOUR) {
    relayState = false;
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("⏹️ Past irrigation window. Sleeping until 8:00 AM tomorrow.");
    setRTCAlarmFor(MORNING_SYNC_HOUR, 0);
    goToDeepSleep();
  }
  else {
    relayState = false;
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("💤 Before irrigation window. Sleeping until 8:00 AM.");
    setRTCAlarmFor(MORNING_SYNC_HOUR, 0);
    goToDeepSleep();
  }
}

void loop() {
  flushBufferedMessages(); // retry unsent messages
  delay(5000);             // check every 5s
}

// ====== Sync RTC with NTP (single attempt) ======
bool syncRTCwithNTP() {
  timeClient.begin();
  if (!timeClient.update()) {
    timeClient.forceUpdate();
  }

  unsigned long epochTime = timeClient.getEpochTime();
  if (epochTime < 100000) return false;

  epochTime += gmtOffset;
  rtc.adjust(DateTime(epochTime));
  rtcSynced = true;

  DateTime now = rtc.now();
  Serial.printf("✅ RTC synced to NTP (local time): %02d:%02d:%02d\n",
                now.hour(), now.minute(), now.second());
  return true;
}

void syncRTCwithNTP_retry() {
  Serial.println("🌐 Trying to sync RTC with NTP...");
  int attempts = 0;
  while (attempts < 10) {
    if (connectWiFi()) {
      if (syncRTCwithNTP()) {
        Serial.println("🎯 RTC successfully synced with NTP!");
        return;
      }
    }
    attempts++;
    Serial.println("⏳ NTP sync failed, retrying in 30s...");
    delay(30000);
  }
  Serial.println("⚠️ RTC sync failed after retries.");
}

// ====== Set RTC Alarm ======
void setRTCAlarmFor(int targetHour, int targetMinute) {
  DateTime now = rtc.now();
  DateTime target(now.year(), now.month(), now.day(), targetHour, targetMinute, 0);
  if (now >= target) {
    target = target + TimeSpan(1, 0, 0, 0);
  }
  rtc.setAlarm2(target, DS3231_A2_Hour);
  rtc.clearAlarm(2);
  rtc.writeSqwPinMode(DS3231_OFF);
  Serial.printf("⏰ Alarm set for %02d:%02d\n", target.hour(), target.minute());
}

// ====== Deep Sleep ======
void goToDeepSleep() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  pinMode(RTC_INT_PIN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)RTC_INT_PIN, 0);
  Serial.println("💤 Going to deep sleep...");
  esp_deep_sleep_start();
}

// ====== Stay Awake Until STOP_HOUR ======
void stayAwakeUntilStop() {
  Serial.println("📡 Staying awake until stop time...");
  while (true) {
    DateTime now = rtc.now();
    if (now.hour() >= STOP_HOUR) break;

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️ WiFi lost! Retrying...");
      if (connectWiFi()) {
        Serial.println("✅ WiFi reconnected.");
        flushBufferedMessages();
        syncRTCwithNTP();
      }
    }

    if (WiFi.status() == WL_CONNECTED && millis() - lastCheckTime > checkInterval) {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      while (numNewMessages) {
        handleNewMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
      lastCheckTime = millis();
    }

    flushBufferedMessages();
    delay(WIFI_RETRY_INTERVAL); // retry every 120s
  }
  Serial.println("⌛ Control window closed.");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// ====== Handle Telegram Commands ======
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) continue;

    String text = bot.messages[i].text;
    if (text == "/on") {
      relayState = true;
      digitalWrite(RELAY_PIN, HIGH);
      sendMessageSafe("✅ Relay turned ON by command.");
    }
    else if (text == "/off") {
      relayState = false;
      digitalWrite(RELAY_PIN, LOW);
      sendMessageSafe("❌ Relay turned OFF by command.");
    }
    else if (text == "/status") {
      String status = relayState ? "ON 💡" : "OFF ❌";
      DateTime now = rtc.now();
      char buf[40];
      sprintf(buf, "📊 Relay: %s | Time: %02d:%02d:%02d",
              status.c_str(), now.hour(), now.minute(), now.second());
      sendMessageSafe(buf);
    }
    else if (text == "/time") {
      DateTime now = rtc.now();
      char buf[30];
      sprintf(buf, "⏱ Current time: %02d:%02d:%02d", now.hour(), now.minute(), now.second());
      sendMessageSafe(buf);
    }
    else {
      sendMessageSafe("⚡ Commands: /on /off /status /time (10 AM–3 PM only)");
    }
  }
}

// ====== WiFi connect helper ======
bool connectWiFi() {
  WiFi.begin(ssid, password);
  client.setInsecure();
  Serial.print("Connecting to WiFi");
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(1000);
    Serial.print(".");
    retries++;
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}


