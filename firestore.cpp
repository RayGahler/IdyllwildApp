#include "firestore.h"

#include <QNetworkReply>
#include <QNetworkRequest>

namespace Firestore {

const QString projectId = QStringLiteral("idyllwild-e323e");

QUrl inquiryUrl(const QString &collection, const QString &code)
{
    return QUrl(QStringLiteral(
        "https://firestore.googleapis.com/v1/projects/%1/databases/(default)/documents/%2/%3")
                    .arg(projectId, collection, QString::fromLatin1(QUrl::toPercentEncoding(code))));
}

QUrl inquiryCollectionUrl(const QString &collection)
{
    return QUrl(QStringLiteral(
        "https://firestore.googleapis.com/v1/projects/%1/databases/(default)/documents/%2")
                    .arg(projectId, collection));
}

QJsonValue stringField(const QString &value)
{
    return QJsonObject{{"stringValue", value}};
}

QString readStringField(const QJsonObject &fields, const QString &name)
{
    return fields.value(name).toObject().value("stringValue").toString();
}

bool isNotFound(const QNetworkReply *reply, const QJsonObject &response)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404
           || response.value("error").toObject().value("status").toString() == "NOT_FOUND";
}

QString firestoreError(const QJsonObject &error, const QString &fallback)
{
    const QString message = error.value("message").toString(fallback);
    const QString status = error.value("status").toString();
    return status.isEmpty() ? message : QStringLiteral("%1 — %2").arg(status, message);
}

QJsonObject firestoreFields(const QJsonObject &fields)
{
    QJsonObject result;
    for (auto it = fields.begin(); it != fields.end(); ++it)
        result.insert(it.key(), stringField(it.value().toString()));
    return result;
}
}
