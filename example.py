import socket

# ---------- ここは変更しないようにしてください！ ----------
# ソケット通信の準備 (AF_INET: IPv4という通信, SOCK_DGRAM: UDPという通信形態)
sock = socket.socket(socket.AF_INET, type=socket.SOCK_DGRAM)
# 通信を受け取るIPアドレス・ポート番号の定義
sock.bind(('127.0.0.1', 1235))
# ----------------------------------------------------------

while True:
    try:
        print('Waiting message')
        # クライアントからメッセージが届くまで待機
        message, client_addr = sock.recvfrom(1024)
        # メッセージが届いたら文字列形式にデコード
        message = message.decode(encoding='utf-8')

        print(f'Received message is [{message}]')

    # Ctrl+Cで実行を中止した時
    except KeyboardInterrupt:
        print ('\n . . .\n')
        sock.close()
        break
