/*
  GoogleHomeNotifierESP32AgentEx.ino  (for ESP32)

  Copyright (c) 2018 KLab Inc.

  Released under the MIT license
  https://opensource.org/licenses/mit-license.php

  Google Home へのキャスト用エージェント拡張版.
  所定のトピックの MQTT メッセージをトリガーに処理を行う.

  horihiro 様 による esp8266-google-home-notifier を
  カスタマイズしたライブラリを使用

    オリジナル : https://github.com/horihiro/esp8266-google-home-notifier 
  手を加えた版 : https://github.com/mkttanabe/esp8266-google-home-notifier  
  
  MQTT ブローカーに Beebotte を使用.
  所定のトピックのメッセージの data キーの値を参照し以下を行う.

   A. data 値の先頭文字が '*' であれば音声合成用のテキストとみなして処理
   B. data 値の先頭文字が '*' または '@' でなければ MP3 ファイル名とみなし
      所定の Web サーバ の URL に編集して再生
   C. data 値の先頭文字が '@'であれば MP3 データ連続再生用のリスト名とみなし
      所定の Web サーバから当該リストを読み込んで順次再生
      o 下記の指定でシャッフル再生
        {"data":"@list01,1"} , {"data":"@list01,ランダム"}
      o 上記以外ならリストの順に再生（ {"data":"@list01"} と等価）
      o 終端アイテムの再生が終わったら先頭から繰り返す
      o リストは 1行に 1件ずつ MP3 データ名を記述
        "dir01/data01" の要領
      o リスト中の '#'から当該行末尾まではコメントとして扱われる

  なお IP アドレス直指定ではなくデバイス名でのアドレス解決を指定の場合に,
  mDNS 照会コスト軽減のため一度取得したアドレスを ESP32 のフラッシュメモリへ
  保持し次回はまずそれを参照する処理を加えた.詳細は DeviceAddress.cpp に.

 */
#include <WiFiClientSecure.h>
#include <esp8266-google-home-notifier.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "MqttBeebotteCom_pem.h"
#include "SplitArray.h"
#include "DeviceAddress.h"

//------- ユーザ定義 ------------------

// Google Home デバイスの IP アドレス, デバイス名

// 有効にすれば指定 IP アドレスを直接使用
// 無効にすれば指定デバイス名で mDNS 照会
//#define USE_GH_IPADRESS

IPAddress myGoogleHomeIPAddress(192,168,0,110);
#define myGoogleHomeDeviceName "room02"

// WiFi アクセスポイント
#define ssid "ssid"
#define password "pass"

// Beebotte 情報
#define mqtt_host  "mqtt.beebotte.com"
#define mqtt_port  8883
#define mqtt_topic "test01/msg"
#define mqtt_pass  "token:" "token_************"

// MP3 データを配置する Web サーバ
#define dataServer "192.168.0.127"
#define dataServerPort 80
// MP3 データの URL
#define MP3DataFmt "http://" dataServer "/sound/%s.mp3"
// MP3 再生リストのパス
#define MP3ListFmt "/sound/list/%s.txt"

//-------------------------------------

WiFiClientSecure wifiClientSecure;
PubSubClient pubsubClient(wifiClientSecure);
GoogleHomeNotifier ghn;
void doCast(const char *data, void (*callback)(int) = NULL);

char **playList = NULL;
int playListIndex = -1;
TaskHandle_t taskHandle = NULL;
EventGroupHandle_t evGroup = NULL;
int playCommand;

void dbg(const char *format, ...) {
  //return;
  char b[256];
  va_list va;
  va_start(va, format);
  vsnprintf(b, sizeof(b), format, va);
  va_end(va);
  Serial.print(b);
}

// MP3 連続再生用コールバック
void playEndCallback(int val) {
  playCommand = val;
  dbg("playaEndCallback: cmd=%d\r\n", playCommand);
  if (!taskHandle) {
    // 連続再生用タスクと制御用イベントグループを生成
    evGroup = xEventGroupCreate();
    xTaskCreatePinnedToCore(playTask, "playTask", 8192,
      (void*)&playCommand, 1, &taskHandle, 1);
    delay(200);
  }
  // タスクをキック
  xEventGroupSetBits(evGroup, BIT0);
}

// MP3 連続再生用タスク
void playTask(void *arg) {
  while (true) {
    // evGroup の BIT0 が立つまで待機
    xEventGroupWaitBits(evGroup,  BIT0, false, true, portMAX_DELAY);
    // 直ちに BIT0 をクリアして処理開始
    xEventGroupClearBits(evGroup, BIT0);
    int cmd = *((int*)arg);
    dbg("playTask: cmd=%d, idx=%d\r\n", cmd, playListIndex);
    if (cmd == -1) { // 別の再生が開始された
      dbg("playTask: started another playback..\r\n");
      continue;
    } else if (cmd == 0) { // 再生停止指示
      if (playList) {
        deleteSplitArray(playList);
        playList = NULL;
      }
      playListIndex = -1;
      continue;
    } else if (cmd == 1) { // 次の曲へ
      playListIndex++;
    }
    // リストの終端に達したらふたたび先頭から
    if (playListIndex >= countSplitArray(playList)) {
      playListIndex = 0;
    }
    dbg("playListIndex=%d\r\n", playListIndex);
    doCast(playList[playListIndex], playEndCallback);
  }
}

