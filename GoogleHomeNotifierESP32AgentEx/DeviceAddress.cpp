/*
  DeviceAddress.cpp

  Copyright (c) 2018 KLab Inc.

  Released under the MIT license
  https://opensource.org/licenses/mit-license.php

 */

#include "DeviceAddress.h"

void dbg(const char *format, ...);

const char *fileName = "/ip.txt";
uint16_t infoPort = 8008;

// GoogleHomeNotifier へ
// 指定された名前の Google Home デバイスの IP アドレスを設定.
//
// SPIFFS 上に保存ずみのアドレスデータが存在すれば
// アクセスを試し Google Home デバイスであることが確認できれば
// 成功すればそのアドレスを採用.
//
// 保存ずみのアドレスデータが存在しない, または
// 当該アドレスが Google Home デバイスであることを確認できなければ
// mDNS 照会を行って IP アドレスを取得し SPIFFS 上に保存.
//
// DHCP 割り当てであってもデバイスの IP アドレスは通常あまり頻繁に
// 変わるものではない一方で mDNS 照会には比較的時間がかかるため
// この方法を選んだ.アドレスが変わった場合にはボードをリセットすれば
// 自動的に修正される.
//
bool setDeviceAddress(GoogleHomeNotifier *ghn, const char *name) {

  File fp;
  uint8_t ip[4];
  uint8_t ipAddr[16];
  bool success = false;

  SPIFFS.begin(true);

/* for test1
  fp = SPIFFS.open(fileName, FILE_WRITE);
  fp.print("192.168.123.456");
  fp.close();
  SPIFFS.end();
  return true;
*/
/* for test2
  SPIFFS.remove(fileName);
  SPIFFS.end();
  return true;
*/

  // SPIFFS 上の IP アドレスデータの read を試行
  fp = SPIFFS.open(fileName);
  memset(ipAddr, 0, sizeof(ipAddr));
  if (fp) {
    fp.read(ipAddr, sizeof(ipAddr)-1);
    fp.close();
    dbg("setDeviceAddress: saved addr=[%s]\r\n", (char*)ipAddr);
  }

  // アドレスデータらしきものが存在
  if (strlen((char*)ipAddr) > 8) {
    WiFiClient wifiClient;
    // http://[ipAddr]:8008/setup/eureka_info へのアクセスを試行
    if (!wifiClient.connect((char*)ipAddr, infoPort)) {
      dbg("setDeviceAddress: %s:%d, connect failed\r\n",
          (char*)ipAddr, infoPort);
    } else {
      String req = String("GET /setup/eureka_info HTTP/1.1\r\n") +
        "Host: " + (char*)ipAddr + "\r\n" +
        "Connection: close\r\n\r\n";
      dbg("setDeviceAddress: req=[%s]\r\n", req.c_str());
      wifiClient.print(req);
      unsigned long timeout = millis();
      while (wifiClient.available() == 0) {
        if (millis() - timeout > 5000) {
          dbg("setDeviceAddress: fetch timeout..\r\n");
          wifiClient.stop();
          break;
        }
      } 
      bool first = true;
      while (wifiClient.available()) {
        String line = wifiClient.readStringUntil('\n');
        if (first) {
          dbg("setDeviceAddress: buf=[%s]\r\n", line.c_str());
          // レスポンスステータスが 200 なら OK
          if (line.indexOf("200") != -1) {
            success = true;
          }
          first = false;
        }
      }
    }
  }

  if (success) {
    // 保存ずみの IP アドレスを GoogleHomeNotifier へ設定
    char **ar = createSplitArray((char*)ipAddr, '.');
    ip[0] = atoi(ar[0]); ip[1] = atoi(ar[1]);
    ip[2] = atoi(ar[2]); ip[3] = atoi(ar[3]);
    deleteSplitArray(ar);
    IPAddress addr(ip);
    if (ghn->ip(addr, "ja") != true) {
      dbg("setDeviceAddress: %s\r\n", ghn->getLastError());
    }
    dbg("setDeviceAddress: addr=[%s]\r\n",
        ghn->getIPAddress().toString().c_str());
  } else {
    dbg("setDeviceAddress: calling mDNS..\r\n");
    if (ghn->device(name, "ja") != true) {
      dbg("setDeviceAddress: %s\r\n", ghn->getLastError());
    }
    dbg("setDeviceAddress: addr:port=[%s:%u]\r\n", 
      ghn->getIPAddress().toString().c_str(), ghn->getPort());

    // 今回取得した IP アドレスを SPIFFS 上に保存
    fp = SPIFFS.open(fileName, FILE_WRITE);
    snprintf((char*)ipAddr, sizeof(ipAddr), "%s", 
        ghn->getIPAddress().toString().c_str());
    fp.print((char*)ipAddr);
    fp.close();
    SPIFFS.end();
    // 再起動
    dbg("setDeviceAddress: restart...\r\n");
    ESP.restart();
  }
  SPIFFS.end();
  return true;
}
