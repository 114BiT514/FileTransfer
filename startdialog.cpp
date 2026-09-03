#include "startdialog.h"
#include "ui_startdialog.h"

StartDialog::StartDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::StartDialog)
{
    ui->setupUi(this);

    // 【信号与槽】两个角色按钮：点击后记录所选角色并关闭对话框，
    // 由 main.cpp 根据结果打开对应的服务端/客户端窗口
    connect(ui->btnServer, &QPushButton::clicked, this, &StartDialog::onServerClicked);
    connect(ui->btnClient, &QPushButton::clicked, this, &StartDialog::onClientClicked);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

StartDialog::~StartDialog()
{
    delete ui;
}

void StartDialog::onServerClicked()
{
    m_role = AppRole::Server;
    accept();
}

void StartDialog::onClientClicked()
{
    m_role = AppRole::Client;
    accept();
}
