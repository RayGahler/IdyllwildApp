#include "mainidyll.h"
#include "firestore.h"

#include <ui_mainidyll.h>

#include <QApplication>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QSignalBlocker>
#include <QTabBar>
#include <QUrlQuery>

#include <memory>
#include <utility>

using Firestore::firestoreError;
using Firestore::firestoreFields;
using Firestore::inquiryCollectionUrl;
using Firestore::inquiryUrl;
using Firestore::isNotFound;
using Firestore::readStringField;

void MainIdyll::refreshPendingInquiries(bool background)
{
    if (idToken.isEmpty() || pendingRefreshInProgress || !ui->refreshPendingButton->isEnabled())
        return;

    pendingRefreshInProgress = true;
    if (!refreshToken.isEmpty() && QDateTime::currentSecsSinceEpoch() >= tokenExpiresAt - 60)
        renewPendingSession(background);
    else
        loadPendingPage(background, {}, {});
}

void MainIdyll::loadPendingPage(bool background, const QString &pageToken, QJsonArray documents)
{
    QUrl url = inquiryCollectionUrl("pendingInquiries");
    if (!pageToken.isEmpty()) {
        QUrlQuery query;
        query.addQueryItem("pageToken", pageToken);
        url.setQuery(query);
    }
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    const auto session = sessionGeneration;
    const auto revision = pendingListRevision;
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, session, revision, background, documents = std::move(documents)]() mutable {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        if (revision != pendingListRevision) {
            pendingRefreshInProgress = false;
            return;
        }

        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        const bool missing = isNotFound(reply, response);
        if (!missing && (reply->error() != QNetworkReply::NoError
                         || response.contains("error"))) {
            pendingRefreshInProgress = false;
            showError(tr("Could not check pending inquiries. %1")
                          .arg(firestoreError(response.value("error").toObject(), reply->errorString())));
            return;
        }

        for (const auto &document : response.value("documents").toArray())
            documents.append(document);
        const QString nextPage = response.value("nextPageToken").toString();
        if (!nextPage.isEmpty()) {
            loadPendingPage(background, nextPage, std::move(documents));
            return;
        }

        pendingRefreshInProgress = false;
        if (!ui->refreshPendingButton->isEnabled())
            return;
        applyPendingList(documents);
        if (!background)
            ui->statusbar->showMessage(tr("%1 pending inquiries loaded.").arg(documents.size()));
    });
}

void MainIdyll::applyPendingList(const QJsonArray &documents)
{
    auto *list = ui->pendingList;
    const QSignalBlocker blocker(list);
    QHash<QString, QListWidgetItem *> existing;
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        existing.insert(item->data(Qt::UserRole).toString(), item);
    }

    QSet<QString> currentIds;
    for (const auto &entry : documents) {
        const auto document = entry.toObject();
        const QString id = document.value("name").toString().section('/', -1);
        if (id.isEmpty() || currentIds.contains(id))
            continue;
        currentIds.insert(id);
        const QString code = readStringField(document.value("fields").toObject(), "inquiryCode");
        auto *item = existing.take(id);
        if (!item) {
            item = new QListWidgetItem(list);
            item->setData(Qt::UserRole, id);
        }
        item->setText(code.isEmpty() ? id : code);
    }
    for (auto *item : existing) {
        if (list->currentItem() == item)
            list->setCurrentItem(nullptr);
        delete list->takeItem(list->row(item));
    }
    if (!pendingCode.isEmpty() && !currentIds.contains(pendingCode)) {
        pendingCode.clear();
        selectedPendingData = {};
        ui->pendingSelectedCodeLabel->setText(tr("None"));
        setPendingFieldsEnabled(false);
        ui->promotePendingButton->setEnabled(false);
    }

    unreadPendingIds.intersect(currentIds);
    const auto arrivals = currentIds - knownPendingIds;
    if (pendingBaselineLoaded && !arrivals.isEmpty()) {
        unreadPendingIds.unite(arrivals);
        if (pendingSound.isLoaded())
            pendingSound.play();
        else
            QApplication::beep();
    }
    knownPendingIds = currentIds;
    pendingBaselineLoaded = true;
    updatePendingHighlight();
    updatePendingDeleteButton();
}

