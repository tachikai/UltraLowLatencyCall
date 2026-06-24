// session-server/src/main.rs
//
// UDP エコーサーバー
//
// 動作:
//   1. UDPポート 9876 で待ち受ける
//   2. 受信したデータをそのまま送信元へ返す（エコー）
//   3. 単一スレッドで複数クライアントからのパケットを順次処理する
//      （UDP は接続レスのため、同じソケットで全クライアントを扱える）

use std::net::UdpSocket;

const LISTEN_ADDR: &str = "0.0.0.0:9876";
const BUF_SIZE: usize = 4096; // 1パケットの最大バイト数

fn main() {
    // ── ソケットを開いて待ち受け ─────────────────────────────────────────────
    let socket = UdpSocket::bind(LISTEN_ADDR)
        .expect("ポート 9876 のバインドに失敗しました");

    println!("UDP エコーサーバー起動: {}", LISTEN_ADDR);
    println!("Ctrl+C で終了");

    let mut buf = [0u8; BUF_SIZE];

    // ── メインループ：パケットを受け取るたびにそのまま返す ───────────────────
    loop {
        // recv_from: データを受信し、送信元アドレスも返す
        match socket.recv_from(&mut buf) {
            Ok((len, src)) => {
                println!("[{}] {} bytes 受信", src, len);

                // 受信データをそのまま送信元へ返す
                if let Err(e) = socket.send_to(&buf[..len], src) {
                    eprintln!("[{}] 送信エラー: {}", src, e);
                }
            }
            Err(e) => {
                eprintln!("受信エラー: {}", e);
            }
        }
    }
}
