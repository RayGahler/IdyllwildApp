#include "firestore.h"

#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

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

QDateTime inquiryReceivedAt(const QJsonObject &document)
{
    const auto fields = document.value("fields").toObject();
    for (const auto *name : {"receivedAt", "submittedAt", "createdAt"}) {
        const auto value = fields.value(QLatin1String(name)).toObject();
        const auto timestamp = QDateTime::fromString(
            value.value("timestampValue").toString(value.value("stringValue").toString()), Qt::ISODate);
        if (timestamp.isValid())
            return timestamp;
    }
    return QDateTime::fromString(document.value("createTime").toString(), Qt::ISODate);
}

QList<QJsonObject> newestInquiriesFirst(const QJsonArray &documents)
{
    struct DatedInquiry {
        QJsonObject document;
        QDateTime receivedAt;
    };
    QList<DatedInquiry> dated;
    for (const auto &entry : documents) {
        const auto document = entry.toObject();
        dated.append(DatedInquiry{document, inquiryReceivedAt(document)});
    }
    std::stable_sort(dated.begin(), dated.end(), [](const auto &left, const auto &right) {
        if (left.receivedAt.isValid() != right.receivedAt.isValid())
            return left.receivedAt.isValid();
        return left.receivedAt > right.receivedAt;
    });

    QList<QJsonObject> sorted;
    sorted.reserve(dated.size());
    for (const auto &entry : dated)
        sorted.append(entry.document);
    return sorted;
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
