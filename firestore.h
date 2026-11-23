#ifndef FIRESTORE_H
#define FIRESTORE_H

#include <QJsonObject>
#include <QString>
#include <QUrl>

class QNetworkReply;

namespace Firestore {

extern const QString projectId;

QUrl inquiryUrl(const QString &collection, const QString &code);
QUrl inquiryCollectionUrl(const QString &collection);
QJsonValue stringField(const QString &value);
QString readStringField(const QJsonObject &fields, const QString &name);
bool isNotFound(const QNetworkReply *reply, const QJsonObject &response);
QString firestoreError(const QJsonObject &error, const QString &fallback);
QJsonObject firestoreFields(const QJsonObject &fields);

}

#endif
