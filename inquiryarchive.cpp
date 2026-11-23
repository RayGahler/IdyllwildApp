#include "mainidyll.h"
#include "firestore.h"

#include <ui_mainidyll.h>

#include <QDateTime>
#include <QJsonDocument>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QUuid>

#include <memory>

using Firestore::firestoreError;
using Firestore::inquiryCollectionUrl;
using Firestore::inquiryUrl;
using Firestore::isNotFound;
using Firestore::projectId;
using Firestore::readStringField;
using Firestore::stringField;

namespace {
const QString deletedCollection = QStringLiteral("deletedInquiries");
}

void MainIdyll::refreshDeletedInquiries()
{
    if (idToken.isEmpty() || !ui->refreshDeletedButton->isEnabled())
        return;

    ui->refreshDeletedButton->setEnabled(false);
    ui->deletedInquiryList->clear();
    ui->deletedInquiryDetails->clear();
    ui->deletedListStatus->setText(tr("Loading deleted inquiries…"));
    loadDeletedPage({});
}

void MainIdyll::loadDeletedPage(const QString &pageToken)
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, session] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            ui->refreshDeletedButton->setEnabled(true);
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

        for (const auto &entry : response.value("documents").toArray()) {
            const auto document = entry.toObject();
            const auto fields = document.value("fields").toObject();
            QString code = readStringField(fields, "inquiryCode");
            if (code.isEmpty())
                code = document.value("name").toString().section('/', -1);
            const auto timestamp = fields.value("deletedAt").toObject().value("timestampValue").toString();
            const auto deletedAt = QDateTime::fromString(timestamp, Qt::ISODate);
            const QString label = deletedAt.isValid()
                ? tr("%1 · %2").arg(code, QLocale().toString(deletedAt.toLocalTime(), QLocale::ShortFormat))
                : code;
            auto *item = new QListWidgetItem(label, ui->deletedInquiryList);
            item->setData(Qt::UserRole, fields);
        }

        const QString nextPage = response.value("nextPageToken").toString();
        if (!nextPage.isEmpty()) {
            loadDeletedPage(nextPage);
            return;
        }

        ui->refreshDeletedButton->setEnabled(true);
        ui->deletedListStatus->setText(ui->deletedInquiryList->count() == 0
                                          ? tr("No deleted inquiries.")
                                          : QString());
    });
}

void MainIdyll::showDeletedInquiry(QListWidgetItem *item)
{
    ui->deletedInquiryDetails->clear();
    if (!item)
        return;

    const auto fields = item->data(Qt::UserRole).toJsonObject();
    QStringList sections{item->text()};
    const auto appendRecord = [&sections](const QString &title, const QJsonObject &record) {
        if (record.isEmpty())
            return;
        QStringList lines{title};
        const auto values = record.value("mapValue").toObject().value("fields").toObject();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            const auto encoded = it.value().toObject();
            const auto value = encoded.constBegin();
            if (value == encoded.constEnd())
                continue;

            QString text;
            if (value->isString())
                text = value->toString();
            else if (value->isObject())
                text = QString::fromUtf8(QJsonDocument(value->toObject()).toJson(QJsonDocument::Compact));
            else
                text = value->toVariant().toString();
            lines.append(it.key() + ": " + text);
        }
        sections.append(lines.join('\n'));
    };
    appendRecord(tr("Pending record"), fields.value("pendingRecord").toObject());
    appendRecord(tr("Public record"), fields.value("publicRecord").toObject());
    appendRecord(tr("Private record"), fields.value("privateRecord").toObject());
    ui->deletedInquiryDetails->setPlainText(sections.join("\n\n"));
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
    ui->deleteInquiryButton->setEnabled(!idToken.isEmpty() && !savingPublic && !savingPrivate
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

    const QString documentsPath = QStringLiteral("projects/%1/databases/(default)/documents").arg(projectId);
    const auto post = [this, documentsPath](const QString &method, const QJsonObject &body) {
        QNetworkRequest request(QUrl("https://firestore.googleapis.com/v1/" + documentsPath + method));
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        return network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    };
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

    const auto read = [this, archive, post, fail, documentsPath, code, label, fromPending](
                          const QString &collection, const QString &field) {
        const QString name = documentsPath + "/" + collection + "/" + code;
        QNetworkRequest request(inquiryUrl(collection, code));
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
        auto *reply = network.get(request);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, archive, post, fail, documentsPath, collection, name, field, code, label, fromPending] {
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

            const QString archiveName = documentsPath + "/" + deletedCollection + "/"
                                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
            archive->writes.prepend(QJsonObject{
                {"update", QJsonObject{{"name", archiveName}, {"fields", archive->fields}}},
                {"currentDocument", QJsonObject{{"exists", false}}}
            });
            auto *commit = post(":commit", {{"writes", archive->writes}});
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
                    const bool lookupMatches = loadedCode == code;
                    if (lookupMatches) {
                        loadedCode.clear();
                        publicLoaded = privateLoaded = false;
                        publicExists = privateExists = false;
                    }
                    for (auto *list : {ui->publicInquiryList, ui->privateInquiryList}) {
                        const auto *current = list->currentItem();
                        const bool currentMatches = current && current->data(Qt::UserRole).toString() == code;
                        if (currentMatches)
                            list->setCurrentItem(nullptr);
                        if (currentMatches || (lookupMatches && !current)) {
                            const bool isPublic = list == ui->publicInquiryList;
                            auto *tab = isPublic ? ui->publicTab : ui->privateTab;
                            auto *selectionLabel = isPublic ? ui->publicSelectedCodeLabel : ui->privateSelectedCodeLabel;
                            selectionLabel->setText(tr("Select an inquiry"));
                            for (auto *edit : tab->findChildren<QLineEdit *>())
                                edit->clear();
                            for (auto *edit : tab->findChildren<QPlainTextEdit *>())
                                edit->clear();
                        }
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
