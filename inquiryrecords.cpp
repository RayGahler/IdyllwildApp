#include "mainidyll.h"
#include "preferredcontact.h"
#include "firestore.h"

#include <ui_mainidyll.h>

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrlQuery>

using Firestore::firestoreFields;
using Firestore::inquiryCollectionUrl;
using Firestore::inquiryUrl;
using Firestore::isNotFound;
using Firestore::readStringField;
using Firestore::stringField;

void MainIdyll::refreshCollection(const QString &collection)
{
    if (idToken.isEmpty())
        return;
    const bool isPublic = collection == "publicInquiries";
    auto *list = isPublic ? ui->publicInquiryList : ui->privateInquiryList;
    auto *refreshButton = isPublic ? ui->refreshPublicButton : ui->refreshPrivateButton;
    refreshButton->setEnabled(false);
    updateDeleteButton();
    QNetworkRequest request(inquiryCollectionUrl(collection));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, collection, list, refreshButton, isPublic] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (isNotFound(reply, response)) {
            list->clear();
            if (isPublic) {
                publicSelectedCode.clear();
                ui->saveButton->setEnabled(false);
            } else {
                privateSelectedCode.clear();
                ui->savePrivateButton->setEnabled(false);
            }
            refreshButton->setEnabled(true);
            updateDeleteButton();
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            refreshButton->setEnabled(true);
            updateDeleteButton();
            reply->deleteLater();
            return;
        }
        list->clear();
        if (isPublic) {
            publicSelectedCode.clear();
            ui->saveButton->setEnabled(false);
            ui->publicSelectedCodeLabel->setText(QStringLiteral("Select an inquiry"));
        } else {
            privateSelectedCode.clear();
            ui->savePrivateButton->setEnabled(false);
            ui->privateSelectedCodeLabel->setText(QStringLiteral("Select an inquiry"));
        }
        const auto documents = response.value("documents").toArray();
        for (const auto &entry : documents) {
            const auto document = entry.toObject();
            const QString documentId = document.value("name").toString().section('/', -1);
            const auto fields = document.value("fields").toObject();
            const QString code = readStringField(fields, "inquiryCode");
            auto *item = new QListWidgetItem(code.isEmpty() ? documentId : code, list);
            item->setData(Qt::UserRole, documentId);
        }
        refreshButton->setEnabled(true);
        updateDeleteButton();
        reply->deleteLater();
    });
}

void MainIdyll::loadCollectionRecord(const QString &collection, const QString &code)
{
    const bool isPublic = collection == "publicInquiries";
    QNetworkRequest request(inquiryUrl(collection, code));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic, code] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            reply->deleteLater();
            return;
        }
        const auto fields = response.value("fields").toObject();
        if (isPublic) {
            if (publicSelectedCode != code) {
                reply->deleteLater();
                return;
            }
            ui->publicSelectedCodeLabel->setText(code);
            ui->publicDeviceEdit->setText(readStringField(fields, "device"));
            ui->publicModelEdit->setText(readStringField(fields, "model"));
            ui->statusEdit->setText(readStringField(fields, "status"));
            ui->publicMessageEdit->setPlainText(readStringField(fields, "publicMessage"));
            if (!savingPublic)
                ui->saveButton->setEnabled(true);
        } else {
            if (privateSelectedCode != code) {
                reply->deleteLater();
                return;
            }
            ui->privateSelectedCodeLabel->setText(code);
            ui->nameEdit->setText(readStringField(fields, "name"));
            ui->phoneEdit->setText(readStringField(fields, "phone"));
            ui->privateEmailEdit->setText(readStringField(fields, "email"));
            ui->privateDeviceEdit->setText(readStringField(fields, "device"));
            ui->privateModelEdit->setText(readStringField(fields, "model"));
            ui->problemEdit->setPlainText(readStringField(fields, "problem"));
            setPreferredContact(*ui->preferredContactEdit, readStringField(fields, "preferredContact"));
            ui->addressEdit->setPlainText(readStringField(fields, "address"));
            if (!savingPrivate)
                ui->savePrivateButton->setEnabled(true);
        }
        reply->deleteLater();
    });
}

