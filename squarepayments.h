#ifndef SQUAREPAYMENTS_H
#define SQUAREPAYMENTS_H

#include <QJsonObject>
#include <QNetworkRequest>
#include <QString>

namespace Square {
struct Credentials {
    QString accessToken;
    QString locationId;

    bool isConfigured() const { return !accessToken.isEmpty() && !locationId.isEmpty(); }
};

Credentials savedCredentials();
bool saveCredentials(const Credentials &credentials);
QNetworkRequest request(const Credentials &credentials, const QString &path);
QJsonObject checkoutPayload(const Credentials &credentials, const QString &paymentId,
                            const QString &invoiceNumber, qint64 amountCents, const QUrl &returnUrl);
bool isActiveCardLocation(const QJsonObject &location, const QString &locationId);
bool isCheckoutUrl(const QUrl &url);
bool completedCardPaymentMatches(const QJsonObject &payment, const QString &paymentId,
                                 const QString &orderId, const QString &locationId, qint64 amountCents);
}

#endif
