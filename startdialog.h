#ifndef STARTDIALOG_H
#define STARTDIALOG_H

#include <QDialog>
#include "settingsdialog.h"

namespace Ui { class StartDialog; }

/**
 * @brief 启动角色选择：决定打开服务端窗口还是客户端窗口。
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
