#include "mainidyll.h"
#include "preferredcontact.h"
#include "firestore.h"

#include <ui_mainidyll.h>

#include <QDateTime>
#include <QJsonDocument>
#include <QMenuBar>
#include <QNetworkRequest>
#include <QTime>
#include <QUrlQuery>

using Firestore::firestoreError;

namespace {
const QString apiKey = QStringLiteral("AIzaSyAUIPxT6zXZPK4ZmFQ5xQj8OzybNmPLnEg");
#ifdef IDYLLWILD_ENABLE_LOCAL_TEST_ACCOUNT
const QString localTestUsername = QStringLiteral("admin");
const QString localTestPassword = QStringLiteral("admin");
#endif
}

void MainIdyll::signIn()
{
    if (screenTransitionInProgress || !ui->signInButton->isEnabled())
        return;

#ifdef IDYLLWILD_ENABLE_LOCAL_TEST_ACCOUNT
    if (ui->emailEdit->text().trimmed().compare(localTestUsername, Qt::CaseInsensitive) == 0
        && ui->passwordEdit->text() == localTestPassword) {
        finishSignIn({}, {}, 0, true);
        return;
    }
#endif
    ui->signInButton->setEnabled(false);
    ui->statusbar->showMessage(QStringLiteral("Signing in…"));
    QNetworkRequest request(QUrl(QStringLiteral(
        "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=%1")
        .arg(apiKey)));
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QJsonObject body{
        {"email", ui->emailEdit->text().trimmed()},
        {"password", ui->passwordEdit->text()},
        {"returnSecureToken", true}
    };
    auto *reply = network.post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray payload = reply->readAll();
        const auto response = QJsonDocument::fromJson(payload).object();
        if (reply->error() != QNetworkReply::NoError || !response.contains("idToken")) {
            const auto error = response.value("error").toObject();
            const QString code = error.value("message").toString();
            QString message;
            if (code == "EMAIL_NOT_FOUND" || code == "INVALID_PASSWORD"
                || code == "INVALID_LOGIN_CREDENTIALS") {
                message = QStringLiteral("Email or password is incorrect. Check the email and password, and confirm the account exists in Firebase Authentication.");
            } else if (code == "OPERATION_NOT_ALLOWED") {
                message = QStringLiteral("Email and password sign-in is disabled. Enable it in Firebase Console under Authentication > Sign-in method.");
            } else if (code == "API_KEY_INVALID" || code == "INVALID_API_KEY") {
                message = QStringLiteral("Firebase rejected the API key. Confirm this is the Web API key for project idyllwild-e323e.");
            } else if (code == "TOO_MANY_ATTEMPTS_TRY_LATER") {
                message = QStringLiteral("Firebase temporarily blocked sign-in attempts. Wait and try again later.");
            } else if (!code.isEmpty()) {
                message = QStringLiteral("Firebase sign-in failed (%1).").arg(code);
            } else {
                message = QStringLiteral("Firebase sign-in failed: %1").arg(reply->errorString());
            }
            showError(message);
            ui->signInButton->setEnabled(true);
            reply->deleteLater();
            return;
        }
        finishSignIn(response.value("idToken").toString(),
                     response.value("refreshToken").toString(),
                     QDateTime::currentSecsSinceEpoch()
                         + response.value("expiresIn").toString().toLongLong(), false);
        reply->deleteLater();
    });
}

void MainIdyll::finishSignIn(const QString &token, const QString &refresh, qint64 expiresAt,
                             bool localTestAccount)
{
    transitionTo(ui->workspaceWidget, [this, token, refresh, expiresAt, localTestAccount] {
        localTestMode = localTestAccount;
        idToken = token;
        refreshToken = refresh;
        tokenExpiresAt = expiresAt;
        ui->emailEdit->setEnabled(false);
        ui->passwordEdit->clear();
        ui->passwordEdit->setEnabled(false);
        ui->signInButton->setText(localTestMode ? tr("Local test") : tr("Signed in"));
        ui->signInButton->setEnabled(false);
        ui->signOutButton->setEnabled(true);
        ui->inquiryTabs->setEnabled(true);
        menuBar()->show();
        if (localTestMode) {
            ui->signInGroup->setTitle(tr("Local test account · no cloud data"));
            for (QPushButton *button : {ui->loadButton, ui->deleteInquiryButton,
                                        ui->createInquiryButton, ui->createContactButton,
                                        ui->scheduleRepairButton, ui->refreshPendingButton,
                                        ui->refreshPublicButton, ui->refreshPrivateButton,
                                        ui->refreshDeletedButton, ui->deletePendingButton,
                                        ui->promotePendingButton, ui->saveButton,
                                        ui->savePrivateButton, ui->permanentlyDeleteButton,
                                        ui->createInvoiceButton, ui->uploadInvoiceButton})
                button->setEnabled(false);
            ui->statusbar->showMessage(tr("Local test mode: the workspace is empty and does not connect to Firebase."));
        } else {
            ui->loadButton->setEnabled(true);
            ui->createInquiryButton->setEnabled(true);
            ui->createContactButton->setEnabled(true);
            ui->scheduleRepairButton->setEnabled(true);
            pendingTimer.start();
            refreshPendingInquiries();
            refreshDeletedInquiries();
            refreshCollection("publicInquiries");
            refreshCollection("privateInquiries");
            ui->statusbar->showMessage(tr("Signed in. Enter an inquiry code to load records."));
        }

        updateGreeting();
    });
}

