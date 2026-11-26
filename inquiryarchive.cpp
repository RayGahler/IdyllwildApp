#include "mainidyll.h"
#include "firestore.h"
#include "preferredcontact.h"

#include <ui_mainidyll.h>

#include <QDateTime>
#include <QJsonDocument>
#include <QLocale>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QUuid>

#include <memory>

using Firestore::firestoreError;
using Firestore::inquiryCollectionUrl;
using Firestore::inquiryUrl;
using Firestore::isNotFound;
using Firestore::documentsPath;
using Firestore::readStringField;
using Firestore::stringField;

namespace {
const QString deletedCollection = QStringLiteral("deletedInquiries");
constexpr int archiveIdRole = Qt::UserRole + 1;
constexpr int archiveVersionRole = Qt::UserRole + 2;

}

void MainIdyll::refreshDeletedInquiries(bool preserveSelection)
{
    if (idToken.isEmpty() || !ui->refreshDeletedButton->isEnabled())
        return;

    ui->refreshDeletedButton->setEnabled(false);
    if (!preserveSelection) {
        ui->deletedInquiryList->clear();
        ui->deletedInquiryDetails->clear();
    }
    updatePermanentDeleteButton();
    ui->deletedListStatus->setText(tr("Loading deleted inquiries…"));
    const QString selectedArchiveId = preserveSelection && ui->deletedInquiryList->currentItem()
        ? ui->deletedInquiryList->currentItem()->data(archiveIdRole).toString()
        : QString();
    loadDeletedPage({}, preserveSelection, selectedArchiveId);
}

void MainIdyll::loadDeletedPage(const QString &pageToken, bool preserveSelection,
                                const QString &selectedArchiveId)
{
    QUrl url = inquiryCollectionUrl(deletedCollection);
    QUrlQuery query;
    query.addQueryItem("orderBy", "deletedAt desc");
    if (!pageToken.isEmpty())
        query.addQueryItem("pageToken", pageToken);
    url.setQuery(query);

    const auto session = sessionGeneration;
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, session, pageToken, preserveSelection, selectedArchiveId] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            ui->refreshDeletedButton->setEnabled(true);
            updatePermanentDeleteButton();
            const auto error = response.value("error").toObject();
            if (error.value("status").toString() == "PERMISSION_DENIED"
                || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 403) {
                ui->deletedListStatus->setText(tr("Firestore rules denied access to %1.").arg(deletedCollection));
            } else {
                ui->deletedListStatus->setText(tr("Could not finish loading deleted inquiries."));
            }
            showError(firestoreError(error, reply->errorString()));
            return;
        }

        const QString activeSelectionId = preserveSelection && pageToken.isEmpty()
            && ui->deletedInquiryList->currentItem()
            ? ui->deletedInquiryList->currentItem()->data(archiveIdRole).toString()
            : selectedArchiveId;
        QSignalBlocker selectionBlocker(ui->deletedInquiryList);
        if (pageToken.isEmpty())
            ui->deletedInquiryList->clear();
        for (const auto &entry : response.value("documents").toArray()) {
            const auto document = entry.toObject();
            const auto fields = document.value("fields").toObject();
            const QString archiveId = document.value("name").toString().section('/', -1);
            QString code = readStringField(fields, "inquiryCode");
            if (code.isEmpty())
                code = archiveId;
            const auto timestamp = fields.value("deletedAt").toObject().value("timestampValue").toString();
            const auto deletedAt = QDateTime::fromString(timestamp, Qt::ISODate);
            const QString label = deletedAt.isValid()
                ? tr("%1 · %2").arg(code, QLocale().toString(deletedAt.toLocalTime(), QLocale::ShortFormat))
                : code;
            auto *item = new QListWidgetItem(label, ui->deletedInquiryList);
            item->setData(Qt::UserRole, fields);
            item->setData(archiveIdRole, archiveId);
            item->setData(archiveVersionRole, document.value("updateTime").toString());
        }

        const QString nextPage = response.value("nextPageToken").toString();
        if (!nextPage.isEmpty()) {
            loadDeletedPage(nextPage, preserveSelection, activeSelectionId);
            return;
        }

        if (preserveSelection) {
            QListWidgetItem *selectedItem = nullptr;
            for (int row = 0; row < ui->deletedInquiryList->count(); ++row) {
                auto *item = ui->deletedInquiryList->item(row);
                if (item->data(archiveIdRole).toString() == activeSelectionId) {
                    selectedItem = item;
                    break;
                }
            }
            ui->deletedInquiryList->setCurrentItem(selectedItem);
            if (!selectedItem)
                ui->deletedInquiryDetails->clear();
        }

        ui->refreshDeletedButton->setEnabled(true);
        updatePermanentDeleteButton();
        ui->deletedListStatus->setText(ui->deletedInquiryList->count() == 0
                                          ? tr("No deleted inquiries.")
                                          : QString());
    });
}

