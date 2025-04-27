
//------------------------------------------------------------------------LIBRARIES--------------------------------------------------------------------------------------//

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Servo.h>
#include <RP2040_RTC.h>
#include <EEPROM.h>

//------------------------------------------------------------------------VARIABLES--------------------------------------------------------------------------------------//

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* botToken = "YOUR_BOT_TOKEN";
const char* chatID = "YOUR_CHAT_ID";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); // 19800 sec offset for IST

WiFiClientSecure client;

Servo servo;
#define SERVO_PIN 5 // GP5 (physical pin 7)

String previousMessage;
unsigned long previousMillis = 0;
int scheduleTime = -1; // scheduled hour
int scheduledServingSize = 0;
bool scheduleToggle = false; // paused or running


int scheduleMinute = 0; // store minute

//-----------------------------------------------------------------------FUNCTIONS--------------------------------------------------------------------------------------//


void syncTimeFromNTP() {
  Serial.println("Syncing time from NTP...");
  timeClient.begin();
  while (!timeClient.update()) {
    timeClient.forceUpdate();
    delay(500);
  }

  struct tm timeinfo;
  time_t epochTime = timeClient.getEpochTime();
  gmtime_r(&epochTime, &timeinfo);

  rtc_init();
  datetime_t t = {
    .year = (uint16_t)(timeinfo.tm_year + 1900),
    .month = (uint8_t)(timeinfo.tm_mon + 1),
    .day = (uint8_t)(timeinfo.tm_mday),
    .dotw = (uint8_t)(timeinfo.tm_wday),
    .hour = (uint8_t)(timeinfo.tm_hour),
    .min = (uint8_t)(timeinfo.tm_min),
    .sec = (uint8_t)(timeinfo.tm_sec)
  };
  rtc_set_datetime(&t);

  Serial.println("Time synced!");
}

void checkTelegram() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(botToken) + "/getUpdates?limit=1&offset=-1";

  Serial.println("\n🔄 Fetching Telegram updates...");
  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    String payload = http.getString();

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      String text = doc["result"][0]["message"]["text"];
      String senderID = doc["result"][0]["message"]["chat"]["id"];

      Serial.println("Message Received: " + text);
      
      if (senderID == chatID && (text == "Help" || text == "help") && (previousMessage != text)) {
        sendTelegramMessage("1. /feed small - Dispenses a small portion of food.");
        sendTelegramMessage("2. /feed medium - Dispenses a medium portion of food.");
        sendTelegramMessage("3. /feed large - Dispenses a large portion of food.");
        sendTelegramMessage("4. /schedule (size) (24-hour format time) - Set a schedule for automatic feeding:");
        sendTelegramMessage("5. /schedule pause - Pause the scheduled feeding.");
        sendTelegramMessage("6. /schedule resume - Resume the scheduled feeding.");
        sendTelegramMessage("7. /schedule status - View the current schedule and its status (active or paused).");
        sendTelegramMessage("8. /help - Display this help message.");
        sendTelegramMessage("Don't forget to text 'over' before a repeated command");
        
        previousMessage = text;
      }


      if (senderID == chatID && text.startsWith("/feed") && (previousMessage != text)) {
        if (text == "/feed small") singleDispense(1);
        if (text == "/feed medium") singleDispense(2);
        if (text == "/feed large") singleDispense(3);
        Serial.println("✅ Valid Command Received!");
        previousMessage = text;
      }

      if (senderID == chatID && text.startsWith("/schedule") && (previousMessage != text)) {
        if (text.startsWith("/schedule small ")) {
          int hour = 0, minute = 0;
          if (sscanf(text.c_str(), "/schedule small %d:%d", &hour, &minute) == 2) {
            scheduleTime = hour;
            scheduleMinute = minute;
            scheduledServingSize = 1;
            scheduleToggle = true;
            sendTelegramMessage("Schedule set! Text '/schedule status' to see all your schedules.");
            saveSchedule();
          } else {
            sendTelegramMessage("❌ Invalid time format. Use HH:MM format.");
          }
        } else if (text.startsWith("/schedule medium ")) {
          int hour = 0, minute = 0;
          if (sscanf(text.c_str(), "/schedule medium %d:%d", &hour, &minute) == 2) {
            scheduleTime = hour;
            scheduleMinute = minute;
            scheduledServingSize = 2;
            scheduleToggle = true;
            sendTelegramMessage("Schedule set! Text '/schedule status' to see all your schedules.");
            saveSchedule();
          } else {
            sendTelegramMessage("❌ Invalid time format. Use HH:MM format.");
          }
        } else if (text.startsWith("/schedule large ")) {
          int hour = 0, minute = 0;
          if (sscanf(text.c_str(), "/schedule large %d:%d", &hour, &minute) == 2) {
            scheduleTime = hour;
            scheduleMinute = minute;
            scheduledServingSize = 3;
            scheduleToggle = true;
            sendTelegramMessage("Schedule set! Text '/schedule status' to see all your schedules.");
            saveSchedule();
          } else {
            sendTelegramMessage("❌ Invalid time format. Use HH:MM format.");
          }
        } else if (text == "/schedule pause") {
          scheduleToggle = false;
          sendTelegramMessage("Schedule paused.");
        } else if (text == "/schedule resume") {
          scheduleToggle = true;
          sendTelegramMessage("Schedule resumed.");
        } else if (text == "/schedule status") {
          String sizeStr;
          if (scheduledServingSize == 1) sizeStr = "small";
          if (scheduledServingSize == 2) sizeStr = "medium";
          if (scheduledServingSize == 3) sizeStr = "large";

          String statusStr = scheduleToggle ? "active" : "paused";
          sendTelegramMessage(sizeStr + " serving at " + String(scheduleTime) + ":" + String(scheduleMinute) + " everyday.");
          sendTelegramMessage("Status: " + statusStr);
        }
        previousMessage = text;
      } else {
        Serial.println("❌ Ignored: Message not recognized or from unknown sender.");
        previousMessage = text;
      }
    } else {
      Serial.println("❌ JSON Parse Error: " + String(error.c_str()));
    }
  } else {
    Serial.println("❌ HTTP GET Failed: " + String(http.errorToString(httpCode).c_str()));
  }

  http.end();
}