void MainIdyll::signOut()
{
    if (screenTransitionInProgress)
        return;

    ++sessionGeneration;
    pendingTimer.stop();
    pendingSound.stop();
    transitionTo(ui->loginPage, [this] {
        localTestMode = false;
        menuBar()->hide();
        ui->signInGroup->setTitle(QStringLiteral("Admin sign in"));
        pendingRefreshInProgress = false;
        pendingBaselineLoaded = false;
        knownPendingIds.clear();
        unreadPendingIds.clear();
        updatePendingHighlight();
        ui->refreshPendingButton->setEnabled(true);
        idToken.clear();
        publicInquiry = {};
        privateInquiry = {};
        savingInquiry = false;
        refreshToken.clear();
        invoiceUploadInProgress = false;
        publicStatusByCode.clear();

        ui->inquiryTabs->setEnabled(false);
        ui->pendingList->clear();
        ui->pendingList->setEnabled(true);
        ui->deletedInquiryList->clear();
        ui->deletedInquiryDetails->clear();
        ui->deletedListStatus->clear();
        ui->refreshDeletedButton->setEnabled(true);
        ui->refreshPublicButton->setEnabled(true);
        ui->refreshPrivateButton->setEnabled(true);
        ui->publicInquiryList->clear();
        ui->privateInquiryList->clear();
        ui->publicSelectedCodeLabel->setText(QStringLiteral("Select an inquiry"));
        ui->privateSelectedCodeLabel->setText(QStringLiteral("Select an inquiry"));
        clearPendingEditor();
        populateInquiryEditor(true, {});
        populateInquiryEditor(false, {});
        ui->codeEdit->clear();
        ui->emailEdit->clear();
        ui->emailEdit->setEnabled(true);
        ui->passwordEdit->clear();
        ui->passwordEdit->setEnabled(true);
        ui->signInButton->setText(QStringLiteral("Sign in"));
        ui->signInButton->setEnabled(true);
        ui->signOutButton->setEnabled(false);
        ui->createInquiryButton->setEnabled(false);
        ui->createContactButton->setEnabled(false);
        ui->scheduleRepairButton->setEnabled(false);
        ui->promotePendingButton->setEnabled(false);
        ui->saveButton->setEnabled(false);
        ui->savePrivateButton->setEnabled(false);
        updateInquiryActions();
        ui->statusbar->showMessage(QStringLiteral("Signed out."));
    });
}

void MainIdyll::renewPendingSession(bool background)
{
    QUrl url(QStringLiteral("https://securetoken.googleapis.com/v1/token"));
    QUrlQuery query;
    query.addQueryItem("key", apiKey);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    const QByteArray body = "grant_type=refresh_token&refresh_token=" + QUrl::toPercentEncoding(refreshToken);
    const auto session = sessionGeneration;
    auto *reply = network.post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, background] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        const QString newIdToken = response.value("id_token").toString();
        if (reply->error() != QNetworkReply::NoError || newIdToken.isEmpty()) {
            pendingRefreshInProgress = false;
            const auto error = response.value("error").toObject();
            showError(tr("Could not renew sign-in for pending checks. %1")
                          .arg(firestoreError(error, reply->errorString())));
            return;
        }

        idToken = newIdToken;
        const QString newRefreshToken = response.value("refresh_token").toString();
        if (!newRefreshToken.isEmpty())
            refreshToken = newRefreshToken;
        tokenExpiresAt = QDateTime::currentSecsSinceEpoch() + response.value("expires_in").toString().toLongLong();
        loadPendingPage(background, {}, {});
    });
}
