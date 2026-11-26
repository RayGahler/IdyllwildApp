#include "mainidyll.h"
#include "repairstatus.h"
#include "firestore.h"
#include "invoice.h"

#include <ui_mainidyll.h>

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QUuid>
#include <QtMath>

#ifdef Q_OS_MACOS
#include <QFutureWatcher>
#include <QtConcurrentRun>
#endif

namespace {
constexpr qint64 maximumInvoiceBytes = 10 * 1024 * 1024;

QString safeInvoiceName(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QString());
    return value.isEmpty() ? QStringLiteral("Invoice") : value;
}

QString localInvoiceDirectory(const QString &code)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Idyllwild Invoices/%1").arg(safeInvoiceName(code)));
}

} // namespace

void MainIdyll::openInvoiceMaker()
{
    const QString code = invoiceInquiryCode();
    if (idToken.isEmpty() || localTestMode || code.isEmpty()
        || !invoiceStatusEligible() || invoiceUploadInProgress)
        return;

    invoiceUploadInProgress = true;
    updateInvoiceButtons();
    ui->statusbar->showMessage(tr("Loading repair details for invoice…"));
    const auto session = sessionGeneration;
    QNetworkRequest request(Firestore::inquiryUrl(QStringLiteral("privateInquiries"), code));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    request.setTransferTimeout(15000);
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, code, session] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        const auto error = reply->error();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (session != sessionGeneration)
            return;
        if (error != QNetworkReply::NoError) {
            invoiceUploadInProgress = false;
            updateInvoiceButtons();
            QMessageBox::warning(this, tr("Could not open invoice form"),
                tr("Could not load the repair details for %1. %2")
                    .arg(code, Firestore::firestoreError(
                        response.value(QStringLiteral("error")).toObject(), fallback)));
            return;
        }

        ui->statusbar->clearMessage();
        GeneratedInvoice invoice;
        const bool submitted = createInvoice(
            this, code, response.value(QStringLiteral("fields")).toObject(), &invoice);
        if (session != sessionGeneration)
            return;
        invoiceUploadInProgress = false;
        updateInvoiceButtons();
        if (!submitted)
            return;
        const QString invoiceDirectory = localInvoiceDirectory(code);
        if (!QDir().mkpath(invoiceDirectory)) {
            showError(tr("The invoice was created, but its local folder could not be created."));
            return;
        }
        const QString localInvoicePath = QDir(invoiceDirectory).filePath(
            QStringLiteral("%1.pdf").arg(safeInvoiceName(invoice.number)));
        QSaveFile localFile(localInvoicePath);
        if (!localFile.open(QIODevice::WriteOnly)
            || localFile.write(invoice.pdf) != invoice.pdf.size() || !localFile.commit()) {
            showError(tr("The invoice was created, but could not be saved locally: %1")
                          .arg(localFile.errorString()));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(localInvoicePath));
        publishInvoicePdf(code, invoice.number, invoice.pdf, invoice.amountDueCents);
    });
}

void MainIdyll::publishExistingInvoice()
{
    const QString code = invoiceInquiryCode();
    if (idToken.isEmpty() || localTestMode || code.isEmpty()
        || !invoiceStatusEligible() || invoiceUploadInProgress)
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose invoice PDF for %1").arg(code), localInvoiceDirectory(code), tr("PDF files (*.pdf)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showError(tr("Could not open the selected PDF: %1").arg(file.errorString()));
        return;
    }
    if (file.size() <= 0 || file.size() > maximumInvoiceBytes) {
        showError(tr("The PDF must be smaller than 10 MB."));
        return;
    }
    const QByteArray bytes = file.readAll();
    publishInvoicePdf(code, QFileInfo(path).completeBaseName(), bytes);
}

