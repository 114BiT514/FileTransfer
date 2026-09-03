#ifndef FILECLIENT_H
#define FILECLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include "protocol.h"

/**
 * @brief 客户端（发送端）传输管理类 —— 网络逻辑与界面(View)分离（FileSender）
 *
 * 职责：
 *  1. 连接 / 断开服务器（QTcpSocket::connectToHost，全异步事件驱动）；
 *  2. 按自定义协议头发送文本消息与文件（见 protocol.h）；
 *  3. 采用 write + bytesWritten 信号驱动的"流水线"发送：
 *     每次只从文件读取一小块（64KB）写入套接字；收到 bytesWritten 通知表示
 *     "已写出 n 字节"，再继续写下一块。套接字内部待写缓冲超过水位线就暂停，
 *     避免把大文件一次性读入内存；
 *  4. 通过信号上报连接状态 / 进度 / 错误，界面层只负责显示。
 *
 * 同一时刻只允许一个传输任务（文本或文件），由 isBusy() 保护，
 * 这样 bytesWritten 的字节计数可以安全地归属到当前传输。
 */
class FileClient : public QObject
{
    Q_OBJECT
public:
    explicit FileClient(QObject *parent = nullptr);
    ~FileClient() override;

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const;
    bool isBusy() const { return m_transferring; }
    QAbstractSocket::SocketState socketState() const { return m_socket->state(); }
    QString peerInfo() const;

public slots:
    void sendText(const QString &text);        // 发送文本消息
    void sendFile(const QString &filePath);    // 发送本地文件

signals:
    void stateChanged(bool connected);                                 // 连接状态变化
    void logInfo(const QString &msg);
    void logError(const QString &msg);
    void sendStarted(const QString &fileName, qint64 total);           // 开始一次发送
    void sendProgress(const QString &fileName, qint64 sent, qint64 total); // 发送进度
    void sendFinished(const QString &fileName, qint64 total);          // 发送完成
    void sendFailed(const QString &reason);                            // 发送失败

private slots:
    void onConnected();
    void onDisconnected();
    void onBytesWritten(qint64 bytes);   // 核心：流水线续传
    void onErrorOccurred(QAbstractSocket::SocketError error);

private:
    void startTransfer();                // 组协议头并启动一次传输
    void pump();                         // 视水位继续写入下一块数据
    void finishTransfer(bool ok, const QString &reason = QString());

    QTcpSocket *m_socket = nullptr;
    QFile      *m_file = nullptr;        // 正在发送的本地文件（文本消息时为 nullptr）
    QByteArray  m_pendingText;           // 正在发送的文本内容
    bool        m_transferring = false;  // 传输中标志（一次只允许一个任务）
    quint32     m_type = 0;              // 本次传输的消息类型（MT_TEXT / MT_FILE）
    QString     m_fileName;              // 发送的文件名（只含名字，不含路径）
    qint64      m_fileSize = 0;          // 本次传输的数据总大小
    qint64      m_headerSize = 0;        // 协议头 + 文件名 的总字节数
    qint64      m_queued = 0;            // 数据部分已写入套接字的字节数
    qint64      m_acked = 0;             // 已被 bytesWritten 确认写出的字节数（含协议头）
    qint64      m_lastEmitted = 0;       // 进度节流
};

#endif // FILECLIENT_H
