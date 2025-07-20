/**
 * @file tftp_server.h
 * @brief TFTP server functionality interface
 */

#ifndef TFTP_SERVER_H_
#define TFTP_SERVER_H_

#include "tftp/tftp_common.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>

namespace tftpserver {

// 前方宣言
namespace internal {
class TftpServerImpl;
}

/**
 * @class TftpServer
 * @brief TFTPサーバー機能を提供するクラス
 */
class TFTP_EXPORT TftpServer {
 public:
  /**
   * @brief コンストラクタ
   * @param root_dir ルートディレクトリ
   * @param port ポート番号（デフォルトは69）
   */
  TftpServer(const std::string& root_dir, uint16_t port = kDefaultTftpPort);

  /**
   * @brief デストラクタ
   */
  ~TftpServer();

  /**
   * @brief サーバーを起動する
   * @return 成功した場合はtrue、失敗した場合はfalse
   */
  bool Start();

  /**
   * @brief サーバーを停止する
   */
  void Stop();

  /**
   * @brief サーバーが実行中かどうかを確認
   * @return 実行中の場合はtrue
   */
  bool IsRunning() const;

  /**
   * @brief ファイル読み取りコールバックを設定
   * @param callback ファイル読み取りのコールバック関数
   */
  void SetReadCallback(std::function<bool(const std::string&, std::vector<uint8_t>&)> callback);

  /**
   * @brief ファイル書き込みコールバックを設定
   * @param callback ファイル書き込みのコールバック関数
   */
  void SetWriteCallback(std::function<bool(const std::string&, const std::vector<uint8_t>&)> callback);

  /**
   * @brief セキュリティモードの設定
   * @param secure セキュアモードを有効にする場合はtrue
   */
  void SetSecureMode(bool secure);

  /**
   * @brief 最大転送サイズの設定
   * @param size 最大転送サイズ（バイト）
   */
  void SetMaxTransferSize(size_t size);

  /**
   * @brief タイムアウト値の設定
   * @param seconds タイムアウト（秒）
   */
  void SetTimeout(int seconds);

 private:
  friend class internal::TftpServerImpl;
  std::unique_ptr<internal::TftpServerImpl> impl_;
};

/**
 * @class TftpClient
 * @brief TFTPクライアント機能を提供するクラス（libcurlベース）
 */
class TFTP_EXPORT TftpClient {
 public:
  /**
   * @brief コンストラクタ
   */
  TftpClient();

  /**
   * @brief デストラクタ
   */
  ~TftpClient();

  /**
   * @brief ファイルをダウンロードする
   * @param host ホスト名またはIPアドレス
   * @param filename ファイル名
   * @param output_buffer 出力バッファ
   * @param port ポート番号（デフォルトは69）
   * @return 成功した場合はtrue、失敗した場合はfalse
   */
  bool DownloadFile(const std::string& host, const std::string& filename, 
                    std::vector<uint8_t>& output_buffer, uint16_t port = kDefaultTftpPort);

  /**
   * @brief ファイルをアップロードする
   * @param host ホスト名またはIPアドレス
   * @param filename ファイル名
   * @param data データバッファ
   * @param port ポート番号（デフォルトは69）
   * @return 成功した場合はtrue、失敗した場合はfalse
   */
  bool UploadFile(const std::string& host, const std::string& filename, 
                  const std::vector<uint8_t>& data, uint16_t port = kDefaultTftpPort);

  /**
   * @brief タイムアウト値の設定
   * @param seconds タイムアウト（秒）
   */
  void SetTimeout(int seconds);

  /**
   * @brief 転送モードの設定
   * @param mode 転送モード
   */
  void SetTransferMode(TransferMode mode);

  /**
   * @brief 最後のエラーメッセージを取得
   * @return エラーメッセージ
   */
  std::string GetLastError() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tftpserver

#endif // TFTP_SERVER_H_ 