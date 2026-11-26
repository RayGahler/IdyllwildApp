#include "mainidyll.h"
#include "repairstatus.h"
#include "preferredcontact.h"
#include "firestore.h"

#include <ui_mainidyll.h>

#include <QDateTime>
#include <QBrush>
#include <QColor>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QSignalBlocker>
#include <QUrlQuery>

#include <utility>

using Firestore::firestoreFields;
using Firestore::inquiryCollectionUrl;
using Firestore::inquiryUrl;
using Firestore::isNotFound;
using Firestore::readStringField;

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
    request.setTransferTimeout(15000);
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
        const auto *selected = list->currentItem();
        const QString selectedId = selected ? selected->data(Qt::UserRole).toString() : QString();
        const QString previousStatus = publicStatusByCode.value(publicInquiry.code);
        bool selectionRestored = false;
        {
            const QSignalBlocker blocker(list);
            list->clear();
            if (isPublic)
                publicStatusByCode.clear();
            for (const auto &document : Firestore::newestInquiriesFirst(documents)) {
                const QString documentId = document.value("name").toString().section('/', -1);
                if (documentId.isEmpty())
                    continue;
                const auto fields = document.value("fields").toObject();
                const QString code = readStringField(fields, "inquiryCode");
                const QString displayCode = code.isEmpty() ? documentId : code;
                auto *item = new QListWidgetItem(displayCode, list);
                item->setData(Qt::UserRole, documentId);
                item->setData(Qt::UserRole + 1, displayCode);
                if (isPublic) {
                    const QString status = RepairStatus::normalized(readStringField(fields, "status"));
                    publicStatusByCode.insert(documentId, status);
                    item->setToolTip(status);
                    const QColor color = RepairStatus::accent(status);
                    if (color.isValid())
                        item->setForeground(color);
                } else {
                    applyPrivateInquiryStatus(item);
                }
                if (documentId == selectedId) {
                    list->setCurrentItem(item);
                    selectionRestored = true;
                }
            }
        }
        if (!selectedId.isEmpty() && !selectionRestored)
            loadCollectionRecord(isPublic, {});

        if (isPublic) {
            const QString latestStatus = publicStatusByCode.value(publicInquiry.code);
            if (publicInquiry.loaded && ui->statusEdit->currentText() == previousStatus
                && latestStatus != previousStatus && ui->statusEdit->findText(latestStatus) >= 0)
                ui->statusEdit->setCurrentText(latestStatus);
            for (int row = 0; row < ui->privateInquiryList->count(); ++row)
                applyPrivateInquiryStatus(ui->privateInquiryList->item(row));
        }
        applyPrivateInquiryFilter();
        updateInvoiceButtons();
        refreshButton->setEnabled(true);
        updateDeleteButton();
    });
}

void MainIdyll::applyPrivateInquiryStatus(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString code = item->data(Qt::UserRole + 1).toString();
    const QString status = publicStatusByCode.value(item->data(Qt::UserRole).toString()).trimmed();
    const QString normalized = status.toLower();
    item->setData(Qt::UserRole + 3, status);
    QString category = tr("Pending Review/In-Repair");
    QString bubbleLabel = tr("In-Progress");
    QColor foreground(QStringLiteral("#755600"));
    QColor background(QStringLiteral("#fff1b8"));
    if (normalized == QStringLiteral("done!")) {
        category = tr("Completed");
        bubbleLabel = tr("Completed");
        foreground = QColor(QStringLiteral("#47311F"));
        background = QColor(QStringLiteral("#E3C9A8"));
    } else if (status == RepairStatus::readyForPickup) {
        category = bubbleLabel = status;
        foreground = QColor(QStringLiteral("#153F62"));
        background = RepairStatus::accent(status);
    } else if (status == RepairStatus::pendingPaymentReview) {
        category = bubbleLabel = status;
        foreground = QColor(QStringLiteral("#5E320A"));
        background = RepairStatus::accent(status);
    } else if (normalized == QStringLiteral("canceled") || normalized == QStringLiteral("on hold")) {
        category = tr("Canceled");
        bubbleLabel = tr("Canceled");
        foreground = QColor(QStringLiteral("#9f2020"));
        background = QColor(QStringLiteral("#f8d9d9"));
    }
    item->setData(Qt::UserRole + 2, category);
    item->setText(code + QLatin1Char('\n') + bubbleLabel);
    item->setTextAlignment(Qt::AlignCenter);
    item->setSizeHint(QSize(192, 52));
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
        const bool visible = filter == tr("All Inquiries") || filter == category;
        item->setHidden(!visible);
    }
    if (const auto *selected = ui->privateInquiryList->currentItem(); selected && selected->isHidden())
        ui->privateInquiryList->setCurrentItem(nullptr);
}

