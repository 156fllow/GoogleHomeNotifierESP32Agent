/*
  SplitArray.h

  Copyright (c) 2018 KLab Inc.

  Released under the MIT license
  https://opensource.org/licenses/mit-license.php

 */

#ifndef SPLITARRAY_H_
#define SPLITARRAY_H_

#include "Arduino.h"

// 指定文字列を指定セパレータで split した結果を
// ポインタ配列で返す
// 配列末尾に終端識別用の NULL エントリが付与される
char **createSplitArray(String str, char sep);
char **createSplitArray(const char *str, char sep);

// SplitArray に格納された要素数を返す
int countSplitArray(char **ppArray);

// SplitArray を開放
void deleteSplitArray(char **ppArray);

// SplitArray の要素順をシャッフル
void shuffleSplitArray(char **ppArray);

#endif // SPLITARRAY_H_
