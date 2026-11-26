#include "mainidyll.h"
#include "firestore.h"
#include "invoice.h"

#include <ui_mainidyll.h>

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUrlQuery>

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

QUrl websiteInvoiceUrl(const QString &inquiryCode, const QString &invoiceNumber)
{
    QUrl url(QStringLiteral("https://idyllwildelectronics.com/invoice%1/")
                 .arg(QString::fromLatin1(QUrl::toPercentEncoding(invoiceNumber))));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("code"), inquiryCode);
    url.setQuery(query);
    return url;
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
        publishInvoicePdf(code, invoice.number, invoice.pdf);
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
                                 const QByteArray &pdfBytes)
{
    if (idToken.isEmpty() || localTestMode || invoiceUploadInProgress)
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
            [this, watcher, session, inquiryCode, safeNumber] {
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
        saveInvoiceLink(inquiryCode, safeNumber, link.url, link.expiresAt);
    });
    watcher->setFuture(QtConcurrent::run(publishMacInvoice, safeInvoiceName(inquiryCode), safeNumber, pdfBytes));
#else
    Q_UNUSED(inquiryCode);
    Q_UNUSED(invoiceNumber);
    showError(tr("Publish the PDF from IdyllwildApp on a Mac with iCloud Drive enabled."));
#endif
}

void MainIdyll::saveInvoiceLink(const QString &inquiryCode, const QString &invoiceNumber,
                                const QUrl &downloadUrl, const QDateTime &expiresAt)
{
    QUrl url = Firestore::inquiryUrl(QStringLiteral("publicInquiries"), inquiryCode);
    const QJsonObject fields{
        {QStringLiteral("invoiceICloudUrl"), Firestore::stringField(downloadUrl.toString(QUrl::FullyEncoded))},
        {QStringLiteral("invoiceLinkExpiresAt"), QJsonObject{{QStringLiteral("timestampValue"),
            expiresAt.toUTC().toString(Qt::ISODateWithMs)}}},
        {QStringLiteral("invoiceUrl"), Firestore::stringField(
             websiteInvoiceUrl(inquiryCode, invoiceNumber).toString(QUrl::FullyEncoded))},
        {QStringLiteral("invoiceNumber"), Firestore::stringField(invoiceNumber)},
        {QStringLiteral("invoiceUpdatedAt"), QJsonObject{{QStringLiteral("timestampValue"),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}}
    };
    QUrlQuery query;
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        query.addQueryItem(QStringLiteral("updateMask.fieldPaths"), it.key());
    query.addQueryItem(QStringLiteral("currentDocument.exists"), QStringLiteral("true"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(15000);
    const auto session = sessionGeneration;
    auto *reply = network.sendCustomRequest(
        request, "PATCH", QJsonDocument(QJsonObject{{QStringLiteral("fields"), fields}}).toJson());
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, session, inquiryCode, invoiceNumber, expiresAt] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        const auto error = reply->error();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        invoiceUploadInProgress = false;
        updateInvoiceButtons();
        if (error != QNetworkReply::NoError || response.contains(QStringLiteral("error"))) {
            ui->statusbar->showMessage(tr("The invoice was not published."));
            QMessageBox::warning(this, tr("Could not publish invoice"),
                tr("The PDF is in iCloud Drive, but the website link could not be saved. %1 Try Publish invoice PDF again.")
                    .arg(Firestore::firestoreError(
                        response.value(QStringLiteral("error")).toObject(), fallback)));
            return;
        }

        ui->statusbar->showMessage(tr("Invoice %1 is available for inquiry %2.")
                                       .arg(invoiceNumber, inquiryCode));
        QMessageBox::information(this, tr("Invoice published"),
            tr("Customers can download invoice %1 by entering %2 on the repair status page. The link expires on %3.")
                .arg(invoiceNumber, inquiryCode, QLocale().toString(expiresAt.toLocalTime().date(), QLocale::LongFormat)));
    });
}
