#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <AceButton.h>

using namespace ace_button;

// Wi-Fi and Telegram Credentials
const char* ssid = "SLT_FIBRE";  
const char* password = "20002001";  
const char* botToken = "8189177967:AAG3exlzaKTdxYcqHFFVoqREBiHfpZELZHA"; 
const long chatID = 1483198598;  

WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

// Pin Definitions
const int relayPin = 23;     // Relay connected to GPIO23
const int switchPin = 19;    // Button connected to GPIO18
#define soil_moisture_pin 32 
#define LED_PIN 2
#define RED_PIN   25
#define GREEN_PIN 26
#define BLUE_PIN  27


bool relayState = false;     // Track relay state

unsigned long relayOnTime = 0;
unsigned long lastAutoTrigger = 0;
unsigned long last15MinMessage = 0;

const unsigned long ONE_HOUR = 60000;
const unsigned long TWENTY_THREE_HOURS = 60000;  // 23h = 23 * 60 * 60 * 1000
const unsigned long FIFTEEN_MINUTES = 15000;

bool isAutoRelayOn = false;
bool isManualRelay = false;


// AceButton Setup
ButtonConfig buttonConfig;
AceButton button(&buttonConfig);

void handleNewMessages(int numNewMessages);
void buttonHandler(AceButton* button, uint8_t eventType, uint8_t buttonState);

void setup() {
  Serial.begin(115200);

  // Pin Setup
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW); // Turn off relay (active LOW)
  pinMode(switchPin, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);


  // WiFi Connection
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
 // digitalWrite(LED_PIN, HIGH);

  client.setInsecure();  // Skip certificate check

  // Button Init
  buttonConfig.setEventHandler(buttonHandler);
  button.init(switchPin);
}

unsigned long lastSoilCheck = 0;
const unsigned long soilCheckInterval = 2000;  // Check every 2 seconds

void loop() {
  button.check(); // Check button input

  int newMessages = bot.getUpdates(bot.last_message_received + 1);
  if (newMessages > 0) {
    handleNewMessages(newMessages);
  }
  

  unsigned long now = millis();

  // Soil check every 2 seconds
  static unsigned long lastSoilCheck = 0;
  if (now - lastSoilCheck >= 2000) {
    lastSoilCheck = now;
    updateLEDState();
  }

  // === Auto Relay Scheduler ===
  if ((now - lastAutoTrigger >= ONE_HOUR + TWENTY_THREE_HOURS) || lastAutoTrigger == 0) {
    lastAutoTrigger = now;

    int moisture = analogRead(soil_moisture_pin);
    if (moisture >= 1990) {
      relayState = true;
      isAutoRelayOn = true;
      isManualRelay = false;

      digitalWrite(relayPin, HIGH);
      updateLEDState();
      bot.sendMessage(String(chatID), "Auto Relay ON for irrigation.", "");
      relayOnTime = now;
      last15MinMessage = now;  // start 15 min timer
    } else {
      String msg = "Relay tried to turn ON, but soil moisture is sufficient.\nADC: " + String(moisture);
      bot.sendMessage(String(chatID), msg, "");
    }
  }

  // === Auto Relay OFF After 1 Hour ===
  if (isAutoRelayOn && now - relayOnTime >= ONE_HOUR) {
    relayState = false;
    isAutoRelayOn = false;
    digitalWrite(relayPin, LOW);
    updateLEDState();
    bot.sendMessage(String(chatID), "Auto Relay OFF after 1 hour.", "");
  }

  // === Send Message Every 15 Minutes if Relay is ON ===
  if ((relayState) && (now - last15MinMessage >= FIFTEEN_MINUTES)) {
    last15MinMessage = now;
    bot.sendMessage(String(chatID), "Relay is ON (auto/manual) - running irrigation.", "");
  }


  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);  // ON = connected
  } else {
    digitalWrite(LED_PIN, LOW);   // OFF = not connected
  }

  delay(100);
}

void setRGB(bool r, bool g, bool b) {
  digitalWrite(RED_PIN,   r ? HIGH : LOW);
  digitalWrite(GREEN_PIN, g ? HIGH : LOW);
  digitalWrite(BLUE_PIN,  b ? HIGH : LOW);
}

void updateLEDState() {
  if (relayState) {
    // Relay is ON → Green
    setRGB(false, true, false);
  } else {
    // Relay is OFF → Use soil moisture
    int value = analogRead(soil_moisture_pin);
    if (value < 1990) {
      setRGB(false, false, true);  // Blue
    } else {
      setRGB(true, false, false);  // Red
    }
  }

}


void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    if (chat_id.toInt() != chatID) continue;

    String text = bot.messages[i].text;
    Serial.println("Command: " + text);

    if (text == "/relay_on") {
      relayState = true;
      isManualRelay = true;
      isAutoRelayOn = false;
      digitalWrite(relayPin, HIGH);
      bot.sendMessage(chat_id, "Relay is ON", "");
      updateLEDState();
    } else if (text == "/relay_off") {
      relayState = false;
      isManualRelay = false;
      isAutoRelayOn = false;
      digitalWrite(relayPin, LOW);
      bot.sendMessage(chat_id, "Relay is OFF", "");
      updateLEDState();
    } else if (text == "/relay_state") {
      String stateMsg = String("Relay is ") + (relayState ? "ON" : "OFF");
      bot.sendMessage(chat_id, stateMsg, "");
    } else if (text == "/moisture") {
      int value = analogRead(soil_moisture_pin);
      int percent = map(value, 0, 4095, 0, 100);  // You can reverse if sensor behaves oppositely
      String msg = "Soil Moisture: " + String(percent) + "%\nRaw ADC Value: " + String(value);
      bot.sendMessage(chat_id, msg, "");
      updateLEDState();

    } else {
      bot.sendMessage(chat_id, "Valid commands:\n/relay_on\n/relay_off\n/relay_state\n/moisture", "");
    }
  }
}

// Button Event Handler
void buttonHandler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  if (eventType == AceButton::kEventPressed) {
    relayState = !relayState; // Toggle state
    isManualRelay = relayState;  // Manual ON/OFF toggle
    isAutoRelayOn = false;       // Override auto mode

    // Set the relay pin (active LOW)
    digitalWrite(relayPin, relayState ? HIGH : LOW);

    // Print to Serial
    Serial.println(relayState ? "Button Pressed - Relay ON" : "Button Pressed - Relay OFF");

    // Send message to Telegram bot
    String msg = relayState ? "Relay is ON - button pressed" : "Relay is OFF - button pressed";
    bot.sendMessage(String(chatID), msg, "");
    updateLEDState();
  }
}

