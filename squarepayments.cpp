#include "squarepayments.h"

#include <QUrl>

namespace Square {

#ifndef Q_OS_MACOS
Credentials savedCredentials()
{
    return {};
}

bool saveCredentials(const Credentials &)
{
    return false;
}
#endif

QNetworkRequest request(const Credentials &credentials, const QString &path)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://connect.squareup.com/v2/") + path));
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Authorization", "Bearer " + credentials.accessToken.toUtf8());
    request.setRawHeader("Square-Version", "2026-11-18");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

QJsonObject checkoutPayload(const Credentials &credentials, const QString &paymentId,
                            const QString &invoiceNumber, qint64 amountCents, const QUrl &returnUrl)
{
    return {
        {"idempotency_key", paymentId},
        {"quick_pay", QJsonObject{
            {"name", QStringLiteral("Idyllwild invoice %1").arg(invoiceNumber)},
            {"price_money", QJsonObject{{"amount", amountCents}, {"currency", "USD"}}},
            {"location_id", credentials.locationId}
        }},
        {"checkout_options", QJsonObject{
            {"allow_tipping", false},
            {"accepted_payment_methods", QJsonObject{
                {"apple_pay", false}, {"google_pay", false}, {"cash_app_pay", false},
                {"afterpay_clearpay", false}, {"bank_account", false}
            }},
            {"redirect_url", returnUrl.toString(QUrl::FullyEncoded)}
        }}
    };
}

bool isCheckoutUrl(const QUrl &url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https")
        && url.port() == -1 && url.userInfo().isEmpty() && !url.path().isEmpty()
        && (url.host() == QStringLiteral("square.link") || url.host() == QStringLiteral("checkout.square.site")
            || url.host() == QStringLiteral("squareup.com"));
}

bool completedCardPaymentMatches(const QJsonObject &payment, const QString &paymentId,
                                 const QString &orderId, const QString &locationId, qint64 amountCents)
{
    const auto amount = payment.value("amount_money").toObject();
    const auto refund = payment.value("refunded_money");
    return amountCents > 0 && payment.value("id").toString() == paymentId
        && payment.value("status").toString() == QStringLiteral("COMPLETED")
        && payment.value("source_type").toString() == QStringLiteral("CARD")
        && payment.value("order_id").toString() == orderId
        && payment.value("location_id").toString() == locationId
        && amount.value("amount").toInteger(-1) == amountCents
        && amount.value("currency").toString() == QStringLiteral("USD")
        && (refund.isUndefined() || refund.toObject().value("amount").toInteger(-1) == 0);
}
}
