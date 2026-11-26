#ifndef MAINIDYLL_H
#define MAINIDYLL_H

#include <QMainWindow>
#include <QByteArray>
#include <QDateTime>
#include <QMargins>
#include <QResizeEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QHash>
#include <QSoundEffect>
#include <QTimer>
#include <QString>
#include <QImage>
#include <QPixmap>
#include <QUrl>
#include <QVariantAnimation>

#include <functional>
#include <optional>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainIdyll;
}
QT_END_NAMESPACE

class QListWidgetItem;
class QShowEvent;
class QEvent;
class QObject;

class MainIdyll : public QMainWindow
{
    Q_OBJECT

public:
    MainIdyll(QWidget *parent = nullptr);
    ~MainIdyll();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void signIn();
    void finishSignIn(const QString &token, const QString &refresh, qint64 expiresAt,
                      bool localTestAccount);
    void signOut();
    void renewPendingSession(bool background);

    void openCreateInquiry();
    void openRepairExport(bool schedule);
    void showRepairExport(bool schedule, const QString &inquiryCode, const QJsonObject &fields);
    void openInvoiceMaker();
    void publishExistingInvoice();
    void publishInvoicePdf(const QString &inquiryCode, const QString &invoiceNumber,
                           const QByteArray &pdfBytes, std::optional<qint64> amountDue = {});
    void saveInvoiceLink(const QString &inquiryCode, const QString &invoiceNumber,
                         const QUrl &downloadUrl, const QDateTime &expiresAt, std::optional<qint64> amountDue);
    void finishInvoicePublication(const QString &code, const QString &invoiceNumber,
                                  const QDateTime &expiresAt, const QString &updateTime,
                                  const QJsonObject &invoiceFields, const QJsonArray &extraWrites = {});
    void reportInvoicePublicationError(const QString &message);
    void configureSquarePayments();
    void reviewInvoicePayment();
    void finishPaymentReview(const QString &message);
    void checkPendingCardPayments();
    void approveInvoicePayment(const QString &paymentId, const QString &updateTime,
                               const QString &cardInquiryCode = {}, bool background = false);
    void verifySquarePayment(const QString &code, const QString &paymentId, const QString &updateTime,
                             bool background = false);
    void finishSquareCheck(const QString &paymentId, bool background, const QString &message);
    QString invoiceInquiryCode() const;
    QString invoiceInquiryStatus() const;
    bool invoiceStatusEligible() const;
    void updateInvoiceButtons();
    void applyMacSafeArea();
    void postNewInquiry(const QString &code, const QJsonObject &publicData,
                        const QJsonObject &privateData);

    void refreshPendingInquiries(bool background = false);
    void loadPendingPage(bool background, const QString &pageToken, QJsonArray documents);
    void applyPendingList(const QJsonArray &documents);
    void updatePendingHighlight();
    void setPendingFieldsEnabled(bool enabled);
    void loadPendingInquiry();
    void clearPendingEditor();
    void populatePendingEditor(const QJsonObject &fields);
    void promotePendingInquiry();
    void removePendingInquiryFromList(const QString &code);

    void refreshCollection(const QString &collection);
    void loadCollectionPage(const QString &collection, const QString &pageToken, QJsonArray documents);
    void loadCollectionRecord(bool isPublic, const QString &code);
    void populateInquiryEditor(bool isPublic, const QJsonObject &fields);
    void loadInquiry();
    QJsonObject publicFields() const;
    QJsonObject privateFields() const;
    void saveInquiry();
    void savePrivateInquiry();
    void saveInquiryRecords(bool isPublic);

    void refreshDeletedInquiries(bool preserveSelection = false);
    void loadDeletedPage(const QString &pageToken, bool preserveSelection = false,
                         const QString &selectedArchiveId = {});
    void showDeletedInquiry(QListWidgetItem *item);
    void updatePermanentDeleteButton();
    void permanentlyDeleteInquiry();
    QListWidgetItem *selectedInquiry() const;
    void updateDeleteButton();
    void updatePendingDeleteButton();
    void deleteSelectedInquiry();
    void deletePendingInquiry();
    void archiveInquiry(const QString &code, const QString &label, bool fromPending);

    void showError(const QString &message);
    void updateInquiryActions();
    void transitionTo(QWidget *page, std::function<void()> onSwitch);
    void updateGreeting();
    void refreshBrandPixmaps();
    void renderLoginBrand(qreal progress);
    void applyPrivateInquiryFilter();
    void applyPrivateInquiryStatus(QListWidgetItem *item);

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
    bool localTestMode = false;
    bool screenTransitionInProgress = false;
    struct InquiryEditor {
        QString code;
        quint64 revision = 0;
        bool loaded = false;
    };
    InquiryEditor publicInquiry;
    InquiryEditor privateInquiry;
    QHash<QString, QString> publicStatusByCode;
    QPixmap loginLogoPixmap;
    QImage logoImage;
    QVariantAnimation loginBrandAnimation;
    int loginTitleSize = 0;
    bool savingInquiry = false;
    bool invoiceUploadInProgress = false;
    bool paymentReviewInProgress = false;
    bool pendingCardQueryInProgress = false;
    QSet<QString> squarePaymentsInProgress;
    QMargins baseCentralMargins;
    QString pendingCode;
    QString selectedPendingReceivedAt;
    QString selectedPendingUpdateTime;
    quint64 pendingRecordRevision = 0;
};
#endif
