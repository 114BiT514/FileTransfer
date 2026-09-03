#include "fileserver.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>

FileServer::FileServer(QObject *parent)
    : QObject(parent)
{
    m_server = new QTcpServer(this);
    // 有新客户端接入，在槽里接受连接（也可重写 incomingConnection 实现同样功能）
    connect(m_server, &QTcpServer::newConnection, this, &FileServer::onNewConnection);
    connect(m_server, &QTcpServer::acceptError, this, &FileServer::onAcceptError);
}

FileServer::~FileServer()
{
    stopListen();
}

bool FileServer::startListen(quint16 port)
{
    if (m_server->isListening())
        return true;
    if (!m_server->listen(QHostAddress::Any, port)) {
        emit logError(QStringLiteral("监听失败：%1（端口可能被占用，请更换端口重试）")
                          .arg(m_server->errorString()));
        return false;
    }
    emit logInfo(QStringLiteral("服务端已启动，正在监听端口 %1 ...").arg(port));
    emit stateChanged(true);
    return true;
}

void FileServer::stopListen()
{
    if (!m_server->isListening())
        return;
    const auto sockets = m_clients.keys();
    for (QTcpSocket *sock : sockets) {
        ClientCtx *ctx = m_clients.take(sock);
        finishRecv(ctx, false, QStringLiteral("服务端停止监听"));
        delete ctx;
        sock->abort();
        sock->deleteLater();
    }
    m_server->close();
    emit logInfo(QStringLiteral("已停止监听"));
    emit stateChanged(false);
}

void FileServer::setSaveDir(const QString &dir)
{
    m_saveDir = dir;
}

void FileServer::setClientReadBufferSize(qint64 size)
{
    m_readBufLimit = size;
}

bool FileServer::isListening() const
{
    return m_server->isListening();
}

quint16 FileServer::listenPort() const
{
    return m_server->serverPort();
}

void FileServer::onNewConnection()
{
    while (QTcpSocket *sock = m_server->nextPendingConnection()) {
        auto *ctx = new ClientCtx;
        ctx->peer = QStringLiteral("%1:%2").arg(sock->peerAddress().toString())
                        .arg(sock->peerPort());
        m_clients.insert(sock, ctx);
        if (m_readBufLimit > 0)
            sock->setReadBufferSize(m_readBufLimit);  // 限制读缓冲（自测分包场景用）

        connect(sock, &QTcpSocket::readyRead, this, &FileServer::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &FileServer::onDisconnected);
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);

        emit logInfo(QStringLiteral("客户端已连接：%1").arg(ctx->peer));
        emit clientConnected(ctx->peer);
    }
}

void FileServer::onDisconnected()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock)
        return;
    ClientCtx *ctx = m_clients.take(sock);
    if (!ctx)
        return;   // stopListen() 已处理过
    const QString peer = ctx->peer;
    finishRecv(ctx, false, QStringLiteral("客户端断开连接，接收未完成"));
    delete ctx;
    emit logInfo(QStringLiteral("客户端已断开：%1").arg(peer));
    emit clientDisconnected(peer);
}

void FileServer::onAcceptError()
{
    emit logError(QStringLiteral("接受连接出错：%1").arg(m_server->errorString()));
}

void FileServer::onReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock)
        return;
    ClientCtx *ctx = m_clients.value(sock);
    if (!ctx)
        return;

    // 到达的字节先追加进缓冲区，分次到达的数据在这里拼接
    ctx->buffer += sock->readAll();

    // 状态机循环：够一段就解析一段，解决粘包/拆包
    bool progressed = true;
    while (progressed && m_clients.contains(sock)) {
        progressed = false;
        switch (ctx->state) {
        case ClientCtx::WaitHeader:
            progressed = parseHeader(ctx);
            break;
        case ClientCtx::WaitName:
            progressed = parseName(ctx);
            break;
        case ClientCtx::WaitData:
            processData(ctx);
            progressed = (ctx->state == ClientCtx::WaitHeader);
            break;
        }
    }

    // 出错：断开该客户端并清理上下文
    if (ctx->failed) {
        const QString reason = ctx->errorReason;
        m_clients.remove(sock);
        finishRecv(ctx, false, reason);
        delete ctx;
        sock->abort();
        sock->deleteLater();
        emit logError(QStringLiteral("已断开异常客户端：%1").arg(reason));
    }
}

/* 解析 16 字节固定头：类型 4B + 名长 4B + 数据大小 8B */
bool FileServer::parseHeader(ClientCtx *ctx)
{
    if (ctx->buffer.size() < int(Proto::kHeaderSize))
        return false;                     // 头不完整，等下一次数据

    QDataStream in(ctx->buffer);          // 大端序，与发送端约定一致
    in.setVersion(QDataStream::Qt_6_0);
    in >> ctx->type >> ctx->nameLen >> ctx->fileSize;
    ctx->buffer.remove(0, int(Proto::kHeaderSize));
    ctx->received = 0;
    ctx->lastEmitted = 0;

    // 字段非法说明不是本协议的数据，直接断开
    const bool typeOk = (ctx->type == Proto::MT_TEXT || ctx->type == Proto::MT_FILE);
    const bool lenOk  = (ctx->nameLen <= Proto::kMaxNameLen)
                        && (ctx->fileSize <= quint64(Proto::kMaxFileSize))
                        && (ctx->type == Proto::MT_FILE ? ctx->nameLen > 0 : ctx->nameLen == 0)
                        && (ctx->type == Proto::MT_TEXT
                                ? ctx->fileSize <= quint64(Proto::kMaxTextSize) : true);
    if (!typeOk || !lenOk) {
        ctx->failed = true;
        ctx->errorReason = QStringLiteral("非法协议头");
        return false;
    }

    ctx->state = (ctx->nameLen > 0) ? ClientCtx::WaitName : ClientCtx::WaitData;
    return true;
}

