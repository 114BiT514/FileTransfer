#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);

    // 【信号与槽】"浏览..."打开目录选择对话框；按钮盒确定/取消各接一个槽
    connect(ui->btnSaveDirBrowse, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseClicked);
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &SettingsDialog::onOkClicked);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    // 【事件过滤器】把过滤器安装到 IP 输入框与目录输入框上：
    // 拦截它们的 FocusIn 事件，让获得焦点时文字自动全选，方便直接覆盖输入。
    ui->cbIp->lineEdit()->installEventFilter(this);
    ui->leSaveDir->installEventFilter(this);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

/* 按角色裁剪界面：
 *  - 服务端：不显示"服务器 IP"，端口文案改为"监听端口"；
 *  - 客户端：不显示"接收保存目录"（客户端只发送、不保存）。
 * 隐藏该行的标签与输入控件后，QFormLayout 会自动收起整行。 */
void SettingsDialog::setRole(AppRole role)
{
    m_role = role;
    if (role == AppRole::Server) {
        setWindowTitle(QStringLiteral("服务端设置"));
        ui->lblIp->setVisible(false);
        ui->cbIp->setVisible(false);
        ui->lblPort->setText(QStringLiteral("监听端口："));
    } else {
        setWindowTitle(QStringLiteral("连接设置"));
        ui->lblSaveDir->setVisible(false);
        ui->leSaveDir->setVisible(false);
        ui->btnSaveDirBrowse->setVisible(false);
    }
}

/* 事件过滤器：watched 控件收到事件时先经过本函数。
 * 这里只处理"获得焦点"事件并返回 false（继续交给控件自己处理）。 */
bool SettingsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        if (auto *edit = qobject_cast<QLineEdit *>(watched))
            edit->selectAll();
    }
    return QDialog::eventFilter(watched, event);
}

void SettingsDialog::setSettings(const AppSettings &s)
{
    ui->cbIp->setCurrentText(s.host);
    ui->spPort->setValue(int(s.port));
    ui->leSaveDir->setText(s.saveDir);
}

AppSettings SettingsDialog::settings() const
{
    AppSettings s;
    s.host = ui->cbIp->currentText().trimmed();
    s.port = quint16(ui->spPort->value());
    s.saveDir = ui->leSaveDir->text().trimmed();
    return s;
}

void SettingsDialog::onBrowseClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择接收文件保存目录"), ui->leSaveDir->text());
    if (!dir.isEmpty())
        ui->leSaveDir->setText(dir);
}

void SettingsDialog::onOkClicked()
{
    const AppSettings s = settings();
    // 输入校验：不合法给出提示，对话框不关闭
    if (s.host.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("服务器 IP 不能为空。"));
        return;
    }
    if (s.saveDir.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("接收保存目录不能为空。"));
        return;
    }
    // 【跨窗口通信】通过自定义信号把设置参数传递给主窗口，然后关闭对话框
    emit settingsApplied(s);
    accept();
}
