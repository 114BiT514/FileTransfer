#include "fileserver.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>

FileServer::FileServer(QObject *parent)
    : QObject(parent)
{
    m_server = new QTcpServer(this);
    // 【信号与槽】有新客户端接入时触发 newConnection，在槽函数里接受连接。
    // 等价做法：重写 QTcpServer::incomingConnection(qintptr socketDescriptor)，
    // 在其中 new QTcpSocket 并调用 setSocketDescriptor() 包装底层套接字。
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
    // 监听本机所有网卡的指定端口（异步操作，返回值即结果）
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
    // 断开并清理所有客户端
    const auto sockets = m_clients.keys();
    for (QTcpSocket *sock : sockets) {
        ClientCtx *ctx = m_clients.take(sock);
        finishRecv(ctx, false, QStringLiteral("服务端停止监听"));
        delete ctx;
        sock->abort();
        sock->deleteLater();
    }
    m_server->close();      // 停止监听
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

//---------------------------------------------------------------------
// 连接的建立与断开
//---------------------------------------------------------------------

void FileServer::onNewConnection()
{
    // 可能一次有多个等待中的连接，循环全部取出
    while (QTcpSocket *sock = m_server->nextPendingConnection()) {
        auto *ctx = new ClientCtx;
        ctx->peer = QStringLiteral("%1:%2").arg(sock->peerAddress().toString())
                        .arg(sock->peerPort());
        m_clients.insert(sock, ctx);
        if (m_readBufLimit > 0)
            sock->setReadBufferSize(m_readBufLimit);  // 限制读缓冲，制造分包场景（自测用）

        // 【信号与槽】数据到达 / 对端断开 -> 绑定对应的处理槽
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
    // sock 已连接到 deleteLater，稍后自动销毁
}

void FileServer::onAcceptError()
{
    emit logError(QStringLiteral("接受连接出错：%1").arg(m_server->errorString()));
}

//---------------------------------------------------------------------
// 接收核心：分包 + 组包状态机
//---------------------------------------------------------------------

void FileServer::onReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock)
        return;
    ClientCtx *ctx = m_clients.value(sock);
    if (!ctx)
        return;

    // 1) 把本次到达的原始字节追加进缓冲区（"分次到达"的数据在这里拼接）
    ctx->buffer += sock->readAll();

    // 2) 状态机循环：缓冲区够一段就解析一段，直到数据不足（解决粘包/拆包）
    bool progressed = true;
    while (progressed && m_clients.contains(sock)) {
        progressed = false;
        switch (ctx->state) {
        case ClientCtx::WaitHeader:
            progressed = parseHeader(ctx);   // 凑够 16 字节固定头则推进
            break;
        case ClientCtx::WaitName:
            progressed = parseName(ctx);     // 凑够文件名字节数则推进
            break;
        case ClientCtx::WaitData:
            processData(ctx);                // 尽量消费数据；收完一段回到 WaitHeader
            progressed = (ctx->state == ClientCtx::WaitHeader);
            break;
        }
    }

    // 3) 出现协议/IO 错误：断开该客户端，清理其上下文
    if (ctx->failed) {
        const QString reason = ctx->errorReason;      // 先取出错误原因再释放上下文
        m_clients.remove(sock);
        finishRecv(ctx, false, reason);
        delete ctx;
        sock->abort();          // 立即断开
        sock->deleteLater();
        emit logError(QStringLiteral("已断开异常客户端：%1").arg(reason));
    }
}