void MainIdyll::loadInquiry()
{
    const QString code = ui->codeEdit->text().trimmed().toUpper();
    if (code.isEmpty() || idToken.isEmpty())
        return;
    loadedCode = code;
    publicLoaded = privateLoaded = false;
    publicExists = privateExists = false;
    ui->codeEdit->setText(code);
    ui->loadButton->setEnabled(false);
    ui->saveButton->setEnabled(false);
    ui->statusbar->showMessage(QStringLiteral("Loading inquiry…"));
    loadPublicRecord();
    loadPrivateRecord();
}

void MainIdyll::loadPublicRecord()
{
    QNetworkRequest request(inquiryUrl("publicInquiries", loadedCode));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (isNotFound(reply, response)) {
            publicExists = false;
        } else if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            publicLoaded = true;
            reply->deleteLater();
            updateButtons();
            return;
        } else {
            const auto fields = response.value("fields").toObject();
            ui->publicDeviceEdit->setText(readStringField(fields, "device"));
            ui->publicModelEdit->setText(readStringField(fields, "model"));
            ui->statusEdit->setText(readStringField(fields, "status"));
            ui->publicMessageEdit->setPlainText(readStringField(fields, "publicMessage"));
            publicExists = true;
        }
        publicLoaded = true;
        reply->deleteLater();
        updateButtons();
    });
}

void MainIdyll::loadPrivateRecord()
{
    QNetworkRequest request(inquiryUrl("privateInquiries", loadedCode));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (isNotFound(reply, response)) {
            privateExists = false;
        } else if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            privateLoaded = true;
            reply->deleteLater();
            updateButtons();
            return;
        } else {
            const auto fields = response.value("fields").toObject();
            ui->nameEdit->setText(readStringField(fields, "name"));
            ui->phoneEdit->setText(readStringField(fields, "phone"));
            ui->privateEmailEdit->setText(readStringField(fields, "email"));
            ui->privateDeviceEdit->setText(readStringField(fields, "device"));
            ui->privateModelEdit->setText(readStringField(fields, "model"));
            ui->problemEdit->setPlainText(readStringField(fields, "problem"));
            setPreferredContact(*ui->preferredContactEdit, readStringField(fields, "preferredContact"));
            ui->addressEdit->setPlainText(readStringField(fields, "address"));
            privateExists = true;
        }
        privateLoaded = true;
        reply->deleteLater();
        updateButtons();
    });
}

QJsonObject MainIdyll::publicFields() const
{
    return {
        {"device", ui->publicDeviceEdit->text().trimmed()},
        {"model", ui->publicModelEdit->text().trimmed()},
        {"status", ui->statusEdit->text()},
        {"publicMessage", ui->publicMessageEdit->toPlainText()}
    };
}

QJsonObject MainIdyll::privateFields() const
{
    return {
        {"name", ui->nameEdit->text()},
        {"phone", ui->phoneEdit->text()},
        {"email", ui->privateEmailEdit->text()},
        {"device", ui->privateDeviceEdit->text().trimmed()},
        {"model", ui->privateModelEdit->text().trimmed()},
        {"problem", ui->problemEdit->toPlainText()},
        {"preferredContact", ui->preferredContactEdit->currentText()},
        {"address", ui->addressEdit->toPlainText()}
    };
}

void MainIdyll::saveInquiry()
{
    if (publicSelectedCode.isEmpty() || idToken.isEmpty())
        return;
    loadedCode = publicSelectedCode;
    savingPublic = savingPrivate = true;
    updateDeleteButton();
    ui->saveButton->setEnabled(false);
    ui->savePrivateButton->setEnabled(false);
    ui->statusbar->showMessage(QStringLiteral("Saving public inquiry and shared device/model fields…"));
    const QString code = publicSelectedCode;
    const QJsonObject shared{{"device", ui->publicDeviceEdit->text().trimmed()},
                             {"model", ui->publicModelEdit->text().trimmed()}};
    saveSingleRecord("publicInquiries", publicFields());
    saveSingleRecord("privateInquiries", shared);
}

