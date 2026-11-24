#include "mainidyll.h"
#include "preferredcontact.h"
#include "firestore.h"

#include <ui_mainidyll.h>

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrlQuery>

using Firestore::firestoreError;

namespace {
const QString apiKey = QStringLiteral("AIzaSyAUIPxT6zXZPK4ZmFQ5xQj8OzybNmPLnEg");
}

void MainIdyll::signIn()
{
    ui->signInButton->setEnabled(false);
    ui->statusbar->showMessage(QStringLiteral("Signing in…"));
    QNetworkRequest request(QUrl(QStringLiteral(
        "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=%1")
        .arg(apiKey)));
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
        idToken = response.value("idToken").toString();
        refreshToken = response.value("refreshToken").toString();
        tokenExpiresAt = QDateTime::currentSecsSinceEpoch() + response.value("expiresIn").toString().toLongLong();
        ui->emailEdit->setEnabled(false);
        ui->passwordEdit->clear();
        ui->passwordEdit->setEnabled(false);
        ui->signInButton->setText(QStringLiteral("Signed in"));
        ui->signInButton->setEnabled(false);
        ui->signOutButton->setEnabled(true);
        ui->inquiryTabs->setEnabled(true);
        ui->statusbar->showMessage(QStringLiteral("Signed in. Enter an inquiry code to load records."));
        ui->loadButton->setEnabled(true);
        ui->createInquiryButton->setEnabled(true);
        pendingTimer.start();
        refreshPendingInquiries();
        if (ui->inquiryTabs->currentWidget() == ui->deletedTab)
            refreshDeletedInquiries();
        refreshCollection("publicInquiries");
        refreshCollection("privateInquiries");
        reply->deleteLater();
    });
}

void MainIdyll::signOut()
{
    ++sessionGeneration;
    pendingTimer.stop();
    pendingSound.stop();
    pendingRefreshInProgress = false;
    pendingBaselineLoaded = false;
    knownPendingIds.clear();
    unreadPendingIds.clear();
    updatePendingHighlight();
    ui->refreshPendingButton->setEnabled(true);
    idToken.clear();
    refreshToken.clear();
    loadedCode.clear();
    publicSelectedCode.clear();
    privateSelectedCode.clear();
    pendingCode.clear();
    selectedPendingData = {};
    publicLoaded = privateLoaded = false;
    publicExists = privateExists = false;
    savingPublic = savingPrivate = false;
    promotingPublicDone = promotingPrivateDone = false;

    ui->inquiryTabs->setEnabled(false);
    ui->pendingList->clear();
    ui->pendingList->setEnabled(true);
    ui->deletedInquiryList->clear();
    ui->deletedInquiryDetails->clear();
    ui->deletedListStatus->clear();
    ui->refreshDeletedButton->setEnabled(true);
    ui->publicInquiryList->clear();
    ui->privateInquiryList->clear();
    ui->pendingSelectedCodeLabel->setText(QStringLiteral("None"));
    ui->publicSelectedCodeLabel->setText(QStringLiteral("Select an inquiry"));
    ui->privateSelectedCodeLabel->setText(QStringLiteral("Select an inquiry"));
    ui->pendingNameEdit->clear();
    ui->pendingPhoneEdit->clear();
    ui->pendingEmailEdit->clear();
    ui->pendingDeviceEdit->clear();
    ui->pendingModelEdit->clear();
    ui->pendingProblemEdit->clear();
    setPreferredContact(*ui->pendingPreferredEdit);
    ui->pendingAddressEdit->clear();
    ui->pendingStatusEdit->setText(QStringLiteral("Received"));
    ui->pendingMessageEdit->clear();
    ui->publicDeviceEdit->clear();
    ui->publicModelEdit->clear();
    ui->statusEdit->clear();
    ui->publicMessageEdit->clear();
    ui->nameEdit->clear();
    ui->phoneEdit->clear();
    ui->privateEmailEdit->clear();
    ui->privateDeviceEdit->clear();
    ui->privateModelEdit->clear();
    ui->problemEdit->clear();
    setPreferredContact(*ui->preferredContactEdit);
    ui->addressEdit->clear();
    ui->codeEdit->clear();
    ui->emailEdit->clear();
    ui->emailEdit->setEnabled(true);
    ui->passwordEdit->clear();
    ui->passwordEdit->setEnabled(true);
    ui->signInButton->setText(QStringLiteral("Sign in"));
    ui->signInButton->setEnabled(true);
    ui->signOutButton->setEnabled(false);
    ui->createInquiryButton->setEnabled(false);
    ui->promotePendingButton->setEnabled(false);
    ui->saveButton->setEnabled(false);
    ui->savePrivateButton->setEnabled(false);
    setPendingFieldsEnabled(false);
    ui->statusbar->showMessage(QStringLiteral("Signed out."));
}

void MainIdyll::renewPendingSession(bool background)
{
    QUrl url(QStringLiteral("https://securetoken.googleapis.com/v1/token"));
    QUrlQuery query;
    query.addQueryItem("key", apiKey);
    url.setQuery(query);
    QNetworkRequest request(url);
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
