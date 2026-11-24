#include "mainidyll.h"
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
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrlQuery>
#include <QVBoxLayout>

using Firestore::firestoreFields;
using Firestore::inquiryCollectionUrl;

void MainIdyll::openCreateInquiry()
{
    if (idToken.isEmpty())
        return;

    QDialog dialog(this);
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
    status->addItems({QStringLiteral("Recieved"), QStringLiteral("In Repair"), QStringLiteral("Done!"),
                      QStringLiteral("Canceled"), QStringLiteral("On Hold")});
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
    if (!publicExists && publicData.contains("inquiryCode")) {
        // Preserve the current helper's create semantics for new inquiry documents.
    }
    auto postRecord = [this, &code, &now, session](const QString &collection, QJsonObject data,
                                          const QString &label) {
        data.insert("inquiryCode", code);
        QJsonObject fields = firestoreFields(data);
        fields.insert("createdAt", QJsonObject{{"timestampValue", now}});
        fields.insert("updatedAt", QJsonObject{{"timestampValue", now}});
        QUrl collectionUrl = inquiryCollectionUrl(collection);
        QUrlQuery query;
        query.addQueryItem("documentId", code);
        collectionUrl.setQuery(query);
        QNetworkRequest request(collectionUrl);
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        auto *reply = network.sendCustomRequest(
            request, "POST", QJsonDocument(QJsonObject{{"fields", fields}}).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, reply, label, code, session] {
            if (session != sessionGeneration) {
                reply->deleteLater();
                return;
            }
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            if (reply->error() != QNetworkReply::NoError) {
                showError(QStringLiteral("%1 record failed: %2").arg(
                    label, response.value("error").toObject().value("message").toString(
                        reply->errorString())));
            } else {
                ui->statusbar->showMessage(QStringLiteral("%1 record posted for %2.")
                                               .arg(label, code));
                ui->codeEdit->setText(code);
            }
            reply->deleteLater();
        });
    };
    postRecord("publicInquiries", publicData, QStringLiteral("Public"));
    postRecord("privateInquiries", privateData, QStringLiteral("Private"));
}
