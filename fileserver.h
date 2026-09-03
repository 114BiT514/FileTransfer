#ifndef FILESERVER_H
#define FILESERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QHash>
#include "protocol.h"

/**
 * @brief 服务端（接收端）传输管理类 —— 网络逻辑与界面(View)分离
 *
 * 职责：
 *  1. 监听端口、接受客户端连接（QTcpServer::newConnection 信号）；
 *  2. 持续接收数据：用"协议状态机"对字节流做分包、组包，正确处理粘包/拆包，
 *     大文件按到达顺序直接写入本地文件，不整体驻留内存；
 *  3. 收到的文件先写 .part 临时文件，收完后再重命名为正式文件（同名自动加序号）；
 *  4. 通过信号把日志 / 进度 / 结果上报给界面层，界面只负责显示。
 *
 * 支持多个客户端同时连接，每个客户端连接一个独立的接收上下文。
 */
class FileServer : public QObject
{
    Q_OBJECT
public:
    explicit FileServer(QObject *parent = nullptr);
    ~FileServer() override;

    bool startListen(quint16 port);          // 开始监听（QTcpServer::listen）
    void stopListen();                       // 停止监听并断开所有客户端
    bool isListening() const;
    quint16 listenPort() const;
    void setSaveDir(const QString &dir);     // 设置接收文件的保存目录
    QString saveDir() const { return m_saveDir; }
    void setClientReadBufferSize(qint64 size); // 限制每个套接字的读缓冲（自测分包用，0=不限制）

signals:
    void stateChanged(bool listening);                                       // 监听状态变化
    void clientConnected(const QString &peer);                               // 客户端上线
    void clientDisconnected(const QString &peer);                            // 客户端下线
    void logInfo(const QString &msg);                                        // 请求界面追加普通日志
    void logError(const QString &msg);                                       // 请求界面追加错误日志
    void recvStarted(const QString &fileName, qint64 total);                 // 开始接收一个文件
    void recvProgress(const QString &fileName, qint64 received, qint64 total); // 接收进度
    void recvFinished(const QString &fileName, qint64 total,
                      const QString &savedPath);                             // 接收完成
    void recvFailed(const QString &reason);                                  // 接收失败
    void textReceived(const QString &peer, const QString &text);             // 收到文本消息

private slots:
    void onNewConnection();   // 有新客户端连入 -> 接受连接
    void onReadyRead();       // 有数据到达（核心：分包组包状态机）
    void onDisconnected();    // 客户端断开 -> 清理上下文
    void onAcceptError();     // 接受连接出错

private:
    // 每个客户端连接对应一个"接收上下文"（协议状态机）
    struct ClientCtx {
        enum State { WaitHeader, WaitName, WaitData } state = WaitHeader;
        QByteArray buffer;        // 原始字节缓冲：把"分次到达"的数据先拼接在这里
        quint32 type = 0;         // 当前报文的消息类型
        quint32 nameLen = 0;      // 当前报文的文件名长度
        quint64 fileSize = 0;     // 当前报文的数据总大小
        qint64  received = 0;     // 数据部分已接收的字节数
        qint64  lastEmitted = 0;  // 上次发进度信号时的字节数（节流）
        QString peer;             // 对端 "ip:port"
        QString fileName;         // 对端文件名
        QString savePath;         // 本地保存路径（正式名）
        QFile   *file = nullptr;  // 正在写入的 .part 临时文件
        QByteArray textBytes;     // 文本消息累积缓冲
        bool failed = false;      // 协议/IO 出错标记（本轮处理完后断开该客户端）
        QString errorReason;
    };

    bool parseHeader(ClientCtx *ctx);  // 状态机：解析 16 字节固定协议头
    bool parseName(ClientCtx *ctx);    // 状态机：解析文件名并准备本地文件
    void processData(ClientCtx *ctx);  // 状态机：消费数据部分（写文件/累积文本）
    void finishRecv(ClientCtx *ctx, bool ok, const QString &reason = QString());
    QString uniqueSavePath(const QString &fileName) const; // 同名文件自动改名

    QTcpServer *m_server = nullptr;
    QString m_saveDir;                 // 接收文件保存目录
    qint64  m_readBufLimit = 0;        // 套接字读缓冲限制（测试用）
    QHash<QTcpSocket *, ClientCtx *> m_clients;  // 套接字 -> 接收上下文
};

#endif // FILESERVER_H