void MainIdyll::loadCollectionRecord(bool isPublic, const QString &code)
{
    auto &editor = isPublic ? publicInquiry : privateInquiry;
    const auto revision = ++editor.revision;
    editor.code = code;
    editor.loaded = false;
    populateInquiryEditor(isPublic, {});
    auto *label = isPublic ? ui->publicSelectedCodeLabel : ui->privateSelectedCodeLabel;
    label->setText(code.isEmpty() ? tr("Select an inquiry") : code);
    updateInquiryActions();
    if (code.isEmpty() || idToken.isEmpty())
        return;

    const QString collection = isPublic ? QStringLiteral("publicInquiries")
                                        : QStringLiteral("privateInquiries");
    const auto session = sessionGeneration;
    QNetworkRequest request(inquiryUrl(collection, code));
    request.setTransferTimeout(15000);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic, code, session, revision] {
        reply->deleteLater();
        auto &editor = isPublic ? publicInquiry : privateInquiry;
        if (session != sessionGeneration || revision != editor.revision)
            return;

        const auto document = QJsonDocument::fromJson(reply->readAll()).object();
        if (isNotFound(reply, document)) {
            showError(isPublic ? tr("Public inquiry %1 was not found.").arg(code)
                               : tr("Private inquiry %1 was not found.").arg(code));
            return;
        }
        const auto error = document.value("error").toObject();
        if (reply->error() != QNetworkReply::NoError || !error.isEmpty()) {
            showError(Firestore::firestoreError(error, reply->errorString()));
            return;
        }

        const auto fields = document.value("fields").toObject();
        populateInquiryEditor(isPublic, fields);
        if (isPublic)
            publicStatusByCode.insert(code, RepairStatus::normalized(readStringField(fields, "status")));
        editor.loaded = true;
        updateInquiryActions();
    });
}

void MainIdyll::populateInquiryEditor(bool isPublic, const QJsonObject &fields)
{
    if (isPublic) {
        ui->publicDeviceEdit->setText(readStringField(fields, "device"));
        ui->publicModelEdit->setText(readStringField(fields, "model"));
        ui->publicMessageEdit->setPlainText(readStringField(fields, "publicMessage"));
        const QString status = RepairStatus::normalized(readStringField(fields, "status"));
        const int index = ui->statusEdit->findText(status, Qt::MatchFixedString);
        if (index < 0 && !status.isEmpty()) {
            ui->statusEdit->addItem(status);
            ui->statusEdit->setCurrentIndex(ui->statusEdit->count() - 1);
        } else {
            ui->statusEdit->setCurrentIndex(qMax(0, index));
        }
        return;
    }

    ui->nameEdit->setText(readStringField(fields, "name"));
    ui->phoneEdit->setText(readStringField(fields, "phone"));
    ui->privateEmailEdit->setText(readStringField(fields, "email"));
    ui->privateDeviceEdit->setText(readStringField(fields, "device"));
    ui->privateModelEdit->setText(readStringField(fields, "model"));
    ui->problemEdit->setPlainText(readStringField(fields, "problem"));
    ui->addressEdit->setPlainText(readStringField(fields, "address"));
    setPreferredContact(*ui->preferredContactEdit, readStringField(fields, "preferredContact"));
}

