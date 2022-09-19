# TobiiEyeTracker.py
Tobii Eye Tracker 4C/5 で取得したデータをPythonで分析するためのプログラムです。

これらのデバイスのSDKはC言語しか提供されていないため、C言語のプログラムで取得→Socket通信を通してPythonのプログラムにデータを送信、という流れで分析を行います。  
https://developer.tobii.com/product-integration/stream-engine/

Windows 10でのみ動作検証済みです。

## 使用方法

`eye_tracking.exe`を起動したあと、対応するPythonプログラムを実行します。
