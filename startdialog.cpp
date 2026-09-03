#include "startdialog.h"
#include "ui_startdialog.h"

StartDialog::StartDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::StartDialog)
{
    ui->setupUi(this);

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