// 再生前処理
void doCast0(const char *data) {
  char path[128], firstElem[32], lastElem[32];
  int num = 0;
  bool doShuffle;
  char **ppArray;

  // 再生中のリストがあれば開放
  if (playList) {
    deleteSplitArray(playList);
    playList = NULL;
    playListIndex = -1;
  }
  // mp3 再生リスト指定以外ならそのまま通す
  if (data[0] != '@') {
    dbg("is not playlist\r\n");
    return doCast(data);
  }
  // 以下 mp3 再生リストの場合
  // カンマでデータを分割
  ppArray = createSplitArray(data, ',');
  if (!ppArray) {
    doCast("*パラメータの読み込みでエラーが発生しました。");
    return;
  } else {
    num = countSplitArray(ppArray);
    /*for (int i = 0; i < num; i++) {
      dbg("doCast0: ppArray[%d]=[%s]\r\n", i, ppArray[i]);
    }*/
    snprintf(firstElem, sizeof(firstElem), "%s", ppArray[0]);
    snprintf(lastElem, sizeof(lastElem), "%s", ppArray[num-1]);
    deleteSplitArray(ppArray);
  }
  doShuffle = false;
  if (num == 1) {
    snprintf(path, sizeof(path), MP3ListFmt, &data[1]);
  } else {
    if (strstr(lastElem, "ランダム") != NULL || 
      strstr(lastElem, "シャッフル") != NULL ||
      strstr(lastElem, "ガンダム") != NULL ||
      strstr(lastElem, "randam") != NULL || 
      lastElem[0] == '1') {
      doShuffle = true;
    }
    snprintf(path, sizeof(path), MP3ListFmt, &firstElem[1]);
  }
  WiFiClient wifiClient;
  // web サーバ上の再生リストを読み込む
  if (!wifiClient.connect(dataServer, dataServerPort)) {
    Serial.println("doCast0: connect failed");
    return;
  }
  String req = String("GET ") + path + " HTTP/1.1\r\n" +
    "Host: " + dataServer + "\r\n" +
    "Connection: close\r\n\r\n";
  dbg("doCast0: req=[%s]\r\n", req.c_str());
  wifiClient.print(req);
  unsigned long timeout = millis();
  while (wifiClient.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println("doCast0: fetch timeout..");
      wifiClient.stop();
      return;
    }
  }
  String listData = "";
  bool first = true, sts200 = true, inBody = false;
  while (wifiClient.available()) {
    // レスポンスを 1 行ずつ処理
    String line = wifiClient.readStringUntil('\n');
    if (first) {
      if (line.indexOf("200") == -1) {
        // ステータス 200 でなければ以降は空読みのみ
        sts200 = false;
      }
      first = false;
    }
    if (!sts200) {
      continue;
    }
    if (inBody) {
      line.trim();
      // コメント除去
      int pos = line.indexOf("#");
      if (pos != -1) {
        line = line.substring(0, pos);
        line.trim();
      }
      if (line.length() <= 0) {
        continue;
      }
      if (listData.length() > 0) {
        listData += ",";
      }
      listData = listData + line;
    } else {
      if (line.length() == 1) {
        // HTTP レスポンスボディ開始点
        inBody = true;
      }
    }
  }
  wifiClient.stop();
  //dbg("doCast0: listData=[%s]\r\n", listData.c_str());
  if (listData.length() <= 0) {
    dbg("doCast0: list is void..\r\n");
    doCast("*指定された再生リストは無効です。");
    return;
  }
  // 残ヒープ量の 1/2 を超えるサイズのリストは弾く
  uint32_t freeHeapSize = system_get_free_heap_size();
  dbg("doCast0: freeHeapSize=%u listData.length=%d\r\n", freeHeapSize, listData.length());
  if (listData.length() > freeHeapSize / 2) {
    doCast("*メモリが足りません。再生リストを小さくして下さい。");
    return;
  }
  // リストのアイテムをポインタ配列へ
  playList = createSplitArray(listData, ',');
  if (!playList) {
    doCast("*再生リストの読み込みでエラーが発生しました。");
    return;
  }
  if (doShuffle) {
    // 要素順をシャッフル    
    shuffleSplitArray(playList);
  }
  for (int i = 0; i < countSplitArray(playList); i++) {
    dbg("doCast0: [%s]\r\n", playList[i]);
  }
  // 連続再生を開始
  playListIndex = -1;
  playEndCallback(1);
}

// 一件の再生
void doCast(const char *data, void (*callback)(int)) {
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
    sts = ghn.play(url, callback);
  }
  if (!sts) {
    Serial.println(String("doCast: ") + ghn.getLastError());
    if (playList) {
      playEndCallback(1);
    }
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
    Serial.println(String("callbackMQTT: " + jsonErr));
  }
  JsonObject& root = doc.as<JsonObject>();
  const char *data = root["data"];
  dbg("callbackMQTT: value=[%s]\r\n", data);
  doCast0(data);
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
  if (ghn.ip(myGoogleHomeIPAddress, "ja") != true) {
    dbg("%s\r\n", ghn.getLastError());
  }
#else
  dbg("setAddress: using DeviceName\r\n");
  setDeviceAddress(&ghn, myGoogleHomeDeviceName);
#endif
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
        dbg("loop: MQTT connected\r\n");
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

