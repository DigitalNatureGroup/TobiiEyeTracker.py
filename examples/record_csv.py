import socket
from datetime import datetime

# ---------- ここは変更しないようにしてください！ ----------
# ソケット通信の準備 (AF_INET: IPv4という通信, SOCK_DGRAM: UDPという通信形態)
sock = socket.socket(socket.AF_INET, type=socket.SOCK_DGRAM)
# 通信を受け取るIPアドレス・ポート番号の定義
sock.bind(('127.0.0.1', 1235))
# ----------------------------------------------------------

# CSVの行を追加していく配列
csv_rows = [['timestamp [ms]', 'x', 'y']]

while True:
    try :
        print('Waiting message')
        # クライアントからメッセージが届くまで待機
        message, client_addr = sock.recvfrom(1024)

        # メッセージが届いたら文字列形式にデコード・カンマ区切りで分解
        row = message.decode(encoding='utf-8').split(',')
        print(row[0], ':', '(', row[1], ',', row[2], ')')

        # csv rowsに追加
        csv_rows.append(row)

    # Ctrl+Cで実行を中止した時
    except KeyboardInterrupt:
        sock.close()
        break

# 現在時刻を記録
now = datetime().strftime('%Y%M%d-%h%m%s')
# csvファイルの保存
with open(now + '.csv', 'w') as f:
    csv.writer(f).writerows(csv_rows)
