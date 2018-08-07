/*
  DeviceAddress.h

  Copyright (c) 2018 KLab Inc.

  Released under the MIT license
  https://opensource.org/licenses/mit-license.php

 */

#ifndef DEVICEADDRESS_H_
#define DEVICEADDRESS_H_

#include "Arduino.h"
#include <WiFi.h>
#include <esp8266-google-home-notifier.h>
#include "FS.h"
#include "SPIFFS.h"
#include "SplitArray.h"

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
bool setDeviceAddress(GoogleHomeNotifier *ghn, const char *name);

#endif // DEVICEADDRESS_H_