void MainIdyll::savePrivateInquiry()
{
    if (privateSelectedCode.isEmpty() || idToken.isEmpty())
        return;
    loadedCode = privateSelectedCode;
    savingPrivate = savingPublic = true;
    updateDeleteButton();
    ui->savePrivateButton->setEnabled(false);
    ui->saveButton->setEnabled(false);
    ui->statusbar->showMessage(QStringLiteral("Saving private inquiry and shared device/model fields…"));
    const QJsonObject fields = privateFields();
    const QJsonObject shared{{"device", fields.value("device")},
                             {"model", fields.value("model")}};
    saveSingleRecord("privateInquiries", fields);
    saveSingleRecord("publicInquiries", shared);
}

void MainIdyll::saveSingleRecord(const QString &collection, const QJsonObject &fields)
{
    const bool isPublic = collection == "publicInquiries";
    const bool exists = true;
    const QString code = isPublic ? publicSelectedCode : privateSelectedCode;
    if (code.isEmpty()) {
        (isPublic ? savingPublic : savingPrivate) = false;
        return;
    }
    QJsonObject documentFields{{"inquiryCode", stringField(code)}};
    for (auto it = fields.begin(); it != fields.end(); ++it)
        documentFields.insert(it.key(), stringField(it.value().toString()));
    QUrl url = inquiryUrl(collection, code);
    QUrlQuery query;
    for (auto it = documentFields.begin(); it != documentFields.end(); ++it)
        query.addQueryItem("updateMask.fieldPaths", it.key());
    query.addQueryItem("updateMask.fieldPaths", "updatedAt");
    if (!exists)
        query.addQueryItem("updateMask.fieldPaths", "createdAt");
    url.setQuery(query);

    QJsonObject encoded = documentFields;
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    encoded.insert("updatedAt", QJsonObject{{"timestampValue", now}});
    if (!exists)
        encoded.insert("createdAt", QJsonObject{{"timestampValue", now}});
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *reply = network.sendCustomRequest(
        request, "PATCH", QJsonDocument(QJsonObject{{"fields", encoded}}).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic, code] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
        } else {
            ui->statusbar->showMessage(QStringLiteral("Saved %1 inquiry %2.")
                                           .arg(isPublic ? QStringLiteral("public") : QStringLiteral("private"), code));
        }
        if (isPublic)
            savingPublic = false;
        else
            savingPrivate = false;
        ui->saveButton->setEnabled(!publicSelectedCode.isEmpty() && !savingPublic);
        ui->savePrivateButton->setEnabled(!privateSelectedCode.isEmpty() && !savingPrivate);
        updateDeleteButton();
        reply->deleteLater();
    });
}

void MainIdyll::saveRecord(const QString &collection, const QJsonObject &fields)
{
    QUrl url = inquiryUrl(collection, loadedCode);
    QUrlQuery query;
    for (auto it = fields.begin(); it != fields.end(); ++it)
        query.addQueryItem("updateMask.fieldPaths", it.key());
    query.addQueryItem("updateMask.fieldPaths", "updatedAt");
    if (!publicExists && collection == "publicInquiries")
        query.addQueryItem("updateMask.fieldPaths", "createdAt");
    if (!privateExists && collection == "privateInquiries")
        query.addQueryItem("updateMask.fieldPaths", "createdAt");
    url.setQuery(query);

    QJsonObject encoded = firestoreFields(fields);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    encoded.insert("updatedAt", QJsonObject{{"timestampValue", now}});
    if ((!publicExists && collection == "publicInquiries")
        || (!privateExists && collection == "privateInquiries"))
        encoded.insert("createdAt", QJsonObject{{"timestampValue", now}});

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *reply = network.sendCustomRequest(
        request, "PATCH", QJsonDocument(QJsonObject{{"fields", encoded}}).toJson());
    const bool isPublic = collection == "publicInquiries";
    connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(
                reply->errorString()));
            ui->statusbar->setProperty("saveFailed", true);
        } else if (isPublic) {
            publicExists = true;
        } else {
            privateExists = true;
        }
        if (isPublic)
            savingPublic = false;
        else
            savingPrivate = false;
        if (!savingPublic && !savingPrivate && !ui->statusbar->property("saveFailed").toBool()) {
            ui->statusbar->showMessage(QStringLiteral("Inquiry saved."));
        }
        if (!savingPublic && !savingPrivate)
            ui->statusbar->setProperty("saveFailed", false);
        updateButtons();
        reply->deleteLater();
    });
}