void MainIdyll::loadInquiry()
{
    const QString code = ui->codeEdit->text().trimmed().toUpper();
    if (code.isEmpty() || idToken.isEmpty())
        return;

    for (auto *list : {ui->publicInquiryList, ui->privateInquiryList}) {
        const QSignalBlocker blocker(list);
        list->setCurrentItem(nullptr);
    }
    ui->codeEdit->setText(code);
    loadCollectionRecord(true, code);
    loadCollectionRecord(false, code);
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
    saveInquiryRecords(true);
}

void MainIdyll::savePrivateInquiry()
{
    saveInquiryRecords(false);
}

void MainIdyll::saveInquiryRecords(bool isPublic)
{
    const auto &editor = isPublic ? publicInquiry : privateInquiry;
    if (idToken.isEmpty() || savingInquiry || !editor.loaded)
        return;

    const QString code = editor.code;
    auto fields = isPublic ? publicFields() : privateFields();
    if (isPublic && fields.value("status").toString() == publicStatusByCode.value(code))
        fields.remove("status");
    const QJsonObject shared{{"device", fields.value("device")}, {"model", fields.value("model")}};
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const auto write = [&code, &now](const QString &collection, QJsonObject data, bool requireExisting) {
        data.insert("inquiryCode", code);
        auto encoded = firestoreFields(data);
        encoded.insert("updatedAt", QJsonObject{{"timestampValue", now}});
        QJsonArray fieldPaths;
        for (auto it = encoded.constBegin(); it != encoded.constEnd(); ++it)
            fieldPaths.append(it.key());

        const QString name = QStringLiteral("%1/%2/%3").arg(Firestore::documentsPath(), collection, code);
        QJsonObject update{
            {"update", QJsonObject{{"name", name}, {"fields", encoded}}},
            {"updateMask", QJsonObject{{"fieldPaths", fieldPaths}}}
        };
        if (requireExisting)
            update.insert("currentDocument", QJsonObject{{"exists", true}});
        return update;
    };

    savingInquiry = true;
    updateInquiryActions();
    ui->statusbar->showMessage(tr("Saving inquiry %1…").arg(code));
    const auto session = sessionGeneration;
    auto *reply = Firestore::commitWrites(network, idToken, {
        write(QStringLiteral("publicInquiries"), isPublic ? fields : shared, isPublic),
        write(QStringLiteral("privateInquiries"), isPublic ? shared : fields, !isPublic)
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, code, isPublic, fields, shared] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        savingInquiry = false;
        const auto result = QJsonDocument::fromJson(reply->readAll()).object();
        const auto error = result.value("error").toObject();
        if (reply->error() != QNetworkReply::NoError || !error.isEmpty()) {
            const bool completionDenied = isPublic && RepairStatus::normalized(fields.value("status").toString()) == QStringLiteral("Done!")
                && error.value("status").toString() == QStringLiteral("PERMISSION_DENIED");
            showError(completionDenied
                ? tr("Could not mark the inquiry Done! Check payment approval in Review invoice payment and verify your staff access.")
                : tr("Could not save inquiry %1. %2").arg(code, Firestore::firestoreError(error, reply->errorString())));
        } else {
            if (isPublic) {
                if (fields.contains("status"))
                    publicStatusByCode.insert(code, fields.value("status").toString());
                for (int row = 0; row < ui->privateInquiryList->count(); ++row) {
                    auto *item = ui->privateInquiryList->item(row);
                    if (item->data(Qt::UserRole).toString() == code)
                        applyPrivateInquiryStatus(item);
                }
                applyPrivateInquiryFilter();
            }

            const auto &other = isPublic ? privateInquiry : publicInquiry;
            if (other.loaded && other.code == code) {
                auto *device = isPublic ? ui->privateDeviceEdit : ui->publicDeviceEdit;
                auto *model = isPublic ? ui->privateModelEdit : ui->publicModelEdit;
                if (!device->isModified())
                    device->setText(shared.value("device").toString());
                if (!model->isModified())
                    model->setText(shared.value("model").toString());
            }
            if (isPublic)
                refreshCollection(QStringLiteral("publicInquiries"));
            ui->statusbar->showMessage(tr("Inquiry %1 saved.").arg(code));
        }
        updateInquiryActions();
    });
}
