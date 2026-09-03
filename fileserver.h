#ifndef FILESERVER_H
#define FILESERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QHash>
#include "protocol.h"

/**
 * @brief 服务端（接收端）传输管理类，与界面分离。
 * 用协议状态机对收到的字节流做分包、组包，支持多客户端同时连接。
 */
class FileServer : public QObject
{
    Q_OBJECT
public:
    explicit FileServer(QObject *parent = nullptr);
    ~FileServer() override;

    bool startListen(quint16 port);
    void stopListen();
    bool isListening() const;
    quint16 listenPort() const;
    void setSaveDir(const QString &dir);
    QString saveDir() const { return m_saveDir; }
    void setClientReadBufferSize(qint64 size); // 限制读缓冲（自测分包场景用，0=不限制）

signals:
    void stateChanged(bool listening);
    void clientConnected(const QString &peer);
    void clientDisconnected(const QString &peer);
    void logInfo(const QString &msg);
    void logError(const QString &msg);
    void recvStarted(const QString &fileName, qint64 total);
    void recvProgress(const QString &fileName, qint64 received, qint64 total);
    void recvFinished(const QString &fileName, qint64 total, const QString &savedPath);
    void recvFailed(const QString &reason);
    void textReceived(const QString &peer, const QString &text);

private slots:
    void onNewConnection();
    void onReadyRead();       // 核心：分包组包状态机
    void onDisconnected();
    void onAcceptError();

private:
    // 每个客户端连接对应一个接收上下文（协议状态机）
    struct ClientCtx {
        enum State { WaitHeader, WaitName, WaitData } state = WaitHeader;
        QByteArray buffer;        // 分次到达的数据先拼接在这里
        quint32 type = 0;
        quint32 nameLen = 0;
        quint64 fileSize = 0;
        qint64  received = 0;
        qint64  lastEmitted = 0;  // 进度节流
        QString peer;
        QString fileName;
        QString savePath;
        QFile   *file = nullptr;  // 正在写入的 .part 临时文件
        QByteArray textBytes;
        bool failed = false;
        QString errorReason;
    };

    bool parseHeader(ClientCtx *ctx);  // 解析 16 字节固定头
    bool parseName(ClientCtx *ctx);    // 解析文件名并准备本地文件
    void processData(ClientCtx *ctx);  // 消费数据部分
    void finishRecv(ClientCtx *ctx, bool ok, const QString &reason = QString());
    QString uniqueSavePath(const QString &fileName) const; // 同名文件自动改名

    QTcpServer *m_server = nullptr;
    QString m_saveDir;
    qint64  m_readBufLimit = 0;
    QHash<QTcpSocket *, ClientCtx *> m_clients;
};

#endif // FILESERVER_H
