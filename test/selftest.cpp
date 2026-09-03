/* 协议自测程序（无界面）：
 * 1. 服务端监听 127.0.0.1:18888，并把每个套接字的读缓冲限制为 16KB，
 *    故意制造"分包"（一次 write 的数据被拆成多次 readyRead 到达）；
 * 2. 客户端连接后依次发送：文本消息 -> 3MB 大文件 -> 紧跟的小文本文件
 *    （连续发送制造"粘包"压力）；
 * 3. 对收到的文件做 MD5 与原始数据比对，完全一致则输出 SELFTEST PASS。
 */
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTimer>
#include <cstdio>

#include "../fileclient.h"
#include "../fileserver.h"
#include "../protocol.h"

/* 生成确定性伪随机测试数据 */
static QByteArray makeTestData(qint64 size)
{
    QByteArray data(int(size), Qt::Uninitialized);
    quint32 seed = 123456789;
    for (int i = 0; i < data.size(); ++i) {
        seed = seed * 1103515245u + 12345u;
        data[i] = char((seed >> 16) & 0xFF);
    }
    return data;
}

static bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString cwd = QCoreApplication::applicationDirPath();
    const QString recvDir = cwd + QStringLiteral("/recv");
    QDir().mkpath(recvDir);

    // 准备测试数据
    const QByteArray bigData = makeTestData(3 * 1024 * 1024 + 12345);
    const QByteArray smallData = QByteArray("Qt 文件传输自测数据 line\n").repeated(200);
    writeFile(cwd + QStringLiteral("/big_test.bin"), bigData);
    writeFile(cwd + QStringLiteral("/small_test.txt"), smallData);
    const QByteArray bigHash = QCryptographicHash::hash(bigData, QCryptographicHash::Md5);
    const QByteArray smallHash = QCryptographicHash::hash(smallData, QCryptographicHash::Md5);
    const QString textMsg = QStringLiteral("你好，TCP 粘包测试！Hello Qt! 123");

    FileServer server;
    server.setSaveDir(recvDir);
    server.setClientReadBufferSize(16 * 1024);   // 强制小包读取 -> 分包压力测试

    FileClient client;

    bool failed = false;
    bool textOk = false;
    int finished = 0;
    QStringList recvNames;
    QList<QByteArray> recvHashes;

    QObject::connect(&server, &FileServer::textReceived,
                     [&](const QString &, const QString &t) {
        if (t == textMsg) {
            textOk = true;
            std::printf("text message OK\n");
        } else {
            failed = true;
            std::printf("text MISMATCH!\n");
        }
    });
    QObject::connect(&server, &FileServer::recvFinished,
                     [&](const QString &name, qint64, const QString &path) {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        recvNames << name;
        recvHashes << QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5);
        std::printf("received: %s\n", qPrintable(name));
        ++finished;
    });
    QObject::connect(&server, &FileServer::recvFailed, [&](const QString &r) {
        failed = true;
        std::printf("recv FAILED: %s\n", qPrintable(r));
    });
    // 过程日志（调试用）
    QObject::connect(&server, &FileServer::logInfo, [](const QString &m) {
        std::printf("[server] %s\n", qPrintable(m));
    });
    QObject::connect(&server, &FileServer::recvProgress,
                     [](const QString &name, qint64 rec, qint64 total) {
        if (rec == total || rec % (1024 * 1024) < 256 * 1024)
            std::printf("[server progress] %s %lld/%lld\n",
                        qPrintable(name), (long long)rec, (long long)total);
    });
    QObject::connect(&client, &FileClient::logError, [](const QString &m) {
        std::printf("[client error] %s\n", qPrintable(m));
    });
    QObject::connect(&client, &FileClient::sendProgress,
                     [](const QString &name, qint64 s, qint64 total) {
        if (s == total)
            std::printf("[client progress] %s %lld/%lld\n",
                        qPrintable(name), (long long)s, (long long)total);
    });
    QObject::connect(&client, &FileClient::sendFailed, [&](const QString &r) {
        failed = true;
        std::printf("send FAILED: %s\n", qPrintable(r));
    });

    // 步骤驱动：连接成功 -> 发文本 -> 文本完成 -> 发大文件 -> 完成 -> 发小文件
    QObject::connect(&client, &FileClient::stateChanged,
                     [&](bool up) {
        if (!up)
            return;
        std::printf("connected\n");
        client.sendText(textMsg);
    });
    QObject::connect(&client, &FileClient::sendFinished,
                     [&](const QString &name, qint64) {
        if (name == QStringLiteral("(文本消息)"))
            client.sendFile(cwd + QStringLiteral("/big_test.bin"));
        else if (name == QStringLiteral("big_test.bin"))
            client.sendFile(cwd + QStringLiteral("/small_test.txt"));
    });

    // 结果判定：2 个文件哈希一致 + 文本一致 -> PASS
    auto check = [&] {
        if (failed) {
            std::printf("SELFTEST FAIL\n");
            app.exit(1);
            return;
        }
        if (finished == 2 && textOk) {
            const bool ok = recvNames.size() == 2
                    && recvNames[0] == QStringLiteral("big_test.bin")
                    && recvHashes[0] == bigHash
                    && recvNames[1] == QStringLiteral("small_test.txt")
                    && recvHashes[1] == smallHash;
            std::printf("%s\n", ok ? "SELFTEST PASS" : "SELFTEST FAIL (hash mismatch)");
            app.exit(ok ? 0 : 1);
        }
    };
    QObject::connect(&server, &FileServer::recvFinished, check);

    // 超时保护
    QTimer::singleShot(30000, [&] {
        std::printf("SELFTEST FAIL (timeout)\n");
        app.exit(2);
    });

    // 启动：服务端监听 -> 客户端连接
    QTimer::singleShot(0, [&] {
        if (!server.startListen(18888)) {
            std::printf("SELFTEST FAIL (listen)\n");
            app.exit(1);
            return;
        }
        client.connectToServer(QStringLiteral("127.0.0.1"), 18888);
    });

    return app.exec();
}
