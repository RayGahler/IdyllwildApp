#include "mainidyll.h"
#include "preferredcontact.h"
#include "firestore.h"

#include <ui_mainidyll.h>

#include <QDateTime>
#include <QBrush>
#include <QColor>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <utility>

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
    auto *refreshButton = collection == "publicInquiries" ? ui->refreshPublicButton : ui->refreshPrivateButton;
    if (!refreshButton->isEnabled())
        return;

    refreshButton->setEnabled(false);
    updateDeleteButton();
    loadCollectionPage(collection, {}, {});
}

void MainIdyll::loadCollectionPage(const QString &collection, const QString &pageToken, QJsonArray documents)
{
    QUrl url = inquiryCollectionUrl(collection);
    if (!pageToken.isEmpty()) {
        QUrlQuery query;
        query.addQueryItem("pageToken", pageToken);
        url.setQuery(query);
    }
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    const auto session = sessionGeneration;
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, session, collection, documents = std::move(documents)]() mutable {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        const bool isPublic = collection == "publicInquiries";
        auto *refreshButton = isPublic ? ui->refreshPublicButton : ui->refreshPrivateButton;
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError && !isNotFound(reply, response)) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            refreshButton->setEnabled(true);
            updateDeleteButton();
            return;
        }

        for (const auto &entry : response.value("documents").toArray())
            documents.append(entry);
        const QString nextPage = response.value("nextPageToken").toString();
        if (!nextPage.isEmpty()) {
            loadCollectionPage(collection, nextPage, std::move(documents));
            return;
        }

        auto *list = isPublic ? ui->publicInquiryList : ui->privateInquiryList;
        list->clear();
        if (isPublic)
            publicStatusByCode.clear();
        if (isPublic) {
            publicSelectedCode.clear();
            ui->saveButton->setEnabled(false);
            ui->publicSelectedCodeLabel->setText(QStringLiteral("Select an inquiry"));
        } else {
            privateSelectedCode.clear();
            ui->savePrivateButton->setEnabled(false);
            ui->privateSelectedCodeLabel->setText(QStringLiteral("Select an inquiry"));
        }
        for (const auto &document : Firestore::newestInquiriesFirst(documents)) {
            const QString documentId = document.value("name").toString().section('/', -1);
            const auto fields = document.value("fields").toObject();
            const QString code = readStringField(fields, "inquiryCode");
            const QString displayCode = code.isEmpty() ? documentId : code;
            auto *item = new QListWidgetItem(displayCode, list);
            item->setData(Qt::UserRole, documentId);
            item->setData(Qt::UserRole + 1, displayCode);
            if (isPublic) {
                const QString status = readStringField(fields, "status");
                publicStatusByCode.insert(displayCode, status);
                publicStatusByCode.insert(documentId, status);
            } else {
                applyPrivateInquiryStatus(item);
            }
        }
        if (isPublic) {
            for (int row = 0; row < ui->privateInquiryList->count(); ++row)
                applyPrivateInquiryStatus(ui->privateInquiryList->item(row));
        }
        applyPrivateInquiryFilter();
        refreshButton->setEnabled(true);
        updateDeleteButton();
    });
}

void MainIdyll::applyPrivateInquiryStatus(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString code = item->data(Qt::UserRole + 1).toString();
    const QString status = publicStatusByCode.value(code).trimmed();
    const QString normalized = status.toLower();
    QString category = tr("Pending Review/In-Repair");
    QString bubbleLabel = tr("In-Progress");
    QColor foreground(QStringLiteral("#755600"));
    QColor background(QStringLiteral("#fff1b8"));
    if (normalized == QStringLiteral("done!")) {
        category = tr("Completed");
        bubbleLabel = tr("Completed");
        foreground = QColor(QStringLiteral("#47311F"));
        background = QColor(QStringLiteral("#E3C9A8"));
    } else if (normalized == QStringLiteral("canceled") || normalized == QStringLiteral("on hold")) {
        category = tr("Canceled");
        bubbleLabel = tr("Canceled");
        foreground = QColor(QStringLiteral("#9f2020"));
        background = QColor(QStringLiteral("#f8d9d9"));
    }
    item->setData(Qt::UserRole + 2, category);
    item->setText(code + QLatin1Char('\n') + bubbleLabel);
    item->setTextAlignment(Qt::AlignCenter);
    item->setSizeHint(QSize(164, 52));
    item->setForeground(foreground);
    item->setBackground(background);
    item->setToolTip(status.isEmpty()
                         ? tr("Public status unavailable; routed to pending review")
                         : tr("Public status: %1").arg(status));
}

