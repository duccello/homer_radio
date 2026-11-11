#include <Arduino.h>
#include <WiFi.h>

#include "AudioFileSourceICYStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

struct WifiCred {
  const char* ssid;
  const char* password;
};

WifiCred WIFI_LIST[] = {
  { "", "" },
};
const int NUM_WIFI_NETWORKS = sizeof(WIFI_LIST) / sizeof(WIFI_LIST[0]);

const char *STREAM_URL = "https://homer-radio-production.up.railway.app/stream";

#define I2S_BCLK  27
#define I2S_LRC   26
#define I2S_DOUT  25

#define BUTTON_PIN  18
#define DEBOUNCE_MS 50

AudioGeneratorMP3        *mp3  = nullptr;
AudioFileSourceICYStream *file = nullptr;
AudioFileSourceBuffer    *buff = nullptr;
AudioOutputI2S           *out  = nullptr;

bool isPlaying        = false;
bool lastReading      = HIGH;
bool buttonState      = HIGH;
unsigned long lastDebounceTime = 0;

void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string) {
  (void) cbData; (void) isUnicode;
  char s1[32], s2[96];
  strncpy_P(s1, type,   sizeof(s1)); s1[sizeof(s1)-1] = 0;
  strncpy_P(s2, string, sizeof(s2)); s2[sizeof(s2)-1] = 0;
  Serial.printf("METADATA '%s' = '%s'\n", s1, s2);
}

void StatusCallback(void *cbData, int code, const char *string) {
  (void) cbData;
  static unsigned long lastPrint = 0;
  unsigned long now = millis();
  if (now - lastPrint < 2000) return;
  lastPrint = now;
  char s1[64];
  strncpy_P(s1, string, sizeof(s1)); s1[sizeof(s1)-1] = 0;
  Serial.printf("STATUS %d: '%s'\n", code, s1);
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  while (true) {
    for (int i = 0; i < NUM_WIFI_NETWORKS; i++) {
      Serial.printf("Trying WiFi '%s'...\n", WIFI_LIST[i].ssid);
      WiFi.begin(WIFI_LIST[i].ssid, WIFI_LIST[i].password);
      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
        delay(300);
        Serial.print(".");
      }
      Serial.println();
      if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false);
        Serial.printf("Connected to '%s', IP: %s, RSSI: %d dBm\n",
          WIFI_LIST[i].ssid,
          WiFi.localIP().toString().c_str(),
          WiFi.RSSI());
        return;
      }
      Serial.printf("Couldn't connect to '%s', trying next...\n", WIFI_LIST[i].ssid);
      WiFi.disconnect();
    }
    Serial.println("None of the known networks were reachable, retrying list...");
  }
}

void stopStream() {
  Serial.println("Stopping stream...");
  if (mp3)  { mp3->stop();  delete mp3;  mp3  = nullptr; }
  if (buff) {               delete buff; buff = nullptr; }
  if (file) {               delete file; file = nullptr; }
  isPlaying = false;
  Serial.printf("Stream stopped. Free heap: %u bytes\n", ESP.getFreeHeap());
}

void startStream() {
  file = new AudioFileSourceICYStream(STREAM_URL);
  file->RegisterMetadataCB(MDCallback, nullptr);
  file->SetReconnect(5, 1000);

  buff = new AudioFileSourceBuffer(file, 64 * 1024);
  buff->RegisterStatusCB(StatusCallback, nullptr);

  mp3 = new AudioGeneratorMP3();
  mp3->RegisterStatusCB(StatusCallback, nullptr);
  mp3->begin(buff, out);

  isPlaying = true;
  Serial.println("Streaming started.");
}

void checkButton() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastReading) {
    lastDebounceTime = millis();
    lastReading = reading;
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        if (isPlaying) {
          Serial.println("Button pressed - turning OFF.");
          stopStream();
        } else {
          Serial.println("Button pressed - turning ON.");
          startStream();
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  audioLogger = &Serial;

  connectToWiFi();

  Serial.printf("Free heap before stream: %u bytes\n", ESP.getFreeHeap());

  out = new AudioOutputI2S();
  out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  out->SetGain(0.8);

  startStream();
}

void loop() {
  checkButton();

  if (isPlaying) {
    if (mp3 && mp3->isRunning()) {
      if (!mp3->loop()) {
        Serial.println("Stream ended - reconnecting...");
        delete mp3;  mp3  = nullptr;
        delete buff; buff = nullptr;
        delete file; file = nullptr;
        startStream();
      }
    }
  }
}
