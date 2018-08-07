/*
  SplitArray.cpp

  Copyright (c) 2018 KLab Inc.

  Released under the MIT license
  https://opensource.org/licenses/mit-license.php

 */

#include "SplitArray.h"

// 指定文字列を指定セパレータで split した結果を
// ポインタ配列で返す
// 配列末尾に終端識別用の NULL エントリが付与される
char **createSplitArray(String str, char sep) {
  int len = str.length();
  if (len <= 0) {
    return NULL;  
  }
  char *datp = (char*)malloc(len+1);
  if (!datp) {
    return NULL;
  }
  str.toCharArray(datp, len+1);
  char **ret = createSplitArray(datp, sep);
  free(datp);
  return ret;
}

char **createSplitArray(const char *str, char sep) {
  char **ppArray = NULL;
  int len = strlen(str);
  if (len <= 0) {
    return NULL;  
  }
  char *datp = strdup(str);
  if (!datp) {
    return NULL;
  }
  int num = 0;
  char *p1 = datp, *p2;
  while (*p1 && (p1 = strchr(p1, sep))) {
    //dbg("[%s]\r\n", p1);
    num++;
    p1++;
  }
  num += 1;
  //dbg("num=%d\r\n", num);
  // 末尾に終端識別用の空要素を付加
  ppArray = (char**)malloc(sizeof(char*) * (num+1));
  memset(ppArray, 0, sizeof(char*) * (num+1));
  p1 = datp;
  if (num == 1) { // アイテムが一件のみの場合
    ppArray[0] = strdup(p1);
  } else {
    for (int i = 0; i < num; i++) {
      p2 = strchr(p1, sep);
      if (!p2) {
        p2 = datp + len;
      } else {
        *p2 = '\0';
      }
      // dbg("[%s]\r\n", p1);
      if (strlen(p1) > 0) {
        ppArray[i] = strdup(p1);
      } else {
        num -= 1;
      }
      p1 = p2 + 1;
    }
  }
  free(datp);
  return ppArray;
}

// SplitArray に格納された要素数を返す
int countSplitArray(char **ppArray) {
  if (!ppArray) {
    return 0;
  }
  int num = 0;
  while (ppArray[num]) num++;
  return num;
}

// SplitArray を開放
void deleteSplitArray(char **ppArray) {
  if (!ppArray) {
    return; 
  }
  int num = countSplitArray(ppArray);
  for (int i = 0; i < num; i++) {
    if (ppArray[i] != NULL) {
      free(ppArray[i]);
      ppArray[i] = NULL;    
    }
  }
  free(ppArray);
}

// SplitArray の要素順をシャッフル
// https://forum.arduino.cc/index.php?topic=345964.0#msg2385606
void shuffleSplitArray(char **ppArray) {
  if (!ppArray) {
    return;
  }
  int size = countSplitArray(ppArray);
  int last = 0;
  char *p = ppArray[last];
  for (int i = 0; i < size; i++) {
    int index = random(size);
    ppArray[last] = ppArray[index];
    last = index;
  }
  ppArray[last] = p;
}
