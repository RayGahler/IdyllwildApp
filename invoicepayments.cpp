#include "mainidyll.h"
#include "repairstatus.h"
#include "firestore.h"
#include "squarepayments.h"

#include <ui_mainidyll.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLocale>
#include <QMessageBox>
#include <QRegularExpression>

namespace {

QJsonObject fieldsOf(const QJsonObject &document)
{
    return document.value("fields").toObject();
}

qint64 amountInCents(const QJsonObject &fields)
{
    return fields.value("invoiceAmountCents").toObject().value("integerValue").toString().toLongLong();
}

QString segment(const QString &value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

QString cashAmount(qint64 cents)
{
    return QLocale(QLocale::English, QLocale::UnitedStates).toCurrencyString(cents / 100.0);
}
}

void MainIdyll::configureSquarePayments()
{
    if (idToken.isEmpty() || localTestMode)
        return;

    const auto previous = Square::savedCredentials();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Square payments"));
    dialog.setWindowModality(Qt::WindowModal);
    auto *layout = new QFormLayout(&dialog);
    auto *location = new QLineEdit(previous.locationId, &dialog);
    auto *token = new QLineEdit(&dialog);
    token->setEchoMode(QLineEdit::Password);
    if (previous.isConfigured())
        token->setPlaceholderText(tr("Leave blank to keep the Keychain token"));
    layout->addRow(tr("Square location ID"), location);
    layout->addRow(tr("Production API token"), token);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *connectButton = buttons->addButton(tr("Connect Square"), QDialogButtonBox::AcceptRole);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(connectButton, &QPushButton::clicked, &dialog, [this, &dialog, token, location, previous, connectButton] {
        const Square::Credentials credentials{
            token->text().trimmed().isEmpty() ? previous.accessToken : token->text().trimmed(),
            location->text().trimmed()
        };
        static const QRegularExpression locationPattern(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
        if (!credentials.isConfigured() || !locationPattern.match(credentials.locationId).hasMatch()) {
            QMessageBox::warning(&dialog, tr("Square credentials required"),
                tr("Enter a production API token and the location ID from your Square account."));
            return;
        }

        connectButton->setEnabled(false);
        auto *reply = network.get(Square::request(credentials, QStringLiteral("locations/") + credentials.locationId));
        connect(&dialog, &QDialog::finished, reply, &QNetworkReply::abort);
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
        connect(reply, &QNetworkReply::finished, &dialog, [reply, credentials, &dialog, connectButton] {
            const auto location = QJsonDocument::fromJson(reply->readAll()).object().value("location").toObject();
            if (reply->error() == QNetworkReply::OperationCanceledError)
                return;
            if (reply->error() != QNetworkReply::NoError || location.value("id").toString() != credentials.locationId
                || location.value("status").toString() != QStringLiteral("ACTIVE")
                || location.value("currency").toString() != QStringLiteral("USD")) {
                connectButton->setEnabled(true);
                QMessageBox::warning(&dialog, tr("Square connection failed"),
                    tr("Square did not confirm an active USD location with this token."));
                return;
            }
            if (!Square::saveCredentials(credentials)) {
                connectButton->setEnabled(true);
                QMessageBox::warning(&dialog, tr("Keychain unavailable"), tr("The Square token could not be saved to Keychain."));
                return;
            }
            dialog.accept();
        });
    });
    if (dialog.exec() == QDialog::Accepted)
        ui->statusbar->showMessage(tr("Square connected. The API token is stored in Keychain."));
}

void MainIdyll::reviewInvoicePayment()
{
    if (idToken.isEmpty() || !publicInquiry.loaded || paymentReviewInProgress)
        return;

    const QString code = publicInquiry.code;
    const auto session = sessionGeneration;
    paymentReviewInProgress = true;
    updateInquiryActions();
    auto *invoiceReply = network.get(Firestore::authorizedRequest(
        Firestore::inquiryUrl(QStringLiteral("publicInquiries"), code), idToken));
    connect(invoiceReply, &QNetworkReply::finished, this, [this, invoiceReply, session, code] {
        invoiceReply->deleteLater();
        if (session != sessionGeneration)
            return;

        const auto fields = fieldsOf(QJsonDocument::fromJson(invoiceReply->readAll()).object());
        const QString paymentId = Firestore::readStringField(fields, "invoicePaymentId");
        const qint64 amount = amountInCents(fields);
        const QString invoiceNumber = Firestore::readStringField(fields, "invoiceNumber");
        if (invoiceReply->error() != QNetworkReply::NoError || paymentId.isEmpty() || amount <= 0) {
            finishPaymentReview(tr("No payable invoice is available for %1.").arg(code));
            return;
        }

        auto *choiceReply = network.get(Firestore::authorizedRequest(
            Firestore::inquiryUrl(QStringLiteral("invoicePaymentChoices"), paymentId), idToken));
        connect(choiceReply, &QNetworkReply::finished, this,
                [this, choiceReply, session, code, paymentId, amount, invoiceNumber] {
            choiceReply->deleteLater();
            if (session != sessionGeneration)
                return;
            const auto document = QJsonDocument::fromJson(choiceReply->readAll()).object();
            if (Firestore::isNotFound(choiceReply, document)) {
                finishPaymentReview(tr("The customer has not selected a payment method."));
                return;
            }
            if (choiceReply->error() != QNetworkReply::NoError) {
                finishPaymentReview(tr("The payment choice could not be loaded."));
                return;
            }

            const auto fields = fieldsOf(document);
            if (Firestore::readStringField(fields, "status") == QStringLiteral("approved")) {
                finishPaymentReview(tr("Payment for invoice %1 is already approved.").arg(invoiceNumber));
                return;
            }

            const QString method = Firestore::readStringField(fields, "method");
            const QString updateTime = document.value("updateTime").toString();
            if (method == QStringLiteral("card")) {
                verifySquarePayment(code, paymentId, updateTime);
                return;
            }

            const QString label = method == QStringLiteral("zelle") ? tr("Zelle")
                : method == QStringLiteral("cash") ? tr("Cash") : tr("Other");
            const int approval = QMessageBox::question(this, tr("Approve invoice payment"),
                tr("Invoice %1 is awaiting review for %2 by %3. Have you received the full payment?")
                    .arg(invoiceNumber, cashAmount(amount), label),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (session != sessionGeneration)
                return;
            if (approval == QMessageBox::Yes)
                approveInvoicePayment(paymentId, updateTime);
            else
                finishPaymentReview(tr("Payment is still awaiting manual review."));
        });
    });
}

void MainIdyll::checkPendingCardPayments()
{
    if (idToken.isEmpty() || localTestMode || pendingCardQueryInProgress
        || !Square::savedCredentials().isConfigured())
        return;

    const auto equals = [](const QString &field, const QString &value) {
        return QJsonObject{{"fieldFilter", QJsonObject{
            {"field", QJsonObject{{"fieldPath", field}}},
            {"op", "EQUAL"},
            {"value", Firestore::stringField(value)}
        }}};
    };
    const QJsonObject query{
        {"from", QJsonArray{QJsonObject{{"collectionId", "invoicePaymentChoices"}}}},
        {"where", QJsonObject{{"compositeFilter", QJsonObject{
            {"op", "AND"},
            {"filters", QJsonArray{equals(QStringLiteral("method"), QStringLiteral("card")),
                                   equals(QStringLiteral("status"), QStringLiteral("pending"))}}
        }}}}
    };
    const QUrl url(QStringLiteral("https://firestore.googleapis.com/v1/%1:runQuery").arg(Firestore::documentsPath()));
    auto request = Firestore::authorizedRequest(url, idToken);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    pendingCardQueryInProgress = true;
    const auto session = sessionGeneration;
    auto *reply = network.post(request, QJsonDocument(QJsonObject{{"structuredQuery", query}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, session] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        pendingCardQueryInProgress = false;
        if (reply->error() != QNetworkReply::NoError)
            return;

        const auto results = QJsonDocument::fromJson(reply->readAll()).array();
        for (const auto &result : results) {
            const auto document = result.toObject().value("document").toObject();
            const QString code = Firestore::readStringField(fieldsOf(document), "code");
            const QString paymentId = document.value("name").toString().section('/', -1);
            const QString updateTime = document.value("updateTime").toString();
            if (!code.isEmpty() && !paymentId.isEmpty() && !updateTime.isEmpty())
                verifySquarePayment(code, paymentId, updateTime, true);
        }
    });
}

void MainIdyll::verifySquarePayment(const QString &code, const QString &paymentId,
                                    const QString &updateTime, bool background)
{
    if (squarePaymentsInProgress.contains(paymentId)) {
        if (!background)
            finishPaymentReview(tr("Square verification is already in progress for this invoice."));
        return;
    }

    const auto credentials = Square::savedCredentials();
    if (!credentials.isConfigured()) {
        if (!background)
            finishPaymentReview(tr("Connect Square in Preferences to verify this card payment."));
        return;
    }

    squarePaymentsInProgress.insert(paymentId);
    const auto fail = [this, paymentId, background](const QString &message) {
        finishSquareCheck(paymentId, background, message);
    };
    const auto session = sessionGeneration;
    auto *checkoutReply = network.get(Firestore::authorizedRequest(
        Firestore::inquiryUrl(QStringLiteral("invoiceSquareCheckouts"), paymentId), idToken));
    connect(checkoutReply, &QNetworkReply::finished, this,
            [this, checkoutReply, session, credentials, code, paymentId, updateTime, background, fail] {
        checkoutReply->deleteLater();
        if (session != sessionGeneration)
            return;
        const auto checkout = fieldsOf(QJsonDocument::fromJson(checkoutReply->readAll()).object());
        const QString orderId = Firestore::readStringField(checkout, "orderId");
        const qint64 amountCents = checkout.value("amountCents").toObject().value("integerValue").toString().toLongLong();
        if (checkoutReply->error() != QNetworkReply::NoError || orderId.isEmpty() || amountCents <= 0
            || Firestore::readStringField(checkout, "code") != code
            || Firestore::readStringField(checkout, "locationId") != credentials.locationId) {
            fail(tr("The checkout could not be verified with the connected Square location."));
            return;
        }

        auto *orderReply = network.get(Square::request(credentials, QStringLiteral("orders/") + segment(orderId)));
        connect(orderReply, &QNetworkReply::finished, this,
                [this, orderReply, session, credentials, code, paymentId, updateTime,
                 background, fail, amountCents, orderId] {
            orderReply->deleteLater();
            if (session != sessionGeneration)
                return;
            const auto order = QJsonDocument::fromJson(orderReply->readAll()).object().value("order").toObject();
            const auto tenders = order.value("tenders").toArray();
            if (orderReply->error() != QNetworkReply::NoError || order.value("id").toString() != orderId) {
                fail(tr("Square did not return the invoice order. Check the Square connection."));
                return;
            }
            if (tenders.size() != 1) {
                fail(tr("Square has not confirmed a single payment for this invoice."));
                return;
            }
            const auto tender = tenders.first().toObject();
            const QString squarePaymentId = tender.value("payment_id").toString(tender.value("id").toString());
            if (squarePaymentId.isEmpty()) {
                fail(tr("Square has not confirmed a payment for this invoice."));
                return;
            }

            auto *paymentReply = network.get(Square::request(credentials, QStringLiteral("payments/") + segment(squarePaymentId)));
            connect(paymentReply, &QNetworkReply::finished, this,
                    [this, paymentReply, session, code, paymentId, updateTime, background, fail, amountCents, orderId,
                     locationId = credentials.locationId, squarePaymentId] {
                paymentReply->deleteLater();
                if (session != sessionGeneration)
                    return;
                const auto payment = QJsonDocument::fromJson(paymentReply->readAll()).object().value("payment").toObject();
                if (paymentReply->error() != QNetworkReply::NoError
                    || !Square::completedCardPaymentMatches(payment, squarePaymentId, orderId, locationId, amountCents)) {
                    fail(tr("Square has not confirmed the full, unrefunded invoice payment."));
                    return;
                }
                approveInvoicePayment(paymentId, updateTime, code, background);
            });
        });
    });
}

void MainIdyll::approveInvoicePayment(const QString &paymentId, const QString &updateTime,
                                      const QString &cardInquiryCode, bool background)
{
    const QJsonObject approval{
        {"update", QJsonObject{
            {"name", Firestore::documentsPath() + QStringLiteral("/invoicePaymentChoices/") + paymentId},
            {"fields", QJsonObject{{"status", Firestore::stringField(QStringLiteral("approved"))}}}
        }},
        {"updateMask", QJsonObject{{"fieldPaths", QJsonArray{QStringLiteral("status")}}}},
        {"updateTransforms", QJsonArray{QJsonObject{{"fieldPath", "reviewedAt"}, {"setToServerValue", "REQUEST_TIME"}}}},
        {"currentDocument", QJsonObject{{"updateTime", updateTime}}}
    };
    QJsonArray writes{approval};
    if (!cardInquiryCode.isEmpty()) {
        writes.append(QJsonObject{
            {"update", QJsonObject{
                {"name", Firestore::documentsPath() + QStringLiteral("/publicInquiries/") + cardInquiryCode},
                {"fields", QJsonObject{{"status", Firestore::stringField(RepairStatus::done)}}}
            }},
            {"updateMask", QJsonObject{{"fieldPaths", QJsonArray{QStringLiteral("status")}}}},
            {"currentDocument", QJsonObject{{"exists", true}}}
        });
    }

    const auto session = sessionGeneration;
    auto *reply = Firestore::commitWrites(network, idToken, writes);
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, paymentId, cardInquiryCode, background] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        const bool saved = reply->error() == QNetworkReply::NoError;
        if (cardInquiryCode.isEmpty()) {
            finishPaymentReview(saved ? tr("Payment approved. The inquiry can now be marked Done!")
                                      : tr("Payment approval failed. Check technician access or reload the inquiry."));
            return;
        }

        finishSquareCheck(paymentId, background,
            saved ? tr("Square payment verified. Inquiry %1 is Done!").arg(cardInquiryCode)
                  : tr("Could not save the verified payment. Reload the inquiry and check technician access."));
        if (saved) {
            refreshCollection(QStringLiteral("publicInquiries"));
            if (background)
                ui->statusbar->showMessage(tr("Square payment verified. Inquiry %1 is Done!").arg(cardInquiryCode));
        }
    });
}

void MainIdyll::finishSquareCheck(const QString &paymentId, bool background, const QString &message)
{
    squarePaymentsInProgress.remove(paymentId);
    if (!background)
        finishPaymentReview(message);
}

void MainIdyll::finishPaymentReview(const QString &message)
{
    paymentReviewInProgress = false;
    updateInquiryActions();
    ui->statusbar->showMessage(message);
}
