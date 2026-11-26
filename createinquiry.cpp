#include "mainidyll.h"
#include "repairstatus.h"
#include "preferredcontact.h"
#include "firestore.h"

#include <ui_mainidyll.h>

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QVBoxLayout>

using Firestore::firestoreFields;

void MainIdyll::openCreateInquiry()
{
    if (idToken.isEmpty())
        return;

    QDialog dialog(this);
    dialog.setWindowFlag(Qt::Dialog, true);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setWindowTitle(QStringLiteral("Create inquiry"));
    dialog.resize(560, 680);
    auto *layout = new QVBoxLayout(&dialog);
    auto *codeEdit = new QLineEdit(QStringLiteral("IDY-%1").arg(
        QString::number(QRandomGenerator::global()->bounded(0x1000000), 16)
            .rightJustified(6, QLatin1Char('0')).toUpper()), &dialog);
    codeEdit->setReadOnly(true);
    auto *codeForm = new QFormLayout;
    codeForm->addRow(QStringLiteral("Inquiry code"), codeEdit);
    layout->addLayout(codeForm);

    auto *publicGroup = new QGroupBox(QStringLiteral("Public record"), &dialog);
    auto *publicForm = new QFormLayout(publicGroup);
    auto *device = new QLineEdit(publicGroup);
    auto *model = new QLineEdit(publicGroup);
    auto *status = new QComboBox(publicGroup);
    RepairStatus::addOptions(*status);
    auto *publicMessage = new QPlainTextEdit(publicGroup);
    publicForm->addRow(QStringLiteral("Device"), device);
    publicForm->addRow(QStringLiteral("Model"), model);
    publicForm->addRow(QStringLiteral("Public status"), status);
    publicForm->addRow(QStringLiteral("Public message"), publicMessage);
    layout->addWidget(publicGroup);

    auto *privateGroup = new QGroupBox(QStringLiteral("Private record"), &dialog);
    auto *privateForm = new QFormLayout(privateGroup);
    auto *name = new QLineEdit(privateGroup);
    auto *phone = new QLineEdit(privateGroup);
    auto *email = new QLineEdit(privateGroup);
    auto *privateDevice = new QLineEdit(privateGroup);
    auto *privateModel = new QLineEdit(privateGroup);
    auto *problem = new QPlainTextEdit(privateGroup);
    auto *preferredContact = new QComboBox(privateGroup);
    setPreferredContact(*preferredContact);
    privateForm->addRow(QStringLiteral("Name"), name);
    privateForm->addRow(QStringLiteral("Phone"), phone);
    privateForm->addRow(QStringLiteral("Email"), email);
    privateForm->addRow(QStringLiteral("Device"), privateDevice);
    privateForm->addRow(QStringLiteral("Model"), privateModel);
    privateForm->addRow(QStringLiteral("Problem"), problem);
    privateForm->addRow(QStringLiteral("Preferred contact"), preferredContact);
    layout->addWidget(privateGroup);

    auto *buttons = new QDialogButtonBox(&dialog);
    auto *cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
    auto *postButton = buttons->addButton(QStringLiteral("Post"), QDialogButtonBox::AcceptRole);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(postButton, &QPushButton::clicked, &dialog, [&] {
        if (device->text().trimmed().isEmpty() || model->text().trimmed().isEmpty()
            || name->text().trimmed().isEmpty() || problem->toPlainText().trimmed().isEmpty()) {
            QMessageBox::information(&dialog, QStringLiteral("Missing information"),
                QStringLiteral("Fill in the device, model, name, and problem fields."));
            return;
        }
        postButton->setEnabled(false);
        cancelButton->setEnabled(false);
        postNewInquiry(codeEdit->text(), {
            {"device", device->text().trimmed()},
            {"model", model->text().trimmed()},
            {"status", status->currentText().trimmed()},
            {"publicMessage", publicMessage->toPlainText().trimmed()}
        }, {
            {"name", name->text().trimmed()},
            {"phone", phone->text().trimmed()},
            {"email", email->text().trimmed()},
            {"device", privateDevice->text().trimmed().isEmpty() ? device->text().trimmed() : privateDevice->text().trimmed()},
            {"model", privateModel->text().trimmed().isEmpty() ? model->text().trimmed() : privateModel->text().trimmed()},
            {"problem", problem->toPlainText().trimmed()},
            {"preferredContact", preferredContact->currentText()}
        });
        dialog.accept();
    });
    layout->addWidget(buttons);
    dialog.exec();
}

void MainIdyll::postNewInquiry(const QString &code, const QJsonObject &publicData,
                               const QJsonObject &privateData)
{
    const auto session = sessionGeneration;
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const auto newFields = [&code, &now](QJsonObject data) {
        data.insert("inquiryCode", code);
        QJsonObject fields = firestoreFields(data);
        fields.insert("createdAt", QJsonObject{{"timestampValue", now}});
        fields.insert("updatedAt", QJsonObject{{"timestampValue", now}});
        return fields;
    };

    auto *reply = Firestore::commitWrites(network, idToken, {
        Firestore::createInquiryWrite(QStringLiteral("publicInquiries"), code, newFields(publicData)),
        Firestore::createInquiryWrite(QStringLiteral("privateInquiries"), code, newFields(privateData))
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, code, session] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || response.contains("error")) {
            showError(tr("Could not create inquiry %1. %2").arg(code, Firestore::firestoreError(
                response.value("error").toObject(), reply->errorString())));
            return;
        }

        ui->codeEdit->setText(code);
        ui->statusbar->showMessage(tr("Inquiry %1 created.").arg(code));
    });
}
