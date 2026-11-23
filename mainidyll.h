#ifndef MAINIDYLL_H
#define MAINIDYLL_H

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QSoundEffect>
#include <QTimer>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainIdyll;
}
QT_END_NAMESPACE

class QListWidgetItem;

class MainIdyll : public QMainWindow
{
    Q_OBJECT

public:
    MainIdyll(QWidget *parent = nullptr);
    ~MainIdyll();

private:
    void signIn();
    void signOut();
    void loadInquiry();
    void loadPublicRecord();
    void loadPrivateRecord();
    void saveInquiry();
    void savePrivateInquiry();
    void deleteSelectedInquiry();
    void deletePendingInquiry();
    void archiveInquiry(const QString &code, const QString &label, bool fromPending);
    void removePendingInquiryFromList(const QString &code);
    void updatePendingDeleteButton();
    QListWidgetItem *selectedInquiry() const;
    void updateDeleteButton();
    void refreshDeletedInquiries();
    void loadDeletedPage(const QString &pageToken);
    void showDeletedInquiry(QListWidgetItem *item);
    void refreshCollection(const QString &collection);
    void loadCollectionRecord(const QString &collection, const QString &code);
    void saveSingleRecord(const QString &collection, const QJsonObject &fields);
    void openCreateInquiry();
    void postNewInquiry(const QString &code, const QJsonObject &publicData,
                        const QJsonObject &privateData);
    void refreshPendingInquiries(bool background = false);
    void loadPendingPage(bool background, const QString &pageToken, QJsonArray documents);
    void applyPendingList(const QJsonArray &documents);
    void renewPendingSession(bool background);
    void updatePendingHighlight();
    void setPendingFieldsEnabled(bool enabled);
    void loadPendingInquiry(const QString &code);
    void promotePendingInquiry();
    void finishPendingPromotion();
    void saveRecord(const QString &collection, const QJsonObject &fields);
    QJsonObject publicFields() const;
    QJsonObject privateFields() const;
    void showError(const QString &message);
    void updateButtons();

    Ui::MainIdyll *ui;
    QNetworkAccessManager network;
    QTimer pendingTimer;
    QSoundEffect pendingSound;
    QSet<QString> knownPendingIds;
    QSet<QString> unreadPendingIds;
    bool pendingBaselineLoaded = false;
    bool pendingRefreshInProgress = false;
    qint64 tokenExpiresAt = 0;
    quint64 sessionGeneration = 0;
    quint64 pendingListRevision = 0;
    QString idToken;
    QString refreshToken;
    QString loadedCode;
    QString publicSelectedCode;
    QString privateSelectedCode;
    bool publicLoaded = false;
    bool privateLoaded = false;
    bool publicExists = false;
    bool privateExists = false;
    bool savingPublic = false;
    bool savingPrivate = false;
    QString pendingCode;
    QJsonObject selectedPendingData;
    bool promotingPublicDone = false;
    bool promotingPrivateDone = false;
};
#endif
