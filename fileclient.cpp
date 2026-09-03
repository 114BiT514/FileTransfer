#include "fileclient.h"

#include <QDataStream>
#include <QFileInfo>

FileClient::FileClient(QObject *parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    // 【信号与槽】Qt 网络类是异步事件驱动的：连接成功/断开/写出数据/出错
    // 都通过信号通知，全程不做阻塞等待，界面不会卡死。
    connect(m_socket, &QTcpSocket::connected, this, &FileClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &FileClient::onDisconnected);
    connect(m_socket, &QTcpSocket::bytesWritten, this, &FileClient::onBytesWritten);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &FileClient::onErrorOccurred);
}

FileClient::~FileClient()
{
    m_socket->abort();
}

//---------------------------------------------------------------------
// 连接管理
//---------------------------------------------------------------------

void FileClient::connectToServer(const QString &host, quint16 port)
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();                    // 连接中/已连接时先复位，再重新连接
    m_socket->connectToHost(host, port);      // 异步发起连接，结果由信号通知
    emit logInfo(QStringLiteral("正在连接 %1:%2 ...").arg(host).arg(port));
}

void FileClient::disconnectFromServer()
{
    if (m_transferring)
        finishTransfer(false, QStringLiteral("主动断开连接"));
    m_socket->disconnectFromHost();
}

bool FileClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QString FileClient::peerInfo() const
{
    return QStringLiteral("%1:%2").arg(m_socket->peerAddress().toString())
            .arg(m_socket->peerPort());
}

void FileClient::onConnected()
{
    emit logInfo(QStringLiteral("已连接到服务器 %1").arg(peerInfo()));
    emit stateChanged(true);
}

void FileClient::onDisconnected()
{
    if (m_transferring)
        finishTransfer(false, QStringLiteral("连接已断开，发送未完成"));
    emit logInfo(QStringLiteral("已断开与服务器的连接"));
    emit stateChanged(false);
}

void FileClient::onErrorOccurred(QAbstractSocket::SocketError)
{
    // 对端正常关闭属于常见情况，不按错误提示
    if (m_socket->error() != QAbstractSocket::RemoteHostClosedError)
        emit logError(QStringLiteral("网络错误：%1").arg(m_socket->errorString()));
    if (m_transferring)
        finishTransfer(false, m_socket->errorString());
    // 连接失败（如拒绝连接/超时）时套接字回到未连接状态，需让界面恢复
    if (m_socket->state() == QAbstractSocket::UnconnectedState)
        emit stateChanged(false);
}

//---------------------------------------------------------------------
// 发送入口
//---------------------------------------------------------------------

void FileClient::sendText(const QString &text)
{
    if (!isConnected()) {
        emit logError(QStringLiteral("尚未连接服务器，无法发送"));
        return;
    }
    if (m_transferring) {
        emit logError(QStringLiteral("正在传输文件，请等待完成后再发送消息"));
        return;
    }
    m_type = Proto::MT_TEXT;
    m_fileName = QStringLiteral("(文本消息)");
    m_pendingText = text.toUtf8();
    // 与服务端的防御性校验（kMaxTextSize）保持一致：超长文本提前拦截并提示，
    // 避免发出后被服务端当作非法报文断开连接
    if (m_pendingText.size() > Proto::kMaxTextSize) {
        emit logError(QStringLiteral("文本消息过长（上限 %1），未发送")
                          .arg(Proto::formatFileSize(Proto::kMaxTextSize)));
        m_pendingText.clear();
        m_type = 0;
        m_fileName.clear();
        return;
    }
    m_fileSize = m_pendingText.size();
    m_file = nullptr;
    startTransfer();
}

void FileClient::sendFile(const QString &filePath)
{
    if (!isConnected()) {
        emit logError(QStringLiteral("尚未连接服务器，无法发送文件"));
        return;
    }
    if (m_transferring) {
        emit logError(QStringLiteral("正在发送其他文件，请等待其完成"));
        return;
    }
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        emit logError(QStringLiteral("文件不存在：%1").arg(filePath));
        return;
    }
    auto *file = new QFile(filePath, this);
    if (!file->open(QIODevice::ReadOnly)) {
        emit logError(QStringLiteral("无法打开文件 %1：%2").arg(filePath, file->errorString()));
        file->deleteLater();
        return;
    }
    m_file = file;
    m_type = Proto::MT_FILE;
    m_fileName = info.fileName();             // 只发送文件名，不发送本地路径
    m_fileSize = file->size();
    // 与服务端的防御性校验（kMaxFileSize）保持一致：超大文件提前拦截并提示
    if (m_fileSize > Proto::kMaxFileSize) {
        emit logError(QStringLiteral("文件超过 %1 上限，未发送")
                          .arg(Proto::formatFileSize(Proto::kMaxFileSize)));
        file->close();
        file->deleteLater();
        m_file = nullptr;
        m_type = 0;
        m_fileSize = 0;
        m_fileName.clear();
        return;
    }
    emit logInfo(QStringLiteral("准备发送文件：%1（%2）")
                     .arg(m_fileName, Proto::formatFileSize(m_fileSize)));
    startTransfer();
}