void MainIdyll::updatePendingHighlight()
{
    ui->inquiryTabs->tabBar()->setStyleSheet(unreadPendingIds.isEmpty() ? QString() : QStringLiteral(
        "QTabBar::tab:first { background-color: #167044; color: white; }"));
}

void MainIdyll::setPendingFieldsEnabled(bool enabled)
{
    const QList<QWidget *> fields{
        ui->pendingNameEdit, ui->pendingPhoneEdit, ui->pendingEmailEdit,
        ui->pendingDeviceEdit, ui->pendingModelEdit, ui->pendingProblemEdit,
        ui->pendingPreferredEdit, ui->pendingAddressEdit, ui->pendingStatusEdit,
        ui->pendingMessageEdit
    };
    for (auto *field : fields)
        field->setEnabled(enabled);
}

void MainIdyll::loadPendingInquiry(const QString &displayCode)
{
    const auto *item = ui->pendingList->currentItem();
    if (!item || idToken.isEmpty())
        return;
    pendingCode = item->data(Qt::UserRole).toString();
    if (pendingCode.isEmpty())
        pendingCode = displayCode;
    setPendingFieldsEnabled(false);
    ui->promotePendingButton->setEnabled(false);
    QNetworkRequest request(inquiryUrl("pendingInquiries", pendingCode));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    const auto session = sessionGeneration;
    const QString code = pendingCode;
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, displayCode, session, code] {
        if (session != sessionGeneration || code != pendingCode) {
            reply->deleteLater();
            return;
        }
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (isNotFound(reply, response)) {
            showError(QStringLiteral("That pending inquiry no longer exists. Refresh the list."));
            setPendingFieldsEnabled(false);
            ui->promotePendingButton->setEnabled(false);
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            reply->deleteLater();
            return;
        }
        selectedPendingData = response.value("fields").toObject();
        ui->pendingSelectedCodeLabel->setText(displayCode);
        ui->pendingNameEdit->setText(readStringField(selectedPendingData, "name"));
        ui->pendingPhoneEdit->setText(readStringField(selectedPendingData, "phone"));
        ui->pendingEmailEdit->setText(readStringField(selectedPendingData, "email"));
        ui->pendingDeviceEdit->setText(readStringField(selectedPendingData, "device"));
        ui->pendingModelEdit->setText(readStringField(selectedPendingData, "model"));
        ui->pendingProblemEdit->setPlainText(readStringField(selectedPendingData, "problem"));
        ui->pendingPreferredEdit->setText(readStringField(selectedPendingData, "preferredContact"));
        setPendingFieldsEnabled(true);
        ui->promotePendingButton->setEnabled(true);
        reply->deleteLater();
    });
}

