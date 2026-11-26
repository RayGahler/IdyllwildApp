#ifndef FIRESTORE_H
#define FIRESTORE_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

class QNetworkReply;
class QNetworkAccessManager;

namespace Firestore {

extern const QString projectId;

QString documentsPath();
QJsonObject createInquiryWrite(const QString &collection, const QString &code,
                               const QJsonObject &fields);
QNetworkReply *commitWrites(QNetworkAccessManager &network, const QString &idToken,
                            const QJsonArray &writes);
QUrl inquiryUrl(const QString &collection, const QString &code);
QUrl inquiryCollectionUrl(const QString &collection);
QDateTime inquiryReceivedAt(const QJsonObject &document);
QList<QJsonObject> newestInquiriesFirst(const QJsonArray &documents);
QJsonValue stringField(const QString &value);
QString readStringField(const QJsonObject &fields, const QString &name);
bool isNotFound(const QNetworkReply *reply, const QJsonObject &response);
QString firestoreError(const QJsonObject &error, const QString &fallback);
QJsonObject firestoreFields(const QJsonObject &fields);

}

#endif