/* 组装协议头并启动一次传输
 * 协议头 = 消息类型(4B) + 文件名长度(4B) + 文件名(UTF-8) + 数据大小(8B)
 * 文本消息 nameLen 固定为 0（不带文件名）；文件消息才携带文件名。
 */
void FileClient::startTransfer()
{
    const QByteArray name = (m_type == Proto::MT_FILE) ? m_fileName.toUtf8() : QByteArray();
    QByteArray header;
    QDataStream out(&header, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);      // 与接收端约定同一序列化版本（大端序）
    out << m_type << quint32(name.size()) << quint64(m_fileSize);
    header.append(name);                      // 紧跟文件名字节（文本消息时为空）

    m_headerSize = header.size();
    m_queued = 0;                             // 数据部分已排队字节数
    m_acked = 0;                              // 已确认写出的字节数
    m_lastEmitted = 0;
    m_transferring = true;

    m_socket->write(header);                  // 先写协议头（数据量小，一次写出）
    emit sendStarted(m_fileName, m_fileSize);
    pump();                                   // 立即写入第一批数据块
}

/* 流水线发送核心：
 *  - 每次只从文件读 64KB 写入套接字，大文件不会一次性占用内存；
 *  - 套接字内部待写缓冲（bytesToWrite）达到水位线就暂停，等 bytesWritten
 *    通知"已写出"后再继续写下一块，发送速度自动匹配网络带宽。
 */
void FileClient::pump()
{
    if (!m_transferring)
        return;
    if (m_socket->bytesToWrite() >= Proto::kChunkSize * 4)
        return;                               // 水位已满，等 bytesWritten 再继续

    const qint64 remaining = m_fileSize - m_queued;
    if (remaining > 0) {
        const qint64 want = qMin<qint64>(Proto::kChunkSize, remaining);
        QByteArray block;
        if (m_type == Proto::MT_FILE) {
            block = m_file->read(want);       // 从文件读一小块
            if (block.size() != int(want)) {  // 读到的字节数不足：文件读取失败
                finishTransfer(false, QStringLiteral("读取本地文件失败"));
                return;
            }
        } else {
            block = m_pendingText.mid(int(m_queued), int(want));
        }
        m_socket->write(block);               // 只写一小块，立即返回（非阻塞）
        m_queued += block.size();
    }

    // 数据已全部排队且套接字缓冲已清空 -> 整个传输完成
    if (m_queued >= m_fileSize && m_socket->bytesToWrite() == 0)
        finishTransfer(true);
}

/* bytesWritten(bytes)：表示又有 bytes 字节成功写到了操作系统。
 * 在这里更新进度并调用 pump() 继续发送 ——
 * 即作业要求的"write + bytesWritten 信号驱动的流水线方式"。
 */
void FileClient::onBytesWritten(qint64 bytes)
{
    if (!m_transferring)
        return;
    m_acked += bytes;
    const qint64 payloadAcked = qMax<qint64>(0, m_acked - m_headerSize); // 去掉协议头部分
    if (payloadAcked - m_lastEmitted >= Proto::kProgressStep
            || payloadAcked == m_fileSize) {
        m_lastEmitted = payloadAcked;
        emit sendProgress(m_fileName, payloadAcked, m_fileSize);
    }
    pump();
}

/* 一次传输结束：清理状态并通知界面 */
void FileClient::finishTransfer(bool ok, const QString &reason)
{
    m_transferring = false;
    if (m_file) {
        m_file->close();
        m_file->deleteLater();
        m_file = nullptr;
    }
    // 先保存结果并复位全部状态，之后再发信号。
    // 顺序不能反：sendFinished 的接收方（界面/调用方）很可能在槽里立即发起
    // 下一次传输（重入），若先发信号再复位，会把新传输刚建立的状态
    // （m_queued / m_fileSize / m_type 等）清空，导致传输悄悄"失踪"。
    const QString name = m_fileName;
    const qint64 size = m_fileSize;
    m_type = 0;
    m_fileSize = 0;
    m_headerSize = 0;
    m_queued = 0;
    m_acked = 0;
    m_lastEmitted = 0;
    m_fileName.clear();
    m_pendingText.clear();

    if (ok) {
        emit logInfo(QStringLiteral("发送完成：%1（%2）")
                         .arg(name, Proto::formatFileSize(size)));
        emit sendFinished(name, size);
    } else {
        emit logError(QStringLiteral("发送失败：%1").arg(reason));
        emit sendFailed(reason);
    }
}