void MainIdyll::promotePendingInquiry()
{
    if (pendingCode.isEmpty() || selectedPendingData.isEmpty())
        return;
    const QString device = ui->pendingDeviceEdit->text().trimmed();
    const QString model = ui->pendingModelEdit->text().trimmed();
    const QString name = ui->pendingNameEdit->text().trimmed();
    const QString problem = ui->pendingProblemEdit->toPlainText().trimmed();
    if (device.isEmpty() || model.isEmpty() || name.isEmpty() || problem.isEmpty()
        || ui->pendingAddressEdit->toPlainText().trimmed().isEmpty()) {
        showError(QStringLiteral("Complete the name, device, model, problem, and address before pushing this inquiry."));
        return;
    }

    ui->promotePendingButton->setEnabled(false);
    ui->refreshPendingButton->setEnabled(false);
    ui->pendingList->setEnabled(false);
    updatePendingDeleteButton();
    ui->statusbar->showMessage(QStringLiteral("Pushing pending inquiry…"));
    promotingPublicDone = false;
    promotingPrivateDone = false;

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QJsonObject publicData{
        {"inquiryCode", pendingCode},
        {"device", device},
        {"model", model},
        {"status", ui->pendingStatusEdit->text().trimmed()},
        {"publicMessage", ui->pendingMessageEdit->toPlainText().trimmed()}
    };
    const QJsonObject privateData{
        {"inquiryCode", pendingCode},
        {"name", name},
        {"phone", ui->pendingPhoneEdit->text().trimmed()},
        {"email", ui->pendingEmailEdit->text().trimmed()},
        {"device", device},
        {"model", model},
        {"problem", problem},
        {"preferredContact", ui->pendingPreferredEdit->text().trimmed()},
        {"address", ui->pendingAddressEdit->toPlainText().trimmed()}
    };

    const auto session = sessionGeneration;
    auto remaining = std::make_shared<int>(2);
    auto createDocument = [this, now, session, remaining](const QString &collection, QJsonObject data, bool isPublic) {
        QUrl url = inquiryCollectionUrl(collection);
        QUrlQuery query;
        query.addQueryItem("documentId", pendingCode);
        url.setQuery(query);
        QJsonObject fields = firestoreFields(data);
        fields.insert("createdAt", QJsonObject{{"timestampValue", now}});
        fields.insert("updatedAt", QJsonObject{{"timestampValue", now}});
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        auto *reply = network.sendCustomRequest(
            request, "POST", QJsonDocument(QJsonObject{{"fields", fields}}).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic, session, remaining] {
            reply->deleteLater();
            if (session != sessionGeneration)
                return;
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            if (reply->error() != QNetworkReply::NoError) {
                showError(QStringLiteral("Could not create %1 inquiry: %2")
                              .arg(isPublic ? QStringLiteral("public") : QStringLiteral("private"),
                                   response.value("error").toObject().value("message").toString(
                                       reply->errorString())));
            } else if (isPublic) {
                promotingPublicDone = true;
            } else {
                promotingPrivateDone = true;
            }
            if (--*remaining != 0)
                return;
            if (promotingPublicDone && promotingPrivateDone) {
                finishPendingPromotion();
            } else {
                ui->refreshPendingButton->setEnabled(true);
                ui->pendingList->setEnabled(true);
                updatePendingDeleteButton();
            }
        });
    };
    createDocument("publicInquiries", publicData, true);
    createDocument("privateInquiries", privateData, false);
}

void MainIdyll::finishPendingPromotion()
{
    const auto session = sessionGeneration;
    const QString code = pendingCode;
    QNetworkRequest request(inquiryUrl("pendingInquiries", code));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.sendCustomRequest(request, "DELETE");
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, code] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            showError(QStringLiteral("Inquiry was copied, but pending record could not be removed: %1")
                          .arg(response.value("error").toObject().value("message").toString(
                              reply->errorString())));
        } else {
            ui->statusbar->showMessage(QStringLiteral("Inquiry pushed to public and private collections."));
            removePendingInquiryFromList(code);
        }
        ui->refreshPendingButton->setEnabled(true);
        ui->pendingList->setEnabled(true);
        updatePendingDeleteButton();
    });
}

void MainIdyll::removePendingInquiryFromList(const QString &code)
{
    ++pendingListRevision;
    auto *list = ui->pendingList;
    const QSignalBlocker blocker(list);
    for (int row = list->count() - 1; row >= 0; --row) {
        auto *item = list->item(row);
        if (item->data(Qt::UserRole).toString() != code)
            continue;
        if (list->currentItem() == item)
            list->setCurrentItem(nullptr);
        delete list->takeItem(row);
    }
    if (pendingCode == code) {
        pendingCode.clear();
        selectedPendingData = {};
        ui->pendingSelectedCodeLabel->setText(tr("None"));
        setPendingFieldsEnabled(false);
        ui->promotePendingButton->setEnabled(false);
    }
    knownPendingIds.remove(code);
    unreadPendingIds.remove(code);
    updatePendingHighlight();
}
