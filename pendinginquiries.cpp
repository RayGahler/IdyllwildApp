#include "mainidyll.h"
#include "preferredcontact.h"
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

#include <utility>

using Firestore::firestoreError;
using Firestore::firestoreFields;
using Firestore::documentsPath;
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
    request.setTransferTimeout(15000);
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

    auto *selected = list->currentItem();
    QSet<QString> currentIds;
    int targetRow = 0;
    for (const auto &document : Firestore::newestInquiriesFirst(documents)) {
        const QString id = document.value("name").toString().section('/', -1);
        if (id.isEmpty() || currentIds.contains(id))
            continue;
        currentIds.insert(id);
        const QString code = readStringField(document.value("fields").toObject(), "inquiryCode");
        auto *item = existing.take(id);
        if (!item) {
            item = new QListWidgetItem;
            item->setData(Qt::UserRole, id);
            list->insertItem(targetRow, item);
        } else if (list->row(item) != targetRow) {
            list->insertItem(targetRow, list->takeItem(list->row(item)));
        }
        item->setText(code.isEmpty() ? id : code);
        ++targetRow;
    }
    for (auto *item : existing) {
        if (selected == item)
            selected = nullptr;
        delete list->takeItem(list->row(item));
    }
    if (list->currentItem() != selected || (selected && !selected->isSelected()))
        list->setCurrentItem(selected);
    if (!pendingCode.isEmpty() && !currentIds.contains(pendingCode)) {
        clearPendingEditor();
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
        "QTabBar::tab:first { background-color: #CC7E3E; color: #142A1E; }"));
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

void MainIdyll::clearPendingEditor()
{
    ++pendingRecordRevision;
    pendingCode.clear();
    selectedPendingUpdateTime.clear();
    selectedPendingReceivedAt.clear();
    ui->pendingSelectedCodeLabel->setText(tr("None"));
    populatePendingEditor({});
    setPendingFieldsEnabled(false);
    ui->promotePendingButton->setEnabled(false);
}

void MainIdyll::populatePendingEditor(const QJsonObject &fields)
{
    ui->pendingNameEdit->setText(readStringField(fields, "name"));
    ui->pendingPhoneEdit->setText(readStringField(fields, "phone"));
    ui->pendingEmailEdit->setText(readStringField(fields, "email"));
    ui->pendingDeviceEdit->setText(readStringField(fields, "device"));
    ui->pendingModelEdit->setText(readStringField(fields, "model"));
    ui->pendingProblemEdit->setPlainText(readStringField(fields, "problem"));
    ui->pendingAddressEdit->setPlainText(readStringField(fields, "address"));
    ui->pendingMessageEdit->setPlainText(readStringField(fields, "publicMessage"));
    setPreferredContact(*ui->pendingPreferredEdit, readStringField(fields, "preferredContact"));
    QString status = readStringField(fields, "status");
    if (status.compare(QStringLiteral("Received"), Qt::CaseInsensitive) == 0)
        status = QStringLiteral("Recieved");
    const int index = ui->pendingStatusEdit->findText(status, Qt::MatchFixedString);
    ui->pendingStatusEdit->setCurrentIndex(qMax(0, index));
}

void MainIdyll::loadPendingInquiry()
{
    clearPendingEditor();
    const auto *item = ui->pendingList->currentItem();
    if (!item || idToken.isEmpty())
        return;

    pendingCode = item->data(Qt::UserRole).toString();
    ui->pendingSelectedCodeLabel->setText(item->text());
    QNetworkRequest request(inquiryUrl("pendingInquiries", pendingCode));
    request.setTransferTimeout(15000);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    const auto session = sessionGeneration;
    const auto revision = pendingRecordRevision;
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, revision] {
        reply->deleteLater();
        if (session != sessionGeneration || revision != pendingRecordRevision)
            return;

        const auto document = QJsonDocument::fromJson(reply->readAll()).object();
        if (isNotFound(reply, document)) {
            showError(tr("That pending inquiry no longer exists. Refresh the list."));
            return;
        }
        const auto error = document.value("error").toObject();
        if (reply->error() != QNetworkReply::NoError || !error.isEmpty()) {
            showError(firestoreError(error, reply->errorString()));
            return;
        }
        selectedPendingUpdateTime = document.value("updateTime").toString();
        selectedPendingReceivedAt = Firestore::inquiryReceivedAt(document).toUTC().toString(Qt::ISODateWithMs);
        populatePendingEditor(document.value("fields").toObject());
        setPendingFieldsEnabled(true);
        ui->promotePendingButton->setEnabled(!selectedPendingUpdateTime.isEmpty());
    });
}

void MainIdyll::promotePendingInquiry()
{
    if (idToken.isEmpty() || pendingCode.isEmpty() || selectedPendingUpdateTime.isEmpty()
        || !ui->promotePendingButton->isEnabled())
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

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QJsonObject publicData{
        {"inquiryCode", pendingCode},
        {"device", device},
        {"model", model},
        {"status", ui->pendingStatusEdit->currentText().trimmed()},
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
        {"preferredContact", ui->pendingPreferredEdit->currentText()},
        {"address", ui->pendingAddressEdit->toPlainText().trimmed()}
    };

    const QString code = pendingCode;
    const auto session = sessionGeneration;
    const auto acceptedFields = [this, &now](const QJsonObject &data) {
        auto fields = firestoreFields(data);
        fields.insert("createdAt", QJsonObject{{"timestampValue", now}});
        if (!selectedPendingReceivedAt.isEmpty())
            fields.insert("receivedAt", QJsonObject{{"timestampValue", selectedPendingReceivedAt}});
        fields.insert("updatedAt", QJsonObject{{"timestampValue", now}});
        return fields;
    };
    auto *reply = Firestore::commitWrites(network, idToken, {
        Firestore::createInquiryWrite(QStringLiteral("publicInquiries"), code, acceptedFields(publicData)),
        Firestore::createInquiryWrite(QStringLiteral("privateInquiries"), code, acceptedFields(privateData)),
        QJsonObject{
            {"delete", QStringLiteral("%1/pendingInquiries/%2").arg(documentsPath(), code)},
            {"currentDocument", QJsonObject{{"updateTime", selectedPendingUpdateTime}}}
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, code] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        ui->refreshPendingButton->setEnabled(true);
        ui->pendingList->setEnabled(true);
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        const auto error = response.value("error").toObject();
        if (reply->error() != QNetworkReply::NoError || !error.isEmpty()) {
            ui->promotePendingButton->setEnabled(pendingCode == code);
            showError(tr("Could not accept inquiry %1. %2")
                          .arg(code, firestoreError(error, reply->errorString())));
        } else {
            removePendingInquiryFromList(code);
            refreshCollection(QStringLiteral("publicInquiries"));
            refreshCollection(QStringLiteral("privateInquiries"));
            ui->statusbar->showMessage(tr("Inquiry %1 accepted.").arg(code));
        }
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
        clearPendingEditor();
    }
    knownPendingIds.remove(code);
    unreadPendingIds.remove(code);
    updatePendingHighlight();
}
