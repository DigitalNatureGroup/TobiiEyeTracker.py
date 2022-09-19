import mss, mss.tools
import numpy as np
import cv2

import time
import math
import socket

# ---------- ここは変更しないようにしてください！ ----------
# ソケット通信の準備 (AF_INET: IPv4という通信, SOCK_DGRAM: UDPという通信形態)
sock = socket.socket(socket.AF_INET, type=socket.SOCK_DGRAM)
# 通信を受け取るIPアドレス・ポート番号の定義
sock.bind(('127.0.0.1', 1235))
# ----------------------------------------------------------

monitor_number = 1
with mss.mss() as sct:
    monitor = sct.monitors[monitor_number]
    monitor['mon'] = monitor_number
    grab_image = sct.grab

print('Waiting message')
while True:
    try:
        # クライアントからメッセージが届くまで待機
        message, client_addr = sock.recvfrom(1024)

        # メッセージが届いたら文字列形式にデコード・カンマ区切りで分解
        row = message.decode(encoding='utf-8').split(',')
        # 行の形式が違う場合は飛ばす
        if len(row) != 3:
            continue

        # スクリーンショットを取得
        frame = cv2.cvtColor(np.array(grab_image(monitor), np.uint8), cv2.COLOR_BGR2RGB)

        cv2.circle(frame, (float(row[1]) * frame.shape[1]), int(float(row[2]) * frame.shape[0])), 10, (0, 255, 0), thickness=5)
        cv2.imshow('Gaze Visualization', frame)
        if (cv2.waitKey(1) & 0xFF) == ord('q'):
            break
    # Ctrl+Cで実行を中止した時
    except KeyboardInterrupt:
        print ('\n . . .\n')
        sock.close()
        break
