#ifndef STARTDIALOG_H
#define STARTDIALOG_H

#include <QDialog>
#include "settingsdialog.h"

namespace Ui { class StartDialog; }

/**
 * @brief 启动角色选择对话框：程序运行时先选择本次以服务端还是客户端身份运行，
 *        随后分别打开 ServerWindow（服务端窗口）或 ClientWindow（客户端窗口）。
 */
class StartDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StartDialog(QWidget *parent = nullptr);
    ~StartDialog() override;

    AppRole selectedRole() const { return m_role; }   // exec() 返回后读取所选角色

private slots:
    void onServerClicked();   // "作为服务端启动"
    void onClientClicked();   // "作为客户端启动"

private:
    Ui::StartDialog *ui;
    AppRole m_role = AppRole::Server;
};

#endif // STARTDIALOG_H