void MainIdyll::publishInvoicePdf(const QString &inquiryCode, const QString &invoiceNumber,
                                 const QByteArray &pdfBytes, std::optional<qint64> amountDue)
{
    if (idToken.isEmpty() || localTestMode || invoiceUploadInProgress || generatingSquareLink)
        return;
    if (!pdfBytes.startsWith("%PDF-") || pdfBytes.size() > maximumInvoiceBytes) {
        showError(tr("Select a valid PDF no larger than 10 MB."));
        return;
    }

#ifdef Q_OS_MACOS
    invoiceUploadInProgress = true;
    updateInvoiceButtons();
    ui->statusbar->showMessage(tr("Syncing the invoice PDF for %1 to iCloud Drive…").arg(inquiryCode));
    const QString safeNumber = safeInvoiceName(invoiceNumber);
    const auto session = sessionGeneration;
    auto *watcher = new QFutureWatcher<ICloudInvoiceLink>(this);
    connect(watcher, &QFutureWatcher<ICloudInvoiceLink>::finished, this,
            [this, watcher, session, inquiryCode, safeNumber, amountDue] {
        const auto link = watcher->result();
        watcher->deleteLater();
        if (session != sessionGeneration)
            return;

        const auto &url = link.url;
        const QUrlQuery query(url);
        const bool valid = url.isValid() && url.scheme() == QStringLiteral("https")
            && url.host() == QStringLiteral("www.icloud.com") && url.port() == -1 && url.userInfo().isEmpty()
            && (url.path() == QStringLiteral("/attachment/") || url.path() == QStringLiteral("/attachment"))
            && !query.queryItemValue(QStringLiteral("u")).isEmpty()
            && !query.queryItemValue(QStringLiteral("uk")).isEmpty()
            && link.expiresAt > QDateTime::currentDateTimeUtc();
        if (!valid) {
            invoiceUploadInProgress = false;
            updateInvoiceButtons();
            ui->statusbar->showMessage(tr("The invoice was not published."));
            QMessageBox::warning(this, tr("Could not publish invoice"),
                link.error.isEmpty() ? tr("iCloud did not return a valid download link.") : link.error);
            return;
        }
        saveInvoiceLink(inquiryCode, safeNumber, link.url, link.expiresAt, amountDue);
    });
    watcher->setFuture(QtConcurrent::run(publishMacInvoice, safeInvoiceName(inquiryCode), safeNumber, pdfBytes));
#else
    Q_UNUSED(inquiryCode);
    Q_UNUSED(invoiceNumber);
    Q_UNUSED(amountDue);
    showError(tr("Publish the PDF from IdyllwildApp on a Mac with iCloud Drive enabled."));
#endif
}

void MainIdyll::saveInvoiceLink(const QString &code, const QString &invoiceNumber,
                                const QUrl &downloadUrl, const QDateTime &expiresAt,
                                std::optional<qint64> amountDue)
{
    const auto session = sessionGeneration;
    auto *reply = network.get(Firestore::authorizedRequest(
        Firestore::inquiryUrl(QStringLiteral("publicInquiries"), code), idToken));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, session, code, invoiceNumber, downloadUrl, expiresAt, amountDue]() mutable {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        const auto document = QJsonDocument::fromJson(reply->readAll()).object();
        const QString updateTime = document.value("updateTime").toString();
        if (reply->error() != QNetworkReply::NoError || updateTime.isEmpty()) {
            reportInvoicePublicationError(tr("The repair record could not be loaded."));
            return;
        }

        const auto previous = document.value("fields").toObject();
        const QString previousId = Firestore::readStringField(previous, "invoicePaymentId");
        const QString previousNumber = Firestore::readStringField(previous, "invoiceNumber");
        const auto previousAmount = previous.value("invoiceAmountCents").toObject()
                                        .value("integerValue").toString().toLongLong();
        const auto url = Firestore::stringField(downloadUrl.toString(QUrl::FullyEncoded));
        const QJsonObject expiration{{"timestampValue", expiresAt.toUTC().toString(Qt::ISODateWithMs)}};
        const QJsonObject download{{"code", Firestore::stringField(code)}, {"url", url}, {"expiresAt", expiration}};
        QJsonObject fields{
            {"invoiceUrl", Firestore::stringField(customerInvoiceUrl(code, invoiceNumber).toString(QUrl::FullyEncoded))},
            {"invoiceNumber", Firestore::stringField(invoiceNumber)},
            {"invoiceUpdatedAt", QJsonObject{{"timestampValue", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}}
        };

        if (!previousId.isEmpty() && previousNumber == invoiceNumber
            && (!amountDue || *amountDue == previousAmount)) {
            fields.insert("invoicePaymentId", Firestore::stringField(previousId));
            const QJsonObject renewDownload{{"update", QJsonObject{
                {"name", Firestore::documentsPath() + QStringLiteral("/invoiceDownloads/") + previousId},
                {"fields", download}
            }}};
            finishInvoicePublication(code, invoiceNumber, expiresAt, updateTime, fields, {renewDownload});
            return;
        }

        if (!amountDue && !previousId.isEmpty()) {
            reportInvoicePublicationError(tr("Choose the PDF for invoice %1 to renew the existing payment link.")
                                              .arg(previousNumber));
            return;
        }

        if (!amountDue) {
            bool confirmed = false;
            const double dollars = QInputDialog::getDouble(this, tr("Invoice amount due"),
                tr("Enter the amount due in this PDF in USD. Enter 0 if it has already been paid."),
                0, 0, 999999.99, 2, &confirmed);
            if (session != sessionGeneration)
                return;
            if (!confirmed) {
                invoiceUploadInProgress = false;
                updateInvoiceButtons();
                ui->statusbar->showMessage(tr("Invoice publication canceled."));
                return;
            }
            amountDue = qRound64(dollars * 100.0);
        }

        if (*amountDue == 0) {
            if (!previousId.isEmpty()) {
                reportInvoicePublicationError(tr("Use Review invoice payment to approve the existing payment."));
                return;
            }
            fields.insert("invoiceICloudUrl", url);
            fields.insert("invoiceLinkExpiresAt", expiration);
            finishInvoicePublication(code, invoiceNumber, expiresAt, updateTime, fields);
            return;
        }

        const QString paymentId = QUuid::createUuid().toString(QUuid::Id128);
        fields.insert("invoicePaymentId", Firestore::stringField(paymentId));
        fields.insert("invoiceAmountCents", QJsonObject{{"integerValue", QString::number(*amountDue)}});
        fields.insert("invoiceCardReady", QJsonObject{{"booleanValue", false}});
        const auto status = RepairStatus::normalized(Firestore::readStringField(previous, "status"));
        if (status == RepairStatus::done || status == RepairStatus::readyForPickup)
            fields.insert("status", Firestore::stringField(RepairStatus::readyForPickup));

        finishInvoicePublication(code, invoiceNumber, expiresAt, updateTime, fields, {
            Firestore::createInquiryWrite(QStringLiteral("invoiceDownloads"), paymentId, download)
        });
    });
}