void MainIdyll::showDeletedInquiry(QListWidgetItem *item)
{
    updatePermanentDeleteButton();
    ui->deletedInquiryDetails->clear();
    if (!item)
        return;

    const auto fields = item->data(Qt::UserRole).toJsonObject();
    QStringList html{
        QStringLiteral("<html><head><style>"
                       "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;color:#e8efe9;background:#101c15;margin:12px;}"
                       "h1{font-size:21px;margin:0 0 5px;color:#fff;}"
                       "h2{font-size:14px;margin:18px 0 7px;color:#f0b17e;text-transform:uppercase;letter-spacing:1px;}"
                       ".meta{color:#b8c6bb;font-size:12px;margin:0 0 12px;}"
                       "table{width:100%;border-collapse:collapse;}"
                       "td{padding:6px 8px;border-bottom:1px solid #2d4033;vertical-align:top;}"
                       "td:first-child{width:30%;color:#aab9ad;font-weight:600;}"
                       "td:last-child{color:#f2f5f2;white-space:pre-wrap;}"
                       ".empty{color:#91a094;font-style:italic;}"
                       "</style></head><body>")
    };
    const QString inquiryCode = readStringField(fields, "inquiryCode");
    const QString deletedAtRaw = fields.value("deletedAt").toObject()
                                     .value("timestampValue").toString();
    const QDateTime deletedAt = QDateTime::fromString(deletedAtRaw, Qt::ISODate);
    html.append(QStringLiteral("<h1>%1</h1>").arg(
        (inquiryCode.isEmpty() ? item->text() : inquiryCode).toHtmlEscaped()));
    if (deletedAt.isValid()) {
        html.append(QStringLiteral("<p class='meta'>Deleted %1</p>").arg(
            QLocale().toString(deletedAt.toLocalTime(), QLocale::LongFormat).toHtmlEscaped()));
    }

    const auto appendRecord = [&html](const QString &title, const QJsonObject &record) {
        if (record.isEmpty())
            return;
        const auto values = record.value("mapValue").toObject().value("fields").toObject();
        QStringList keys{"name", "phone", "email", "device", "model", "status",
                         "problem", "publicMessage", "preferredContact", "address",
                         "receivedAt", "createdAt"};
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (!keys.contains(it.key()))
                keys.append(it.key());
        }
        html.append(QStringLiteral("<h2>%1</h2><table>").arg(title.toHtmlEscaped()));
        bool hasRows = false;
        for (const QString &key : keys) {
            if (!values.contains(key))
                continue;
            const auto encoded = values.value(key).toObject();
            const auto value = encoded.constBegin();
            if (value == encoded.constEnd())
                continue;
            QString text = value->isString() ? value->toString()
                : value->isObject()
                    ? QString::fromUtf8(QJsonDocument(value->toObject()).toJson(QJsonDocument::Indented))
                    : value->toVariant().toString();
            if (text.trimmed().isEmpty())
                continue;
            QString label;
            for (QChar character : key) {
                if (character.isUpper() && !label.isEmpty())
                    label.append(QLatin1Char(' '));
                label.append(character);
            }
            label[0] = label.at(0).toUpper();
            html.append(QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
                .arg(label.toHtmlEscaped(), text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"))));
            hasRows = true;
        }
        if (!hasRows)
            html.append(QStringLiteral("<tr><td colspan='2' class='empty'>No saved fields</td></tr>"));
        html.append(QStringLiteral("</table>"));
    };
    appendRecord(tr("Pending record"), fields.value("pendingRecord").toObject());
    appendRecord(tr("Public record"), fields.value("publicRecord").toObject());
    appendRecord(tr("Private record"), fields.value("privateRecord").toObject());
    html.append(QStringLiteral("</body></html>"));
    ui->deletedInquiryDetails->setHtml(html.join(QString()));
}

void MainIdyll::updatePermanentDeleteButton()
{
    const auto *item = ui->deletedInquiryList->currentItem();
    ui->permanentlyDeleteButton->setVisible(ui->inquiryTabs->currentWidget() == ui->deletedTab);
    ui->permanentlyDeleteButton->setEnabled(!idToken.isEmpty() && ui->refreshDeletedButton->isEnabled()
        && item && !item->data(archiveIdRole).toString().isEmpty()
        && !item->data(archiveVersionRole).toString().isEmpty());
}

void MainIdyll::permanentlyDeleteInquiry()
{
    const auto *item = ui->deletedInquiryList->currentItem();
    if (!item || !ui->permanentlyDeleteButton->isEnabled())
        return;

    const auto session = sessionGeneration;
    const QString archiveId = item->data(archiveIdRole).toString();
    const QString updateTime = item->data(archiveVersionRole).toString();
    const QString label = item->text();
    QMessageBox confirmation(QMessageBox::Warning, tr("Permanently delete inquiry"),
        tr("Permanently delete %1 from the database? This cannot be undone.").arg(label),
        QMessageBox::Yes | QMessageBox::Cancel, this);
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    if (confirmation.exec() != QMessageBox::Yes || session != sessionGeneration)
        return;

    ui->refreshDeletedButton->setEnabled(false);
    updatePermanentDeleteButton();
    ui->deletedListStatus->setText(tr("Deleting archived inquiry…"));

    const QString name = documentsPath() + "/" + deletedCollection + "/" + archiveId;
    auto *reply = Firestore::commitWrites(network, idToken, {QJsonObject{
        {"delete", name},
        {"currentDocument", QJsonObject{{"updateTime", updateTime}}}
    }});
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, archiveId, label] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        ui->refreshDeletedButton->setEnabled(true);
        if (reply->error() != QNetworkReply::NoError || response.contains("error")) {
            ui->deletedListStatus->setText(tr("Could not permanently delete the inquiry."));
            showError(firestoreError(response.value("error").toObject(), reply->errorString()));
            updatePermanentDeleteButton();
            return;
        }

        auto *list = ui->deletedInquiryList;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(archiveIdRole).toString() == archiveId) {
                delete list->takeItem(row);
                break;
            }
        }
        updatePermanentDeleteButton();
        ui->deletedListStatus->setText(list->count() == 0 ? tr("No deleted inquiries.") : QString());
        ui->statusbar->showMessage(tr("Permanently deleted %1.").arg(label));
    });
}