/* 解析文件名，并为接收文件做准备 */
bool FileServer::parseName(ClientCtx *ctx)
{
    if (ctx->buffer.size() < int(ctx->nameLen))
        return false;

    ctx->fileName = QString::fromUtf8(ctx->buffer.constData(), int(ctx->nameLen));
    ctx->buffer.remove(0, int(ctx->nameLen));

    if (ctx->type == Proto::MT_FILE) {
        QDir().mkpath(m_saveDir);
        ctx->savePath = uniqueSavePath(ctx->fileName);
        ctx->file = new QFile(ctx->savePath + QStringLiteral(".part"), this);
        if (!ctx->file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            ctx->failed = true;
            ctx->errorReason = QStringLiteral("创建本地文件失败：%1").arg(ctx->file->errorString());
            return false;
        }
        emit logInfo(QStringLiteral("开始接收文件：%1（%2）")
                         .arg(ctx->fileName, Proto::formatFileSize(qint64(ctx->fileSize))));
        emit recvStarted(ctx->fileName, qint64(ctx->fileSize));
    } else {
        ctx->textBytes.clear();
    }

    ctx->state = ClientCtx::WaitData;
    if (ctx->fileSize == 0)
        finishRecv(ctx, true);
    return true;
}

/* 数据部分：可能分多次到达，到多少写多少 */
void FileServer::processData(ClientCtx *ctx)
{
    const qint64 want = qint64(ctx->fileSize) - ctx->received;
    if (want <= 0 || ctx->buffer.isEmpty())
        return;
    if (ctx->type == Proto::MT_FILE && !ctx->file)
        return;

    const int take = int(qMin<qint64>(ctx->buffer.size(), want));
    if (ctx->type == Proto::MT_FILE) {
        ctx->file->write(ctx->buffer.constData(), take);   // 到一块写一块
    } else {
        ctx->textBytes.append(ctx->buffer.constData(), take);
    }
    ctx->buffer.remove(0, take);
    ctx->received += take;

    // 进度节流：每 256KB 或收完时才发一次
    if (ctx->received - ctx->lastEmitted >= Proto::kProgressStep
            || ctx->received == qint64(ctx->fileSize)) {
        ctx->lastEmitted = ctx->received;
        emit recvProgress(ctx->fileName, ctx->received, qint64(ctx->fileSize));
    }

    if (ctx->received == qint64(ctx->fileSize))   // 组包完成
        finishRecv(ctx, true);
}

/* 一段报文接收结束（或中途失败），复位状态机 */
void FileServer::finishRecv(ClientCtx *ctx, bool ok, const QString &reason)
{
    if (ctx->file) {
        ctx->file->close();
        delete ctx->file;
        ctx->file = nullptr;
    }

    // 先复位再发信号，防止槽内重入时看到未收尾的旧传输
    const quint32 type = ctx->type;
    const QString fileName = ctx->fileName;
    const QString savePath = ctx->savePath;
    const QString peer = ctx->peer;
    const QString text = QString::fromUtf8(ctx->textBytes);
    const qint64 total = qint64(ctx->fileSize);
    // buffer 不能清空：里面可能已经有下一条报文的数据（粘包场景）
    ctx->state = ClientCtx::WaitHeader;
    ctx->type = 0;
    ctx->nameLen = 0;
    ctx->fileSize = 0;
    ctx->received = 0;
    ctx->lastEmitted = 0;
    ctx->fileName.clear();
    ctx->savePath.clear();
    ctx->textBytes.clear();

    if (ok && type == Proto::MT_FILE) {
        const QString tmp = savePath + QStringLiteral(".part");
        if (QFile::rename(tmp, savePath)) {
            emit logInfo(QStringLiteral("接收完成：%1（%2）已保存到 %3")
                             .arg(fileName, Proto::formatFileSize(total), savePath));
            emit recvFinished(fileName, total, savePath);
        } else {
            emit logError(QStringLiteral("临时文件改名失败：%1").arg(tmp));
            emit recvFailed(QStringLiteral("临时文件改名失败"));
        }
    } else if (ok && type == Proto::MT_TEXT) {
        emit textReceived(peer, text);
    } else if (!ok && type != 0) {
        if (type == Proto::MT_FILE)
            QFile::remove(savePath + QStringLiteral(".part"));
        emit recvFailed(reason.isEmpty() ? QStringLiteral("接收中断") : reason);
    }
}

/* 生成不冲突的保存路径，同名文件自动追加序号 */
QString FileServer::uniqueSavePath(const QString &fileName) const
{
    // 只取纯文件名，防对端传路径造成目录穿越
    const QString base = QFileInfo(fileName).fileName();
    QFileInfo info(QDir(m_saveDir).filePath(base));
    if (!info.exists())
        return info.absoluteFilePath();

    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix();
    for (int i = 1;; ++i) {
        const QString name = suffix.isEmpty()
                ? QStringLiteral("%1(%2)").arg(stem).arg(i)
                : QStringLiteral("%1(%2).%3").arg(stem).arg(i).arg(suffix);
        QFileInfo cand(QDir(m_saveDir).filePath(name));
        if (!cand.exists())
            return cand.absoluteFilePath();
    }
}
