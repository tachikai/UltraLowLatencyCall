# UltraLowLatencyCall

![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-blue)
![Status](https://img.shields.io/badge/Status-Active%20Development-orange)

## 概要

YamahaのSyncRoomやSonoBusに相当する超低遅延音声通話アプリを個人で開発中。
最大20人がインターネット経由でリモート音楽セッションを行えることを目標としている。

---

## 技術的なハイライト（面接官が見るポイント）

### 計測済みのパフォーマンス

| 処理 | 遅延 |
|---|---|
| Opusエンコード + デコード | 0.050ms |
| サーバーミキシング（19人） | 0.93ms |
| UDP送信 | 0.1ms 以下 |
| **片道遅延合計** | **約23ms（LAN環境）** |

### 使用技術と選定理由

| 技術 | 用途 | 選定理由 |
|---|---|---|
| JUCE (C++) | クライアント | VST3とスタンドアロンを同一コードで実現 |
| Opus codec | 音声圧縮 | CELTモードで最低遅延・パケットロス補間対応 |
| Rust + tokio | サーバーネットワーク | メモリ安全・非同期I/Oで高スループット |
| AVX2 SIMD | ミキシング | 256bit幅演算で19ストリームを0.93msで処理 |
| linux-lowlatency | サーバーOS | リアルタイム処理優先のカーネル |

### アーキテクチャの工夫

- スレッドを使わないノンブロッキングUDP受信（WindowsのCOM初期化問題を回避）
- N-1ミックス（自分の声を自分に返さない）
- Opus `RESTRICTED_LOWDELAY` モードでアルゴリズム遅延を最小化

---

## システム構成図

```
クライアント（Windows 11）
  マイク入力
    → Opus encode（0.05ms）
    → UDP送信（9876/UDP）
          ↓
     サーバー（Ubuntu 24.04 LTS）
     Rust受信 → C++ AVX2ミックス（0.93ms）
     → Opus encode → UDP送信
          ↓
クライアント（Windows 11）
  UDP受信 → Opus decode → スピーカー出力

合計片道遅延：約23ms
```

---

## 開発フロー

9つのフェーズに分けて段階的に開発。
各フェーズで動作確認してから次に進む手法を採用。

| フェーズ | 内容 | 状態 |
|---|---|---|
| Phase 0 | 環境構築 | ✅ 完了 |
| Phase 1A | サーバーUDP基盤（Rust） | ✅ 完了 |
| Phase 1B | JUCEクライアント音声基盤 | ✅ 完了 |
| Phase 2 | Opusコーデック統合 | ✅ 完了 |
| Phase 3 | ネットワーク統合テスト | ✅ 完了 |
| Phase 4 | サーバー混合エンジン（AVX2） | ✅ 完了 |
| Phase 5 | マルチクライアントテスト | ✅ 完了 |
| Phase 6 | VST3パラ出力 | ✅ 完了 |
| Phase 7 | UI仕上げ | 🔧 開発中 |
| Phase 8 | 低遅延チューニング | ⬜ 予定 |
| Phase 9 | デプロイ・運用準備 | ⬜ 予定 |

---

## セットアップ

### クライアント（Windows 11）

**必要なもの**
- Visual Studio 2022（C++ワークロード）
- JUCE 最新版
- vcpkg + Opus 1.5.2

**ビルド手順**

1. vcpkg で Opus をインストール
   ```
   C:\vcpkg\vcpkg install opus:x64-windows
   ```
2. Projucer でプロジェクトを開く
3. Visual Studio でビルド

### サーバー（Ubuntu 24.04 LTS）

**必要なもの**
- linux-lowlatency カーネル
- Rust 1.95.0 以上
- libopus-dev

**起動方法**

```bash
cd ~/session-server
cargo run
```

---

## ライセンス

MIT License

## 作者

tachikai
