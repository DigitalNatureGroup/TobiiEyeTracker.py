import socket
import csv
from datetime import datetime

# ---------- ここは変更しないようにしてください！ ----------
# ソケット通信の準備 (AF_INET: IPv4という通信, SOCK_DGRAM: UDPという通信形態)
sock = socket.socket(socket.AF_INET, type=socket.SOCK_DGRAM)
# 通信を受け取るIPアドレス・ポート番号の定義
sock.bind(('127.0.0.1', 1235))
# ----------------------------------------------------------

# CSVの行を追加していく配列
csv_rows = [['timestamp [micros]', 'x', 'y']]

print('Waiting message')

while True:
    try :
        # クライアントからメッセージが届くまで待機
        message, client_addr = sock.recvfrom(1024)

        # メッセージが届いたら文字列形式にデコード・カンマ区切りで分解
        row = message.decode(encoding='utf-8').split(',')
        # 行の形式が違う場合は飛ばす
        if len(row) != 3:
            continue

        print(datetime.fromtimestamp(int(row[0]) / 1000000), ':', '(', row[1], ',', row[2], ')')

        # csv rowsに追加
        csv_rows.append(row[:3])

    # Ctrl+Cで実行を中止した時
    except KeyboardInterrupt:
        sock.close()
        break

# 現在時刻を記録
now = datetime.now().strftime('%Y%m%d-%H%M%S')
# csvファイルの保存
with open(now + '.csv', 'w', newline='') as f:
    csv.writer(f).writerows(csv_rows)

print('Wrote to:', now + '.csv')