/* 状态 1：等待/解析固定协议头（16 字节 = 类型 4B + 名长 4B + 数据大小 8B） */
bool FileServer::parseHeader(ClientCtx *ctx)
{
    if (ctx->buffer.size() < int(Proto::kHeaderSize))
        return false;                     // 头不完整 -> 等待更多数据（拆包场景）

    // 从缓冲区开头解析固定头；QDataStream 默认大端序，与发送端约定一致
    QDataStream in(ctx->buffer);
    in.setVersion(QDataStream::Qt_6_0);
    in >> ctx->type >> ctx->nameLen >> ctx->fileSize;
    ctx->buffer.remove(0, int(Proto::kHeaderSize));   // 头部字节已消费，从缓冲移除
    ctx->received = 0;
    ctx->lastEmitted = 0;

    // 防御性校验：类型/长度非法说明不是本协议的数据，直接断开
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

/* 状态 2：等待/解析文件名，并为文件接收做好准备 */
bool FileServer::parseName(ClientCtx *ctx)
{
    if (ctx->buffer.size() < int(ctx->nameLen))
        return false;                     // 文件名未到齐 -> 继续等待

    ctx->fileName = QString::fromUtf8(ctx->buffer.constData(), int(ctx->nameLen));
    ctx->buffer.remove(0, int(ctx->nameLen));

    if (ctx->type == Proto::MT_FILE) {
        // 准备本地文件：目录不存在则创建；同名冲突自动改序号；先写 .part 临时文件
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
    if (ctx->fileSize == 0)               // 空文件：直接完成本次接收
        finishRecv(ctx, true);
    return true;
}

/* 状态 3：消费数据部分 —— 数据可能分多次到达，到多少写多少 */
void FileServer::processData(ClientCtx *ctx)
{
    const qint64 want = qint64(ctx->fileSize) - ctx->received;
    if (want <= 0 || ctx->buffer.isEmpty())
        return;
    if (ctx->type == Proto::MT_FILE && !ctx->file)
        return;

    const int take = int(qMin<qint64>(ctx->buffer.size(), want));
    if (ctx->type == Proto::MT_FILE) {
        // 大文件：到一块写一块，不整体驻留内存
        ctx->file->write(ctx->buffer.constData(), take);
    } else {
        ctx->textBytes.append(ctx->buffer.constData(), take);
    }
    ctx->buffer.remove(0, take);
    ctx->received += take;

    // 进度信号节流：每 256KB 或收完时才发一次，避免高频刷新拖慢界面
    if (ctx->received - ctx->lastEmitted >= Proto::kProgressStep
            || ctx->received == qint64(ctx->fileSize)) {
        ctx->lastEmitted = ctx->received;
        emit recvProgress(ctx->fileName, ctx->received, qint64(ctx->fileSize));
    }

    if (ctx->received == qint64(ctx->fileSize))   // 一段数据完整接收 -> 组包完成
        finishRecv(ctx, true);
}

/* 一段报文接收结束（或中途失败）：收尾并复位状态机 */
void FileServer::finishRecv(ClientCtx *ctx, bool ok, const QString &reason)
{
    if (ctx->file) {
        ctx->file->close();
        delete ctx->file;
        ctx->file = nullptr;
    }

    // 先取出结果并复位状态机，之后再发信号（防重入）：
    // recvFinished/textReceived 的接收方若在槽里继续与本对象交互，
    // 看到的应当是"空闲"状态，而不是一段还没收尾的旧传输。
    const quint32 type = ctx->type;
    const QString fileName = ctx->fileName;
    const QString savePath = ctx->savePath;
    const QString peer = ctx->peer;
    const QString text = QString::fromUtf8(ctx->textBytes);
    const qint64 total = qint64(ctx->fileSize);
    // 注意：buffer 不能清空 —— 里面可能已经躺着下一条报文的数据（粘包场景）
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
        // 接收完成：把 .part 临时文件重命名为正式文件名
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
        // 传输中途失败/断开：删除不完整的 .part 残留文件
        if (type == Proto::MT_FILE)
            QFile::remove(savePath + QStringLiteral(".part"));
        emit recvFailed(reason.isEmpty() ? QStringLiteral("接收中断") : reason);
    }
}

/* 生成不冲突的保存路径：同名文件自动追加 (1)、(2)… 序号 */
QString FileServer::uniqueSavePath(const QString &fileName) const
{
    // 只取纯文件名，防止对端传 "..\\..\\x" 之类的路径造成目录穿越
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
