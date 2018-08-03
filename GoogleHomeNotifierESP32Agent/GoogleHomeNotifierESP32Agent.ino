/*
  GoogleHomeNotifierESP32Agent.ino  (for ESP32)

  Copyright (c) 2018 KLab Inc.

  Released under the MIT license
  https://opensource.org/licenses/mit-license.php

  Google Home へのキャスト用エージェント.
  所定のトピックの MQTT メッセージをトリガーに処理を行う.

  horihiro 様 による「esp8266-google-home-notifier」を
  カスタマイズしたライブラリを使用.

    オリジナル : https://github.com/horihiro/esp8266-google-home-notifier 
  手を加えた版 : https://github.com/mkttanabe/esp8266-google-home-notifier

  MQTT ブローカーに Beebotte を使用.
  所定のトピックのメッセージの data キーの値を参照し以下を行う.

   A. data 値の先頭文字が '*' であれば音声合成用のテキストとみなして処理
   B. data 値の先頭文字が '*' でなければ MP3 ファイル名とみなし
      所定の Web サーバ の URL に編集して再生

 */

#include <WiFiClientSecure.h>
#include <esp8266-google-home-notifier.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "MqttBeebotteCom_pem.h"

//------- ユーザ定義 ------------------

// Google Home デバイスの IP アドレス, デバイス名

// 有効にすれば指定 IP アドレスを直接使用
// 無効にすれば指定デバイス名で mDNS 照会
#define USE_GH_IPADRESS

IPAddress myGoogleHomeIPAddress(192,168,0,121);
#define myGoogleHomeDeviceName "room01"

// WiFi アクセスポイント
#define ssid "ssid"
#define password "pass"

// Beebotte 情報
#define mqtt_host  "mqtt.beebotte.com"
#define mqtt_port  8883
#define mqtt_topic "test01/msg"
#define mqtt_pass  "token:" "token_************"

// MP3 データを配置する Web サーバとデータパス
#define dataServer "192.168.0.126"
#define dataServerPort 80
#define MP3DataFmt "http://" dataServer "/sound/%s.mp3"

//-------------------------------------

WiFiClientSecure wifiClientSecure;
PubSubClient pubsubClient(wifiClientSecure);
GoogleHomeNotifier ghn;

void dbg(const char *format, ...) {
  //return;
  char b[256];
  va_list va;
  va_start(va, format);
  vsnprintf(b, sizeof(b), format, va);
  va_end(va);
  Serial.print(b);
}

// 再生
void doCast(const char *data) {
  bool sts;
  char url[128];
  if (WiFi.status() != WL_CONNECTED || !data) {
    return;
  }
  dbg("doCast: %s:%u\r\n",
    ghn.getIPAddress().toString().c_str(), ghn.getPort());

  if (data[0] == '*') { // TTS
    sts = ghn.notify(&data[1]);
  } else { // MP3
    snprintf(url, sizeof(url), MP3DataFmt, data);
    dbg("doCast: start [%s]\r\n", url);
    sts = ghn.play(url);
  }
  if (!sts) {
    dbg("%s\r\n", ghn.getLastError());
  }
  dbg("doCast: done\r\n");
}

// MQTT イベント通知
static void callbackMQTT(char *topicName, byte *msg, unsigned int msgLength) {
  dbg("callbackMQTT: topicName=[%s], msg=[%s]\r\n", topicName, (char*)msg);
  if (!strcmp(topicName, mqtt_topic) == 0) {
    return;
  }
  // data キーの値が mp3 ファイル名 or 読み上げ用テキスト
  // {"data":"file01","ispublic":true,"ts":1530128353234}
  // {"data":"*こんにちは","ispublic":true,"ts":1530128353234}
  StaticJsonDocument<128> doc;
  DeserializationError jsonErr = deserializeJson(doc, msg);
  if (jsonErr) {
    dbg("%s\r\n", jsonErr.c_str());
  }
  JsonObject& root = doc.as<JsonObject>();
  const char *data = root["data"];
  dbg("callbackMQTT: value=[%s]\r\n", data);
  doCast(data);
}

// WiFi 接続
void connectWiFi() {
  int count = 0; 
  dbg("connectWiFi: connecting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    dbg(".");
    if (count++ > 60) { // give up
      dbg("restart...\r\n");
      ESP.restart();
    }
  }
  dbg("\r\nWiFi connected\r\n");
  dbg("connectWiFi: IP address: %s\r\n", WiFi.localIP().toString().c_str());
}

// Google Home の IP アドレスを ghn へセット
void setAddress() {
#ifdef USE_GH_IPADRESS
  dbg("setAddress: using IP Address\r\n");
  if (ghn.ip(addr, "ja") != true) {
#else
  dbg("setAddress: calling mDNS..\r\n");
  if (ghn.device(myGoogleHomeDeviceName, "ja") != true) {
#endif
    Serial.println(String("doCast: ") + ghn.getLastError());
  }
  dbg("doCast: addr:port=[%s:%u]\r\n", 
    ghn.getIPAddress().toString().c_str(), ghn.getPort());
}

void setup() {
  Serial.begin(115200);
  // for Beebotte
  wifiClientSecure.setCACert(certMqttBeebotteCom);
  pubsubClient.setServer(mqtt_host, mqtt_port);
  pubsubClient.setCallback(callbackMQTT);
}

void loop() {
  wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus != WL_CONNECTED) {
    dbg("loop: WiFi.status=%d\r\n", wifiStatus);
    connectWiFi();
    setAddress();
  } else {
    if (!pubsubClient.connected()) {
      // Beebotte では ID はランダムで可
      char id[8];
      snprintf(id, sizeof(id), "%lu", millis());
      dbg("loop: connecting to %s:%d, id=[%s]\r\n", mqtt_host, mqtt_port, id);
      if (pubsubClient.connect(id, mqtt_pass, NULL)) {
        dbg("MQTT connected\r\n");
      }
      if (pubsubClient.connected()) {
        bool sts = pubsubClient.subscribe(mqtt_topic);
        dbg("loop: MQTT subscribe [%s] => %d\r\n", mqtt_topic, sts);
        if (sts) {
          doCast("*エージェントの準備ができました");
        }
      }
    }
    pubsubClient.loop();
  }
}

