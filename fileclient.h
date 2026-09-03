#ifndef FILECLIENT_H
#define FILECLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include "protocol.h"

/**
 * @brief 客户端（发送端）传输管理类，与界面分离。
 * 发送采用 write + bytesWritten 信号驱动的流水线方式，大文件不整体占用内存。
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
    void sendText(const QString &text);
    void sendFile(const QString &filePath);

signals:
    void stateChanged(bool connected);
    void logInfo(const QString &msg);
    void logError(const QString &msg);
    void sendStarted(const QString &fileName, qint64 total);
    void sendProgress(const QString &fileName, qint64 sent, qint64 total);
    void sendFinished(const QString &fileName, qint64 total);
    void sendFailed(const QString &reason);

private slots:
    void onConnected();
    void onDisconnected();
    void onBytesWritten(qint64 bytes);
    void onErrorOccurred(QAbstractSocket::SocketError error);

private:
    void startTransfer();
    void pump();                         // 流水线：视水位继续写下一块数据
    void finishTransfer(bool ok, const QString &reason = QString());

    QTcpSocket *m_socket = nullptr;
    QFile      *m_file = nullptr;        // 正在发送的文件（文本消息时为空）
    QByteArray  m_pendingText;           // 正在发送的文本
    bool        m_transferring = false;  // 一次只允许一个传输任务
    quint32     m_type = 0;
    QString     m_fileName;
    qint64      m_fileSize = 0;          // 数据总大小
    qint64      m_headerSize = 0;        // 协议头+文件名的字节数
    qint64      m_queued = 0;            // 已写入套接字的数据字节数
    qint64      m_acked = 0;             // bytesWritten 确认写出的字节数
    qint64      m_lastEmitted = 0;
};

#endif // FILECLIENT_H