void MainIdyll::finishInvoicePublication(const QString &code, const QString &invoiceNumber,
                                         const QDateTime &expiresAt, const QString &updateTime,
                                         const QJsonObject &invoiceFields, const QJsonArray &extraWrites)
{
    QJsonArray mask;
    for (auto it = invoiceFields.begin(); it != invoiceFields.end(); ++it)
        mask.append(it.key());
    if (invoiceFields.contains("invoicePaymentId")) {
        // Masked fields absent from the write are removed from older public records.
        mask.append(QStringLiteral("invoiceICloudUrl"));
        mask.append(QStringLiteral("invoiceLinkExpiresAt"));
    }

    QJsonArray writes = extraWrites;
    writes.append(QJsonObject{
        {"update", QJsonObject{
            {"name", Firestore::documentsPath() + QStringLiteral("/publicInquiries/") + code},
            {"fields", invoiceFields}
        }},
        {"updateMask", QJsonObject{{"fieldPaths", mask}}},
        {"currentDocument", QJsonObject{{"updateTime", updateTime}}}
    });
    auto *reply = Firestore::commitWrites(network, idToken, writes);
    const auto session = sessionGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, code, invoiceNumber, expiresAt] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        if (reply->error() != QNetworkReply::NoError) {
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            const QString error = Firestore::firestoreError(response.value("error").toObject(), reply->errorString());
            reportInvoicePublicationError(tr("The website did not accept the invoice. Check staff access and whether a customer already chose a payment method. %1")
                                             .arg(error));
            return;
        }

        invoiceUploadInProgress = false;
        updateInvoiceButtons();
        refreshCollection(QStringLiteral("publicInquiries"));
        ui->statusbar->showMessage(tr("Invoice %1 is available for inquiry %2.").arg(invoiceNumber, code));
        QMessageBox::information(this, tr("Invoice published"),
            tr("Customers can open invoice %1 from the repair status page with inquiry code %2. The PDF link expires on %3.")
                .arg(invoiceNumber, code, QLocale().toString(expiresAt.date(), QLocale::LongFormat)));
    });
}

void MainIdyll::reportInvoicePublicationError(const QString &message)
{
    invoiceUploadInProgress = false;
    updateInvoiceButtons();
    ui->statusbar->showMessage(tr("The invoice was not published."));
    QMessageBox::warning(this, tr("Could not publish invoice"), message);
}