void MainIdyll::applyPrivateInquiryFilter()
{
    const QString filter = ui->privateInquiryFilter->currentText();
    for (int row = 0; row < ui->privateInquiryList->count(); ++row) {
        auto *item = ui->privateInquiryList->item(row);
        const QString category = item->data(Qt::UserRole + 2).toString();
        const bool visible = filter == tr("All Inquries") || filter == category;
        item->setHidden(!visible);
    }
}

void MainIdyll::loadCollectionRecord(const QString &collection, const QString &code)
{
    const bool isPublic = collection == "publicInquiries";
    const auto session = sessionGeneration;
    QNetworkRequest request(inquiryUrl(collection, code));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic, code, session] {
        if (session != sessionGeneration) {
            reply->deleteLater();
            return;
        }
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
            QString status = readStringField(fields, "status");
            if (status.compare(QStringLiteral("Received"), Qt::CaseInsensitive) == 0)
                status = QStringLiteral("Recieved");
            int statusIndex = ui->statusEdit->findText(status, Qt::MatchFixedString);
            ui->statusEdit->setCurrentIndex(statusIndex < 0 ? 0 : statusIndex);
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
    const auto session = sessionGeneration;
    const QString code = loadedCode;
    QNetworkRequest request(inquiryUrl("publicInquiries", code));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, code] {
        if (session != sessionGeneration || code != loadedCode) {
            reply->deleteLater();
            return;
        }
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
            QString status = readStringField(fields, "status");
            if (status.compare(QStringLiteral("Received"), Qt::CaseInsensitive) == 0)
                status = QStringLiteral("Recieved");
            int statusIndex = ui->statusEdit->findText(status, Qt::MatchFixedString);
            ui->statusEdit->setCurrentIndex(statusIndex < 0 ? 0 : statusIndex);
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
    const auto session = sessionGeneration;
    const QString code = loadedCode;
    QNetworkRequest request(inquiryUrl("privateInquiries", code));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, code] {
        if (session != sessionGeneration || code != loadedCode) {
            reply->deleteLater();
            return;
        }
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
        {"status", ui->statusEdit->currentText()},
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
    const auto session = sessionGeneration;
    const bool updatesStatus = isPublic && fields.contains(QStringLiteral("status"));
    const QString savedStatus = fields.value(QStringLiteral("status")).toString();
    connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic, code, updatesStatus, savedStatus, session] {
        if (session != sessionGeneration) {
            reply->deleteLater();
            return;
        }
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
        } else {
            ui->statusbar->showMessage(QStringLiteral("Saved %1 inquiry %2.")
                                           .arg(isPublic ? QStringLiteral("public") : QStringLiteral("private"), code));
            if (updatesStatus) {
                publicStatusByCode.insert(code, savedStatus);
                for (int row = 0; row < ui->privateInquiryList->count(); ++row) {
                    auto *item = ui->privateInquiryList->item(row);
                    if (item->data(Qt::UserRole + 1).toString() == code)
                        applyPrivateInquiryStatus(item);
                }
                applyPrivateInquiryFilter();
            }
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
    const auto session = sessionGeneration;
    const QString code = loadedCode;
    QUrl url = inquiryUrl(collection, code);
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic, session, code] {
        if (session != sessionGeneration || code != loadedCode) {
            reply->deleteLater();
            return;
        }
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
