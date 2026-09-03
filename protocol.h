#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QtGlobal>
#include <QString>

/**
 * 自定义应用层协议（解决 TCP 粘包 / 拆包问题）
 *
 * TCP 是"字节流"协议，本身没有消息边界：
 *  - 一次 write 的数据可能被拆成多段先后到达（拆包）；
 *  - 连续多次 write 的数据也可能合并成一段同时到达（粘包）。
 * 因此必须自定义协议头，让接收端知道"一段完整数据有多长"，
 * 在接收端通过状态机正确地分包、组包。
 *
 * 报文格式（大端字节序，固定头 16 字节）：
 * +-------------+--------------+--------------+-------------+-------------+
 * | 消息类型 4B  | 文件名长度 4B | 文件名 N B   | 数据大小 8B  | 文件数据 M B |
 * +-------------+--------------+--------------+-------------+-------------+
 *   type(MT_*)    nameLen        name(UTF-8)    fileSize      payload
 *
 *  - 文本消息：nameLen = 0，payload 为 UTF-8 编码的文本；
 *  - 文件消息：nameLen > 0，payload 为文件二进制内容（文本文件/图片等任意文件）。
 */
namespace Proto {

enum MsgType : quint32 {
    MT_TEXT = 0x01,   // 文本消息
    MT_FILE = 0x02    // 文件消息
};

constexpr quint32 kHeaderSize   = 16;                       // 固定协议头长度：4 + 4 + 8
constexpr quint32 kMaxNameLen   = 4096;                     // 文件名长度上限（防御非法报文）
constexpr qint64  kMaxFileSize  = 2LL * 1024 * 1024 * 1024; // 数据大小上限 2GB（防御非法报文）
constexpr qint64  kMaxTextSize  = 1LL * 1024 * 1024;        // 文本消息长度上限 1MB
constexpr qint64  kChunkSize    = 64 * 1024;                // 发送端每次 write 的块大小
constexpr qint64  kProgressStep = 256 * 1024;               // 进度信号最小发射间隔（节流）

// 文件大小格式化（用于日志与进度显示）
inline QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2);
    return QStringLiteral("%1 GB").arg(bytes / 1024.0 / 1024.0 / 1024.0, 0, 'f', 2);
}

} // namespace Proto

#endif // PROTOCOL_H