QListWidgetItem *MainIdyll::selectedInquiry() const
{
    QListWidget *list = nullptr;
    if (ui->inquiryTabs->currentWidget() == ui->publicTab)
        list = ui->publicInquiryList;
    else if (ui->inquiryTabs->currentWidget() == ui->privateTab)
        list = ui->privateInquiryList;

    const auto items = list ? list->selectedItems() : QList<QListWidgetItem *>{};
    return items.size() == 1 ? items.first() : nullptr;
}

void MainIdyll::updateDeleteButton()
{
    ui->deleteInquiryButton->setEnabled(!idToken.isEmpty() && !savingInquiry
                                        && ui->refreshPublicButton->isEnabled()
                                        && ui->refreshPrivateButton->isEnabled()
                                        && selectedInquiry());
}

void MainIdyll::deleteSelectedInquiry()
{
    auto *item = selectedInquiry();
    if (!item || !ui->deleteInquiryButton->isEnabled())
        return;

    archiveInquiry(item->data(Qt::UserRole).toString(), item->text(), false);
}

void MainIdyll::updatePendingDeleteButton()
{
    ui->deletePendingButton->setEnabled(!idToken.isEmpty()
                                       && ui->refreshPendingButton->isEnabled()
                                       && ui->pendingList->currentItem());
}

void MainIdyll::deletePendingInquiry()
{
    const auto *item = ui->pendingList->currentItem();
    if (!item || !ui->deletePendingButton->isEnabled())
        return;
    archiveInquiry(item->data(Qt::UserRole).toString(), item->text(), true);
}