void sendTelegramMessage(String message) {
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(botToken) + "/sendMessage?chat_id=" + String(chatID) + "&text=" + message;

  Serial.println("\n📤 Sending message to Telegram: " + message);
  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    Serial.println("✅ Telegram Message Sent Successfully!");
  } else {
    Serial.println("❌ Failed to Send Message: " + String(http.errorToString(httpCode).c_str()));
  }

  http.end();
}

void singleDispense(int size) {
  Serial.println("✅ Dispensing food...");
  servo.write(180);
  delay(size * 1000);
  servo.write(0);
  delay(size * 1000);
  sendTelegramMessage("🍽 Food dispensed!");
}

void checkSchedule() {
  if (!scheduleToggle) return;

  datetime_t now;
  rtc_get_datetime(&now);

  if (now.hour == scheduleTime && now.min == scheduleMinute && now.sec == 0) {
    Serial.println("🕒 Scheduled feed triggered!");
    singleDispense(scheduledServingSize);
    delay(1000); // Avoid retriggering within the same second
  }
}

void saveSchedule() {
  EEPROM.write(0, scheduleTime); // store at address 0
  EEPROM.write(1, scheduleMinute); // store at address 1
  EEPROM.write(2, scheduledServingSize); // store at address 2
  EEPROM.write(3, scheduleToggle); // store at address 3 (true/false)

  EEPROM.commit(); // write changes to flash
  Serial.println("💾 Schedule saved to EEPROM!");
}

void loadSchedule() {
  scheduleTime = EEPROM.read(0);
  scheduleMinute = EEPROM.read(1);
  scheduledServingSize = EEPROM.read(2);
  scheduleToggle = EEPROM.read(3);

  Serial.println("📥 Schedule loaded from EEPROM:");
  Serial.print("  Hour: "); Serial.println(scheduleTime);
  Serial.print("  Minute: "); Serial.println(scheduleMinute);
  Serial.print("  Size: "); Serial.println(scheduledServingSize);
  Serial.print("  Toggle: "); Serial.println(scheduleToggle ? "Active" : "Paused");
}


//-----------------------------------------------------------------------SETUP AND LOOP---------------------------------------------------------------------------------//
void setup() {
  Serial.begin(115200);

  EEPROM.begin(512);
  loadSchedule();

  servo.attach(SERVO_PIN);
  servo.write(0);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");

  client.setInsecure(); // Skip SSL cert validation

  syncTimeFromNTP();
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= 5000) {
    checkTelegram();
    previousMillis = currentMillis;
  }
  checkSchedule();
}
