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

    connect(ui->btnSaveDirBrowse, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseClicked);
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &SettingsDialog::onOkClicked);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    // 事件过滤器：输入框获得焦点时自动全选
    ui->cbIp->lineEdit()->installEventFilter(this);
    ui->leSaveDir->installEventFilter(this);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

/* 按角色隐藏无关字段（QFormLayout 中隐藏整行控件后会自动收起） */
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

/* 事件过滤器：处理"获得焦点"事件，其余放行 */
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
    // 按角色校验可见字段，被隐藏的字段不参与校验
    if (m_role == AppRole::Client) {
        if (s.host.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("服务器 IP 不能为空。"));
            return;
        }
    } else {
        if (s.saveDir.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("接收保存目录不能为空。"));
            return;
        }
    }
    // 跨窗口通信：把设置参数发给主窗口
    emit settingsApplied(s);
    accept();
}