void MainIdyll::archiveInquiry(const QString &code, const QString &label, bool fromPending)
{
    if (code.isEmpty())
        return;

    const auto choice = QMessageBox::question(
        this, tr("Delete inquiry"),
        (fromPending
             ? tr("Move pending inquiry %1 to Deleted Inquires?")
             : tr("Move inquiry %1 from the public and private collections to Deleted Inquires?")).arg(label),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice != QMessageBox::Yes)
        return;

    ui->centralwidget->setEnabled(false);
    ui->statusbar->showMessage(tr("Archiving inquiry %1…").arg(label));

    const QString path = documentsPath();
    const auto fail = [this, label](const QString &message) {
        ui->centralwidget->setEnabled(true);
        updateDeleteButton();
        const QString title = tr("Could not archive inquiry %1").arg(label);
        ui->statusbar->showMessage(title);
        QMessageBox dialog(QMessageBox::Warning, title, message, QMessageBox::Ok, this);
        dialog.setTextFormat(Qt::PlainText);
        dialog.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        dialog.exec();
    };

    struct ArchiveRead {
        QJsonArray writes;
        QJsonObject fields;
        int pending = 0;
        bool failed = false;
        bool found = false;
    };
    auto archive = std::make_shared<ArchiveRead>();
    archive->pending = fromPending ? 1 : 2;
    archive->fields = {
        {"inquiryCode", stringField(label)},
        {"documentId", stringField(code)},
        {"deletedAt", QJsonObject{{"timestampValue", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}}
    };

    const auto read = [this, archive, fail, path, code, label, fromPending](
                          const QString &collection, const QString &field) {
        const QString name = path + "/" + collection + "/" + code;
        QNetworkRequest request(inquiryUrl(collection, code));
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
        auto *reply = network.get(request);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, archive, fail, path, collection, name, field, code, label, fromPending] {
            const auto document = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            if (archive->failed)
                return;

            QJsonObject precondition;
            if (isNotFound(reply, document)) {
                precondition.insert("exists", false);
            } else {
                const QString updateTime = document.value("updateTime").toString();
                if (reply->error() != QNetworkReply::NoError || updateTime.isEmpty()
                    || document.value("name").toString() != name) {
                    archive->failed = true;
                    const auto error = document.value("error").toObject();
                    const QString fallback = reply->error() == QNetworkReply::NoError
                        ? tr("Firestore returned an incomplete document.")
                        : tr("HTTP %1 — %2").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                                               .arg(reply->errorString());
                    fail(tr("Reading %1 failed.\n\n%2").arg(collection, firestoreError(error, fallback)));
                    return;
                }
                archive->found = true;
                precondition.insert("updateTime", updateTime);
                archive->fields.insert(field, QJsonObject{
                    {"mapValue", QJsonObject{{"fields", document.value("fields").toObject()}}}
                });
            }
            archive->writes.append(QJsonObject{{"delete", name}, {"currentDocument", precondition}});
            if (--archive->pending != 0)
                return;
            if (!archive->found) {
                fail(tr("The inquiry no longer exists."));
                return;
            }

            const QString archiveName = path + "/" + deletedCollection + "/"
                                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
            archive->writes.prepend(QJsonObject{
                {"update", QJsonObject{{"name", archiveName}, {"fields", archive->fields}}},
                {"currentDocument", QJsonObject{{"exists", false}}}
            });
            auto *commit = Firestore::commitWrites(network, idToken, archive->writes);
            connect(commit, &QNetworkReply::finished, this, [this, commit, fail, code, label, fromPending] {
                const auto result = QJsonDocument::fromJson(commit->readAll()).object();
                commit->deleteLater();
                const auto commitError = result.value("error").toObject();
                if (commit->error() != QNetworkReply::NoError || !commitError.isEmpty()) {
                    QString message = tr("Saving the archive and deleting the source records failed.\n\n%1")
                                          .arg(firestoreError(commitError, commit->errorString()));
                    if (commitError.value("status").toString() == "PERMISSION_DENIED"
                        || commit->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 403) {
                        message += tr("\n\nCheck that the Firestore rules allow your account to create "
                                      "documents in %1 and delete the source inquiry.")
                                           .arg(deletedCollection);
                    }
                    fail(message);
                    return;
                }

                if (fromPending) {
                    removePendingInquiryFromList(code);
                } else {
                    if (publicInquiry.code == code)
                        loadCollectionRecord(true, {});
                    if (privateInquiry.code == code)
                        loadCollectionRecord(false, {});
                    for (auto *list : {ui->publicInquiryList, ui->privateInquiryList}) {
                        const QSignalBlocker blocker(list);
                        for (int row = list->count() - 1; row >= 0; --row) {
                            if (list->item(row)->data(Qt::UserRole).toString() == code)
                                delete list->takeItem(row);
                        }
                    }
                }

                ui->centralwidget->setEnabled(true);
                updateDeleteButton();
                updatePendingDeleteButton();
                ui->statusbar->showMessage(tr("Inquiry %1 moved to Deleted Inquires.").arg(label));
            });
        });
    };
    if (fromPending) {
        read("pendingInquiries", "pendingRecord");
    } else {
        read("publicInquiries", "publicRecord");
        read("privateInquiries", "privateRecord");
    }
}
