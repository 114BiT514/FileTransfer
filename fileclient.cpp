#include "fileclient.h"

#include <QDataStream>
#include <QFileInfo>

FileClient::FileClient(QObject *parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    // 连接/断开/写出/出错全部通过信号异步通知，不做阻塞等待
    connect(m_socket, &QTcpSocket::connected, this, &FileClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &FileClient::onDisconnected);
    connect(m_socket, &QTcpSocket::bytesWritten, this, &FileClient::onBytesWritten);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &FileClient::onErrorOccurred);
}

FileClient::~FileClient()
{
    m_socket->abort();
}

void FileClient::connectToServer(const QString &host, quint16 port)
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_socket->connectToHost(host, port);
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
    if (m_socket->error() != QAbstractSocket::RemoteHostClosedError)   // 对端正常关闭不算错误
        emit logError(QStringLiteral("网络错误：%1").arg(m_socket->errorString()));
    if (m_transferring)
        finishTransfer(false, m_socket->errorString());
    if (m_socket->state() == QAbstractSocket::UnconnectedState)
        emit stateChanged(false);   // 连接失败时让界面恢复状态
}

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

/* 组协议头：类型(4B)+名长(4B)+文件名(UTF-8)+数据大小(8B)，文本消息 nameLen=0 */
void FileClient::startTransfer()
{
    const QByteArray name = (m_type == Proto::MT_FILE) ? m_fileName.toUtf8() : QByteArray();
    QByteArray header;
    QDataStream out(&header, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);      // 大端序，与接收端约定一致
    out << m_type << quint32(name.size()) << quint64(m_fileSize);
    header.append(name);

    m_headerSize = header.size();
    m_queued = 0;
    m_acked = 0;
    m_lastEmitted = 0;
    m_transferring = true;

    m_socket->write(header);                  // 先写协议头
    emit sendStarted(m_fileName, m_fileSize);
    pump();
}

/* 流水线发送：每次只写 64KB，缓冲水位到上限就暂停，等 bytesWritten 再续 */
void FileClient::pump()
{
    if (!m_transferring)
        return;
    if (m_socket->bytesToWrite() >= Proto::kChunkSize * 4)
        return;                               // 水位已满

    const qint64 remaining = m_fileSize - m_queued;
    if (remaining > 0) {
        const qint64 want = qMin<qint64>(Proto::kChunkSize, remaining);
        QByteArray block;
        if (m_type == Proto::MT_FILE) {
            block = m_file->read(want);
            if (block.size() != int(want)) {  // 读到的字节数不足：文件读取失败
                finishTransfer(false, QStringLiteral("读取本地文件失败"));
                return;
            }
        } else {
            block = m_pendingText.mid(int(m_queued), int(want));
        }
        m_socket->write(block);
        m_queued += block.size();
    }

    if (m_queued >= m_fileSize && m_socket->bytesToWrite() == 0)
        finishTransfer(true);
}

/* bytesWritten 驱动的流水线：更新进度并继续写下一块 */
void FileClient::onBytesWritten(qint64 bytes)
{
    if (!m_transferring)
        return;
    m_acked += bytes;
    const qint64 payloadAcked = qMax<qint64>(0, m_acked - m_headerSize);
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
    // 先复位再发信号：接收方可能在槽里立即发起下一次传输（重入），
    // 顺序反了会把新传输刚建立的状态清空
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
