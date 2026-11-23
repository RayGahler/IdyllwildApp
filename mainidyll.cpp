#include "mainidyll.h"
#include <ui_mainidyll.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QApplication>
#include <QFileDialog>
#include <QHash>
#include <QMenuBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QVBoxLayout>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <memory>
#include <utility>

namespace {
const QString apiKey = QStringLiteral("AIzaSyAUIPxT6zXZPK4ZmFQ5xQj8OzybNmPLnEg");
const QString projectId = QStringLiteral("idyllwild-e323e");

const QUrl defaultPendingSound(QStringLiteral("qrc:/sounds/pending-alert.wav"));
const QString pendingSoundKey = QStringLiteral("notifications/pendingSound");

const QString deletedCollection = QStringLiteral("deletedInquiries");

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

MainIdyll::MainIdyll(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainIdyll)
{
    ui->setupUi(this);
    ui->inquiryTabs->setEnabled(false);
    ui->signOutButton->setEnabled(false);
    ui->saveButton->setEnabled(false);
    ui->savePrivateButton->setEnabled(false);

    connect(ui->signInButton, &QPushButton::clicked, this, &MainIdyll::signIn);
    connect(ui->signOutButton, &QPushButton::clicked, this, &MainIdyll::signOut);
    connect(ui->loadButton, &QPushButton::clicked, this, &MainIdyll::loadInquiry);
    connect(ui->deleteInquiryButton, &QPushButton::clicked, this, &MainIdyll::deleteSelectedInquiry);
    connect(ui->inquiryTabs, &QTabWidget::currentChanged, this, [this] {
        updateDeleteButton();
        if (ui->inquiryTabs->currentWidget() == ui->pendingTab) {
            unreadPendingIds.clear();
            updatePendingHighlight();
            refreshPendingInquiries();
        }
        if (ui->inquiryTabs->currentWidget() == ui->deletedTab)
            refreshDeletedInquiries();
    });
    connect(ui->inquiryTabs, &QTabWidget::tabBarClicked, this, [this](int index) {
        if (ui->inquiryTabs->widget(index) == ui->pendingTab)
            refreshPendingInquiries();
    });
    connect(ui->refreshDeletedButton, &QPushButton::clicked, this, &MainIdyll::refreshDeletedInquiries);
    connect(ui->deletedInquiryList, &QListWidget::currentItemChanged, this, &MainIdyll::showDeletedInquiry);
    ui->deletedSplitter->setStretchFactor(1, 2);
    connect(ui->publicInquiryList, &QListWidget::itemSelectionChanged, this, &MainIdyll::updateDeleteButton);
    connect(ui->privateInquiryList, &QListWidget::itemSelectionChanged, this, &MainIdyll::updateDeleteButton);
    connect(ui->saveButton, &QPushButton::clicked, this, &MainIdyll::saveInquiry);
    connect(ui->savePrivateButton, &QPushButton::clicked, this, &MainIdyll::savePrivateInquiry);
    connect(ui->refreshPublicButton, &QPushButton::clicked, this, [this] {
        refreshCollection("publicInquiries");
    });
    connect(ui->refreshPrivateButton, &QPushButton::clicked, this, [this] {
        refreshCollection("privateInquiries");
    });
    connect(ui->publicInquiryList, &QListWidget::currentTextChanged, this, [this](const QString &code) {
        publicSelectedCode = code;
        ui->saveButton->setEnabled(false);
        if (!code.isEmpty())
            loadCollectionRecord("publicInquiries", code);
    });
    connect(ui->privateInquiryList, &QListWidget::currentTextChanged, this, [this](const QString &code) {
        privateSelectedCode = code;
        ui->savePrivateButton->setEnabled(false);
        if (!code.isEmpty())
            loadCollectionRecord("privateInquiries", code);
    });
    connect(ui->createInquiryButton, &QPushButton::clicked, this, &MainIdyll::openCreateInquiry);
    connect(ui->refreshPendingButton, &QPushButton::clicked, this, [this] {
        refreshPendingInquiries();
    });
    connect(ui->deletePendingButton, &QPushButton::clicked, this, &MainIdyll::deletePendingInquiry);
    connect(ui->pendingList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        updatePendingDeleteButton();
        if (item) {
            unreadPendingIds.remove(item->data(Qt::UserRole).toString());
            updatePendingHighlight();
            loadPendingInquiry(item->text());
        }
    });
    pendingTimer.setInterval(30000);
    connect(&pendingTimer, &QTimer::timeout, this, [this] { refreshPendingInquiries(true); });

    const QString soundPath = QSettings().value(pendingSoundKey).toString();
    connect(&pendingSound, &QSoundEffect::statusChanged, this, [this] {
        if (pendingSound.status() == QSoundEffect::Error && pendingSound.source() != defaultPendingSound) {
            showError(tr("Could not load the selected alert sound. Using the default sound."));
            QSettings().remove(pendingSoundKey);
            pendingSound.setSource(defaultPendingSound);
        }
    });
    pendingSound.setSource(soundPath.isEmpty() ? defaultPendingSound : QUrl::fromLocalFile(soundPath));
    auto *alerts = menuBar()->addMenu(tr("Alerts"));
    alerts->addAction(tr("Choose pending inquiry sound…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Choose pending inquiry sound"), QSettings().value(pendingSoundKey).toString(),
            tr("WAV audio (*.wav)"));
        if (!path.isEmpty()) {
            QSettings().setValue(pendingSoundKey, path);
            pendingSound.setSource(QUrl::fromLocalFile(path));
        }
    });
    alerts->addAction(tr("Test pending inquiry sound"), this, [this] {
        if (pendingSound.isLoaded())
            pendingSound.play();
        else
            QApplication::beep();
    });
    alerts->addAction(tr("Use default pending inquiry sound"), this, [this] {
        QSettings().remove(pendingSoundKey);
        pendingSound.setSource(defaultPendingSound);
    });
    connect(ui->promotePendingButton, &QPushButton::clicked, this, &MainIdyll::promotePendingInquiry);
    ui->createInquiryButton->setEnabled(false);
    ui->promotePendingButton->setEnabled(false);
    setPendingFieldsEnabled(false);
}

MainIdyll::~MainIdyll()
{
    delete ui;
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
    ui->pendingPreferredEdit->clear();
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
    ui->preferredContactEdit->clear();
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

void MainIdyll::openCreateInquiry()
{
    if (idToken.isEmpty())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Create inquiry"));
    dialog.resize(560, 680);
    auto *layout = new QVBoxLayout(&dialog);
    auto *codeEdit = new QLineEdit(QStringLiteral("IDY-%1").arg(
        QString::number(QRandomGenerator::global()->bounded(0x1000000), 16)
            .rightJustified(6, QLatin1Char('0')).toUpper()), &dialog);
    codeEdit->setReadOnly(true);
    auto *codeForm = new QFormLayout;
    codeForm->addRow(QStringLiteral("Inquiry code"), codeEdit);
    layout->addLayout(codeForm);

    auto *publicGroup = new QGroupBox(QStringLiteral("Public record"), &dialog);
    auto *publicForm = new QFormLayout(publicGroup);
    auto *device = new QLineEdit(publicGroup);
    auto *model = new QLineEdit(publicGroup);
    auto *status = new QLineEdit(QStringLiteral("Received"), publicGroup);
    auto *publicMessage = new QPlainTextEdit(publicGroup);
    publicForm->addRow(QStringLiteral("Device"), device);
    publicForm->addRow(QStringLiteral("Model"), model);
    publicForm->addRow(QStringLiteral("Status"), status);
    publicForm->addRow(QStringLiteral("Public message"), publicMessage);
    layout->addWidget(publicGroup);

    auto *privateGroup = new QGroupBox(QStringLiteral("Private record"), &dialog);
    auto *privateForm = new QFormLayout(privateGroup);
    auto *name = new QLineEdit(privateGroup);
    auto *phone = new QLineEdit(privateGroup);
    auto *email = new QLineEdit(privateGroup);
    auto *privateDevice = new QLineEdit(privateGroup);
    auto *privateModel = new QLineEdit(privateGroup);
    auto *problem = new QPlainTextEdit(privateGroup);
    auto *preferredContact = new QLineEdit(privateGroup);
    privateForm->addRow(QStringLiteral("Name"), name);
    privateForm->addRow(QStringLiteral("Phone"), phone);
    privateForm->addRow(QStringLiteral("Email"), email);
    privateForm->addRow(QStringLiteral("Device"), privateDevice);
    privateForm->addRow(QStringLiteral("Model"), privateModel);
    privateForm->addRow(QStringLiteral("Problem"), problem);
    privateForm->addRow(QStringLiteral("Preferred contact"), preferredContact);
    layout->addWidget(privateGroup);

    auto *buttons = new QDialogButtonBox(&dialog);
    auto *cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
    auto *postButton = buttons->addButton(QStringLiteral("Post"), QDialogButtonBox::AcceptRole);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(postButton, &QPushButton::clicked, &dialog, [&] {
        if (device->text().trimmed().isEmpty() || model->text().trimmed().isEmpty()
            || name->text().trimmed().isEmpty() || problem->toPlainText().trimmed().isEmpty()) {
            QMessageBox::information(&dialog, QStringLiteral("Missing information"),
                QStringLiteral("Fill in the device, model, name, and problem fields."));
            return;
        }
        postButton->setEnabled(false);
        cancelButton->setEnabled(false);
        postNewInquiry(codeEdit->text(), {
            {"device", device->text().trimmed()},
            {"model", model->text().trimmed()},
            {"status", status->text().trimmed()},
            {"publicMessage", publicMessage->toPlainText().trimmed()}
        }, {
            {"name", name->text().trimmed()},
            {"phone", phone->text().trimmed()},
            {"email", email->text().trimmed()},
            {"device", privateDevice->text().trimmed().isEmpty() ? device->text().trimmed() : privateDevice->text().trimmed()},
            {"model", privateModel->text().trimmed().isEmpty() ? model->text().trimmed() : privateModel->text().trimmed()},
            {"problem", problem->toPlainText().trimmed()},
            {"preferredContact", preferredContact->text().trimmed()}
        });
        dialog.accept();
    });
    layout->addWidget(buttons);
    dialog.exec();
}

void MainIdyll::postNewInquiry(const QString &code, const QJsonObject &publicData,
                               const QJsonObject &privateData)
{
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (!publicExists && publicData.contains("inquiryCode")) {
        // Preserve the current helper's create semantics for new inquiry documents.
    }
    auto postRecord = [this, &code, &now](const QString &collection, QJsonObject data,
                                          const QString &label) {
        data.insert("inquiryCode", code);
        QJsonObject fields = firestoreFields(data);
        fields.insert("createdAt", QJsonObject{{"timestampValue", now}});
        fields.insert("updatedAt", QJsonObject{{"timestampValue", now}});
        QUrl collectionUrl = inquiryCollectionUrl(collection);
        QUrlQuery query;
        query.addQueryItem("documentId", code);
        collectionUrl.setQuery(query);
        QNetworkRequest request(collectionUrl);
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        auto *reply = network.sendCustomRequest(
            request, "POST", QJsonDocument(QJsonObject{{"fields", fields}}).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, reply, label, code] {
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            if (reply->error() != QNetworkReply::NoError) {
                showError(QStringLiteral("%1 record failed: %2").arg(
                    label, response.value("error").toObject().value("message").toString(
                        reply->errorString())));
            } else {
                ui->statusbar->showMessage(QStringLiteral("%1 record posted for %2.")
                                               .arg(label, code));
                ui->codeEdit->setText(code);
            }
            reply->deleteLater();
        });
    };
    postRecord("publicInquiries", publicData, QStringLiteral("Public"));
    postRecord("privateInquiries", privateData, QStringLiteral("Private"));
}

void MainIdyll::refreshPendingInquiries(bool background)
{
    if (idToken.isEmpty() || pendingRefreshInProgress || !ui->refreshPendingButton->isEnabled())
        return;

    pendingRefreshInProgress = true;
    if (!refreshToken.isEmpty() && QDateTime::currentSecsSinceEpoch() >= tokenExpiresAt - 60)
        renewPendingSession(background);
    else
        loadPendingPage(background, {}, {});
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

void MainIdyll::loadPendingPage(bool background, const QString &pageToken, QJsonArray documents)
{
    QUrl url = inquiryCollectionUrl("pendingInquiries");
    if (!pageToken.isEmpty()) {
        QUrlQuery query;
        query.addQueryItem("pageToken", pageToken);
        url.setQuery(query);
    }
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    const auto session = sessionGeneration;
    const auto revision = pendingListRevision;
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, session, revision, background, documents = std::move(documents)]() mutable {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        if (revision != pendingListRevision) {
            pendingRefreshInProgress = false;
            return;
        }

        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        const bool missing = isNotFound(reply, response);
        if (!missing && (reply->error() != QNetworkReply::NoError
                         || response.contains("error"))) {
            pendingRefreshInProgress = false;
            showError(tr("Could not check pending inquiries. %1")
                          .arg(firestoreError(response.value("error").toObject(), reply->errorString())));
            return;
        }

        for (const auto &document : response.value("documents").toArray())
            documents.append(document);
        const QString nextPage = response.value("nextPageToken").toString();
        if (!nextPage.isEmpty()) {
            loadPendingPage(background, nextPage, std::move(documents));
            return;
        }

        pendingRefreshInProgress = false;
        if (!ui->refreshPendingButton->isEnabled())
            return;
        applyPendingList(documents);
        if (!background)
            ui->statusbar->showMessage(tr("%1 pending inquiries loaded.").arg(documents.size()));
    });
}

void MainIdyll::applyPendingList(const QJsonArray &documents)
{
    auto *list = ui->pendingList;
    const QSignalBlocker blocker(list);
    QHash<QString, QListWidgetItem *> existing;
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        existing.insert(item->data(Qt::UserRole).toString(), item);
    }

    QSet<QString> currentIds;
    for (const auto &entry : documents) {
        const auto document = entry.toObject();
        const QString id = document.value("name").toString().section('/', -1);
        if (id.isEmpty() || currentIds.contains(id))
            continue;
        currentIds.insert(id);
        const QString code = readStringField(document.value("fields").toObject(), "inquiryCode");
        auto *item = existing.take(id);
        if (!item) {
            item = new QListWidgetItem(list);
            item->setData(Qt::UserRole, id);
        }
        item->setText(code.isEmpty() ? id : code);
    }
    for (auto *item : existing) {
        if (list->currentItem() == item)
            list->setCurrentItem(nullptr);
        delete list->takeItem(list->row(item));
    }
    if (!pendingCode.isEmpty() && !currentIds.contains(pendingCode)) {
        pendingCode.clear();
        selectedPendingData = {};
        ui->pendingSelectedCodeLabel->setText(tr("None"));
        setPendingFieldsEnabled(false);
        ui->promotePendingButton->setEnabled(false);
    }

    unreadPendingIds.intersect(currentIds);
    const auto arrivals = currentIds - knownPendingIds;
    if (pendingBaselineLoaded && !arrivals.isEmpty()) {
        unreadPendingIds.unite(arrivals);
        if (pendingSound.isLoaded())
            pendingSound.play();
        else
            QApplication::beep();
    }
    knownPendingIds = currentIds;
    pendingBaselineLoaded = true;
    updatePendingHighlight();
    updatePendingDeleteButton();
}

void MainIdyll::updatePendingHighlight()
{
    ui->inquiryTabs->tabBar()->setStyleSheet(unreadPendingIds.isEmpty() ? QString() : QStringLiteral(
        "QTabBar::tab:first { background-color: #167044; color: white; }"));
}

void MainIdyll::refreshCollection(const QString &collection)
{
    if (idToken.isEmpty())
        return;
    const bool isPublic = collection == "publicInquiries";
    auto *list = isPublic ? ui->publicInquiryList : ui->privateInquiryList;
    auto *refreshButton = isPublic ? ui->refreshPublicButton : ui->refreshPrivateButton;
    refreshButton->setEnabled(false);
    updateDeleteButton();
    QNetworkRequest request(inquiryCollectionUrl(collection));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, collection, list, refreshButton, isPublic] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (isNotFound(reply, response)) {
            list->clear();
            if (isPublic) {
                publicSelectedCode.clear();
                ui->saveButton->setEnabled(false);
            } else {
                privateSelectedCode.clear();
                ui->savePrivateButton->setEnabled(false);
            }
            refreshButton->setEnabled(true);
            updateDeleteButton();
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            refreshButton->setEnabled(true);
            updateDeleteButton();
            reply->deleteLater();
            return;
        }
        list->clear();
        if (isPublic) {
            publicSelectedCode.clear();
            ui->saveButton->setEnabled(false);
            ui->publicSelectedCodeLabel->setText(QStringLiteral("Select an inquiry"));
        } else {
            privateSelectedCode.clear();
            ui->savePrivateButton->setEnabled(false);
            ui->privateSelectedCodeLabel->setText(QStringLiteral("Select an inquiry"));
        }
        const auto documents = response.value("documents").toArray();
        for (const auto &entry : documents) {
            const auto document = entry.toObject();
            const QString documentId = document.value("name").toString().section('/', -1);
            const auto fields = document.value("fields").toObject();
            const QString code = readStringField(fields, "inquiryCode");
            auto *item = new QListWidgetItem(code.isEmpty() ? documentId : code, list);
            item->setData(Qt::UserRole, documentId);
        }
        refreshButton->setEnabled(true);
        updateDeleteButton();
        reply->deleteLater();
    });
}

void MainIdyll::loadCollectionRecord(const QString &collection, const QString &code)
{
    const bool isPublic = collection == "publicInquiries";
    QNetworkRequest request(inquiryUrl(collection, code));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic, code] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            reply->deleteLater();
            return;
        }
        const auto fields = response.value("fields").toObject();
        if (isPublic) {
            if (publicSelectedCode != code) {
                reply->deleteLater();
                return;
            }
            ui->publicSelectedCodeLabel->setText(code);
            ui->publicDeviceEdit->setText(readStringField(fields, "device"));
            ui->publicModelEdit->setText(readStringField(fields, "model"));
            ui->statusEdit->setText(readStringField(fields, "status"));
            ui->publicMessageEdit->setPlainText(readStringField(fields, "publicMessage"));
            if (!savingPublic)
                ui->saveButton->setEnabled(true);
        } else {
            if (privateSelectedCode != code) {
                reply->deleteLater();
                return;
            }
            ui->privateSelectedCodeLabel->setText(code);
            ui->nameEdit->setText(readStringField(fields, "name"));
            ui->phoneEdit->setText(readStringField(fields, "phone"));
            ui->privateEmailEdit->setText(readStringField(fields, "email"));
            ui->privateDeviceEdit->setText(readStringField(fields, "device"));
            ui->privateModelEdit->setText(readStringField(fields, "model"));
            ui->problemEdit->setPlainText(readStringField(fields, "problem"));
            ui->preferredContactEdit->setText(readStringField(fields, "preferredContact"));
            ui->addressEdit->setPlainText(readStringField(fields, "address"));
            if (!savingPrivate)
                ui->savePrivateButton->setEnabled(true);
        }
        reply->deleteLater();
    });
}

void MainIdyll::setPendingFieldsEnabled(bool enabled)
{
    const QList<QWidget *> fields{
        ui->pendingNameEdit, ui->pendingPhoneEdit, ui->pendingEmailEdit,
        ui->pendingDeviceEdit, ui->pendingModelEdit, ui->pendingProblemEdit,
        ui->pendingPreferredEdit, ui->pendingAddressEdit, ui->pendingStatusEdit,
        ui->pendingMessageEdit
    };
    for (auto *field : fields)
        field->setEnabled(enabled);
}

void MainIdyll::loadPendingInquiry(const QString &displayCode)
{
    const auto *item = ui->pendingList->currentItem();
    if (!item || idToken.isEmpty())
        return;
    pendingCode = item->data(Qt::UserRole).toString();
    if (pendingCode.isEmpty())
        pendingCode = displayCode;
    setPendingFieldsEnabled(false);
    ui->promotePendingButton->setEnabled(false);
    QNetworkRequest request(inquiryUrl("pendingInquiries", pendingCode));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    const auto session = sessionGeneration;
    const QString code = pendingCode;
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, displayCode, session, code] {
        if (session != sessionGeneration || code != pendingCode) {
            reply->deleteLater();
            return;
        }
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (isNotFound(reply, response)) {
            showError(QStringLiteral("That pending inquiry no longer exists. Refresh the list."));
            setPendingFieldsEnabled(false);
            ui->promotePendingButton->setEnabled(false);
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            reply->deleteLater();
            return;
        }
        selectedPendingData = response.value("fields").toObject();
        ui->pendingSelectedCodeLabel->setText(displayCode);
        ui->pendingNameEdit->setText(readStringField(selectedPendingData, "name"));
        ui->pendingPhoneEdit->setText(readStringField(selectedPendingData, "phone"));
        ui->pendingEmailEdit->setText(readStringField(selectedPendingData, "email"));
        ui->pendingDeviceEdit->setText(readStringField(selectedPendingData, "device"));
        ui->pendingModelEdit->setText(readStringField(selectedPendingData, "model"));
        ui->pendingProblemEdit->setPlainText(readStringField(selectedPendingData, "problem"));
        ui->pendingPreferredEdit->setText(readStringField(selectedPendingData, "preferredContact"));
        setPendingFieldsEnabled(true);
        ui->promotePendingButton->setEnabled(true);
        reply->deleteLater();
    });
}

void MainIdyll::promotePendingInquiry()
{
    if (pendingCode.isEmpty() || selectedPendingData.isEmpty())
        return;
    const QString device = ui->pendingDeviceEdit->text().trimmed();
    const QString model = ui->pendingModelEdit->text().trimmed();
    const QString name = ui->pendingNameEdit->text().trimmed();
    const QString problem = ui->pendingProblemEdit->toPlainText().trimmed();
    if (device.isEmpty() || model.isEmpty() || name.isEmpty() || problem.isEmpty()
        || ui->pendingAddressEdit->toPlainText().trimmed().isEmpty()) {
        showError(QStringLiteral("Complete the name, device, model, problem, and address before pushing this inquiry."));
        return;
    }

    ui->promotePendingButton->setEnabled(false);
    ui->refreshPendingButton->setEnabled(false);
    ui->pendingList->setEnabled(false);
    updatePendingDeleteButton();
    ui->statusbar->showMessage(QStringLiteral("Pushing pending inquiry…"));
    promotingPublicDone = false;
    promotingPrivateDone = false;

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QJsonObject publicData{
        {"inquiryCode", pendingCode},
        {"device", device},
        {"model", model},
        {"status", ui->pendingStatusEdit->text().trimmed()},
        {"publicMessage", ui->pendingMessageEdit->toPlainText().trimmed()}
    };
    const QJsonObject privateData{
        {"inquiryCode", pendingCode},
        {"name", name},
        {"phone", ui->pendingPhoneEdit->text().trimmed()},
        {"email", ui->pendingEmailEdit->text().trimmed()},
        {"device", device},
        {"model", model},
        {"problem", problem},
        {"preferredContact", ui->pendingPreferredEdit->text().trimmed()},
        {"address", ui->pendingAddressEdit->toPlainText().trimmed()}
    };

    const auto session = sessionGeneration;
    auto remaining = std::make_shared<int>(2);
    auto createDocument = [this, now, session, remaining](const QString &collection, QJsonObject data, bool isPublic) {
        QUrl url = inquiryCollectionUrl(collection);
        QUrlQuery query;
        query.addQueryItem("documentId", pendingCode);
        url.setQuery(query);
        QJsonObject fields = firestoreFields(data);
        fields.insert("createdAt", QJsonObject{{"timestampValue", now}});
        fields.insert("updatedAt", QJsonObject{{"timestampValue", now}});
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        auto *reply = network.sendCustomRequest(
            request, "POST", QJsonDocument(QJsonObject{{"fields", fields}}).toJson());
        connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic, session, remaining] {
            reply->deleteLater();
            if (session != sessionGeneration)
                return;
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            if (reply->error() != QNetworkReply::NoError) {
                showError(QStringLiteral("Could not create %1 inquiry: %2")
                              .arg(isPublic ? QStringLiteral("public") : QStringLiteral("private"),
                                   response.value("error").toObject().value("message").toString(
                                       reply->errorString())));
            } else if (isPublic) {
                promotingPublicDone = true;
            } else {
                promotingPrivateDone = true;
            }
            if (--*remaining != 0)
                return;
            if (promotingPublicDone && promotingPrivateDone) {
                finishPendingPromotion();
            } else {
                ui->refreshPendingButton->setEnabled(true);
                ui->pendingList->setEnabled(true);
                updatePendingDeleteButton();
            }
        });
    };
    createDocument("publicInquiries", publicData, true);
    createDocument("privateInquiries", privateData, false);
}

void MainIdyll::finishPendingPromotion()
{
    const auto session = sessionGeneration;
    const QString code = pendingCode;
    QNetworkRequest request(inquiryUrl("pendingInquiries", code));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.sendCustomRequest(request, "DELETE");
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, code] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            showError(QStringLiteral("Inquiry was copied, but pending record could not be removed: %1")
                          .arg(response.value("error").toObject().value("message").toString(
                              reply->errorString())));
        } else {
            ui->statusbar->showMessage(QStringLiteral("Inquiry pushed to public and private collections."));
            removePendingInquiryFromList(code);
        }
        ui->refreshPendingButton->setEnabled(true);
        ui->pendingList->setEnabled(true);
        updatePendingDeleteButton();
    });
}

void MainIdyll::refreshDeletedInquiries()
{
    if (idToken.isEmpty() || !ui->refreshDeletedButton->isEnabled())
        return;

    ui->refreshDeletedButton->setEnabled(false);
    ui->deletedInquiryList->clear();
    ui->deletedInquiryDetails->clear();
    ui->deletedListStatus->setText(tr("Loading deleted inquiries…"));
    loadDeletedPage({});
}

void MainIdyll::loadDeletedPage(const QString &pageToken)
{
    QUrl url = inquiryCollectionUrl(deletedCollection);
    QUrlQuery query;
    query.addQueryItem("orderBy", "deletedAt desc");
    if (!pageToken.isEmpty())
        query.addQueryItem("pageToken", pageToken);
    url.setQuery(query);

    const auto session = sessionGeneration;
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, session] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            ui->refreshDeletedButton->setEnabled(true);
            const auto error = response.value("error").toObject();
            if (error.value("status").toString() == "PERMISSION_DENIED"
                || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 403) {
                ui->deletedListStatus->setText(tr("Firestore rules denied access to %1.").arg(deletedCollection));
            } else {
                ui->deletedListStatus->setText(tr("Could not finish loading deleted inquiries."));
            }
            showError(firestoreError(error, reply->errorString()));
            return;
        }

        for (const auto &entry : response.value("documents").toArray()) {
            const auto document = entry.toObject();
            const auto fields = document.value("fields").toObject();
            QString code = readStringField(fields, "inquiryCode");
            if (code.isEmpty())
                code = document.value("name").toString().section('/', -1);
            const auto timestamp = fields.value("deletedAt").toObject().value("timestampValue").toString();
            const auto deletedAt = QDateTime::fromString(timestamp, Qt::ISODate);
            const QString label = deletedAt.isValid()
                ? tr("%1 · %2").arg(code, QLocale().toString(deletedAt.toLocalTime(), QLocale::ShortFormat))
                : code;
            auto *item = new QListWidgetItem(label, ui->deletedInquiryList);
            item->setData(Qt::UserRole, fields);
        }

        const QString nextPage = response.value("nextPageToken").toString();
        if (!nextPage.isEmpty()) {
            loadDeletedPage(nextPage);
            return;
        }

        ui->refreshDeletedButton->setEnabled(true);
        ui->deletedListStatus->setText(ui->deletedInquiryList->count() == 0
                                          ? tr("No deleted inquiries.")
                                          : QString());
    });
}

void MainIdyll::showDeletedInquiry(QListWidgetItem *item)
{
    ui->deletedInquiryDetails->clear();
    if (!item)
        return;

    const auto fields = item->data(Qt::UserRole).toJsonObject();
    QStringList sections{item->text()};
    const auto appendRecord = [&sections](const QString &title, const QJsonObject &record) {
        if (record.isEmpty())
            return;
        QStringList lines{title};
        const auto values = record.value("mapValue").toObject().value("fields").toObject();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            const auto encoded = it.value().toObject();
            const auto value = encoded.constBegin();
            if (value == encoded.constEnd())
                continue;

            QString text;
            if (value->isString())
                text = value->toString();
            else if (value->isObject())
                text = QString::fromUtf8(QJsonDocument(value->toObject()).toJson(QJsonDocument::Compact));
            else
                text = value->toVariant().toString();
            lines.append(it.key() + ": " + text);
        }
        sections.append(lines.join('\n'));
    };
    appendRecord(tr("Pending record"), fields.value("pendingRecord").toObject());
    appendRecord(tr("Public record"), fields.value("publicRecord").toObject());
    appendRecord(tr("Private record"), fields.value("privateRecord").toObject());
    ui->deletedInquiryDetails->setPlainText(sections.join("\n\n"));
}

QListWidgetItem *MainIdyll::selectedInquiry() const
{
    QListWidget *list = nullptr;
    if (ui->inquiryTabs->currentWidget() == ui->publicTab)
        list = ui->publicInquiryList;
    else if (ui->inquiryTabs->currentWidget() == ui->privateTab)
        list = ui->privateInquiryList;

    const auto items = list ? list->selectedItems() : QList<QListWidgetItem *>{};
    return items.size() == 1 ? items.first() : nullptr;
}

void MainIdyll::updateDeleteButton()
{
    ui->deleteInquiryButton->setEnabled(!idToken.isEmpty() && !savingPublic && !savingPrivate
                                        && ui->refreshPublicButton->isEnabled()
                                        && ui->refreshPrivateButton->isEnabled()
                                        && selectedInquiry());
}

void MainIdyll::deleteSelectedInquiry()
{
    auto *item = selectedInquiry();
    if (!item || !ui->deleteInquiryButton->isEnabled())
        return;

    archiveInquiry(item->data(Qt::UserRole).toString(), item->text(), false);
}

void MainIdyll::updatePendingDeleteButton()
{
    ui->deletePendingButton->setEnabled(!idToken.isEmpty()
                                       && ui->refreshPendingButton->isEnabled()
                                       && ui->pendingList->currentItem());
}

void MainIdyll::deletePendingInquiry()
{
    const auto *item = ui->pendingList->currentItem();
    if (!item || !ui->deletePendingButton->isEnabled())
        return;
    archiveInquiry(item->data(Qt::UserRole).toString(), item->text(), true);
}

void MainIdyll::archiveInquiry(const QString &code, const QString &label, bool fromPending)
{
    if (code.isEmpty())
        return;

    const auto choice = QMessageBox::question(
        this, tr("Delete inquiry"),
        (fromPending
             ? tr("Move pending inquiry %1 to Deleted Inquires?")
             : tr("Move inquiry %1 from the public and private collections to Deleted Inquires?")).arg(label),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice != QMessageBox::Yes)
        return;

    ui->centralwidget->setEnabled(false);
    ui->statusbar->showMessage(tr("Archiving inquiry %1…").arg(label));

    const QString documentsPath = QStringLiteral("projects/%1/databases/(default)/documents").arg(projectId);
    const auto post = [this, documentsPath](const QString &method, const QJsonObject &body) {
        QNetworkRequest request(QUrl("https://firestore.googleapis.com/v1/" + documentsPath + method));
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        return network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    };
    const auto fail = [this, label](const QString &message) {
        ui->centralwidget->setEnabled(true);
        updateDeleteButton();
        const QString title = tr("Could not archive inquiry %1").arg(label);
        ui->statusbar->showMessage(title);
        QMessageBox dialog(QMessageBox::Warning, title, message, QMessageBox::Ok, this);
        dialog.setTextFormat(Qt::PlainText);
        dialog.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        dialog.exec();
    };

    struct ArchiveRead {
        QJsonArray writes;
        QJsonObject fields;
        int pending = 0;
        bool failed = false;
        bool found = false;
    };
    auto archive = std::make_shared<ArchiveRead>();
    archive->pending = fromPending ? 1 : 2;
    archive->fields = {
        {"inquiryCode", stringField(label)},
        {"documentId", stringField(code)},
        {"deletedAt", QJsonObject{{"timestampValue", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}}
    };

    const auto read = [this, archive, post, fail, documentsPath, code, label, fromPending](
                          const QString &collection, const QString &field) {
        const QString name = documentsPath + "/" + collection + "/" + code;
        QNetworkRequest request(inquiryUrl(collection, code));
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
        auto *reply = network.get(request);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, archive, post, fail, documentsPath, collection, name, field, code, label, fromPending] {
            const auto document = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            if (archive->failed)
                return;

            QJsonObject precondition;
            if (isNotFound(reply, document)) {
                precondition.insert("exists", false);
            } else {
                const QString updateTime = document.value("updateTime").toString();
                if (reply->error() != QNetworkReply::NoError || updateTime.isEmpty()
                    || document.value("name").toString() != name) {
                    archive->failed = true;
                    const auto error = document.value("error").toObject();
                    const QString fallback = reply->error() == QNetworkReply::NoError
                        ? tr("Firestore returned an incomplete document.")
                        : tr("HTTP %1 — %2").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                                               .arg(reply->errorString());
                    fail(tr("Reading %1 failed.\n\n%2").arg(collection, firestoreError(error, fallback)));
                    return;
                }
                archive->found = true;
                precondition.insert("updateTime", updateTime);
                archive->fields.insert(field, QJsonObject{
                    {"mapValue", QJsonObject{{"fields", document.value("fields").toObject()}}}
                });
            }
            archive->writes.append(QJsonObject{{"delete", name}, {"currentDocument", precondition}});
            if (--archive->pending != 0)
                return;
            if (!archive->found) {
                fail(tr("The inquiry no longer exists."));
                return;
            }

            const QString archiveName = documentsPath + "/" + deletedCollection + "/"
                                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
            archive->writes.prepend(QJsonObject{
                {"update", QJsonObject{{"name", archiveName}, {"fields", archive->fields}}},
                {"currentDocument", QJsonObject{{"exists", false}}}
            });
            auto *commit = post(":commit", {{"writes", archive->writes}});
            connect(commit, &QNetworkReply::finished, this, [this, commit, fail, code, label, fromPending] {
                const auto result = QJsonDocument::fromJson(commit->readAll()).object();
                commit->deleteLater();
                const auto commitError = result.value("error").toObject();
                if (commit->error() != QNetworkReply::NoError || !commitError.isEmpty()) {
                    QString message = tr("Saving the archive and deleting the source records failed.\n\n%1")
                                          .arg(firestoreError(commitError, commit->errorString()));
                    if (commitError.value("status").toString() == "PERMISSION_DENIED"
                        || commit->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 403) {
                        message += tr("\n\nCheck that the Firestore rules allow your account to create "
                                      "documents in %1 and delete the source inquiry.")
                                           .arg(deletedCollection);
                    }
                    fail(message);
                    return;
                }

                if (fromPending) {
                    removePendingInquiryFromList(code);
                } else {
                    const bool lookupMatches = loadedCode == code;
                    if (lookupMatches) {
                        loadedCode.clear();
                        publicLoaded = privateLoaded = false;
                        publicExists = privateExists = false;
                    }
                    for (auto *list : {ui->publicInquiryList, ui->privateInquiryList}) {
                        const auto *current = list->currentItem();
                        const bool currentMatches = current && current->data(Qt::UserRole).toString() == code;
                        if (currentMatches)
                            list->setCurrentItem(nullptr);
                        if (currentMatches || (lookupMatches && !current)) {
                            const bool isPublic = list == ui->publicInquiryList;
                            auto *tab = isPublic ? ui->publicTab : ui->privateTab;
                            auto *selectionLabel = isPublic ? ui->publicSelectedCodeLabel : ui->privateSelectedCodeLabel;
                            selectionLabel->setText(tr("Select an inquiry"));
                            for (auto *edit : tab->findChildren<QLineEdit *>())
                                edit->clear();
                            for (auto *edit : tab->findChildren<QPlainTextEdit *>())
                                edit->clear();
                        }
                        for (int row = list->count() - 1; row >= 0; --row) {
                            if (list->item(row)->data(Qt::UserRole).toString() == code)
                                delete list->takeItem(row);
                        }
                    }
                }

                ui->centralwidget->setEnabled(true);
                updateDeleteButton();
                updatePendingDeleteButton();
                ui->statusbar->showMessage(tr("Inquiry %1 moved to Deleted Inquires.").arg(label));
            });
        });
    };
    if (fromPending) {
        read("pendingInquiries", "pendingRecord");
    } else {
        read("publicInquiries", "publicRecord");
        read("privateInquiries", "privateRecord");
    }
}

void MainIdyll::removePendingInquiryFromList(const QString &code)
{
    ++pendingListRevision;
    auto *list = ui->pendingList;
    const QSignalBlocker blocker(list);
    for (int row = list->count() - 1; row >= 0; --row) {
        auto *item = list->item(row);
        if (item->data(Qt::UserRole).toString() != code)
            continue;
        if (list->currentItem() == item)
            list->setCurrentItem(nullptr);
        delete list->takeItem(row);
    }
    if (pendingCode == code) {
        pendingCode.clear();
        selectedPendingData = {};
        ui->pendingSelectedCodeLabel->setText(tr("None"));
        setPendingFieldsEnabled(false);
        ui->promotePendingButton->setEnabled(false);
    }
    knownPendingIds.remove(code);
    unreadPendingIds.remove(code);
    updatePendingHighlight();
}

void MainIdyll::loadInquiry()
{
    const QString code = ui->codeEdit->text().trimmed().toUpper();
    if (code.isEmpty() || idToken.isEmpty())
        return;
    loadedCode = code;
    publicLoaded = privateLoaded = false;
    publicExists = privateExists = false;
    ui->codeEdit->setText(code);
    ui->loadButton->setEnabled(false);
    ui->saveButton->setEnabled(false);
    ui->statusbar->showMessage(QStringLiteral("Loading inquiry…"));
    loadPublicRecord();
    loadPrivateRecord();
}

void MainIdyll::loadPublicRecord()
{
    QNetworkRequest request(inquiryUrl("publicInquiries", loadedCode));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (isNotFound(reply, response)) {
            publicExists = false;
        } else if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            publicLoaded = true;
            reply->deleteLater();
            updateButtons();
            return;
        } else {
            const auto fields = response.value("fields").toObject();
            ui->publicDeviceEdit->setText(readStringField(fields, "device"));
            ui->publicModelEdit->setText(readStringField(fields, "model"));
            ui->statusEdit->setText(readStringField(fields, "status"));
            ui->publicMessageEdit->setPlainText(readStringField(fields, "publicMessage"));
            publicExists = true;
        }
        publicLoaded = true;
        reply->deleteLater();
        updateButtons();
    });
}

void MainIdyll::loadPrivateRecord()
{
    QNetworkRequest request(inquiryUrl("privateInquiries", loadedCode));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (isNotFound(reply, response)) {
            privateExists = false;
        } else if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
            privateLoaded = true;
            reply->deleteLater();
            updateButtons();
            return;
        } else {
            const auto fields = response.value("fields").toObject();
            ui->nameEdit->setText(readStringField(fields, "name"));
            ui->phoneEdit->setText(readStringField(fields, "phone"));
            ui->privateEmailEdit->setText(readStringField(fields, "email"));
            ui->privateDeviceEdit->setText(readStringField(fields, "device"));
            ui->privateModelEdit->setText(readStringField(fields, "model"));
            ui->problemEdit->setPlainText(readStringField(fields, "problem"));
            ui->preferredContactEdit->setText(readStringField(fields, "preferredContact"));
            ui->addressEdit->setPlainText(readStringField(fields, "address"));
            privateExists = true;
        }
        privateLoaded = true;
        reply->deleteLater();
        updateButtons();
    });
}

QJsonObject MainIdyll::publicFields() const
{
    return {
        {"device", ui->publicDeviceEdit->text().trimmed()},
        {"model", ui->publicModelEdit->text().trimmed()},
        {"status", ui->statusEdit->text()},
        {"publicMessage", ui->publicMessageEdit->toPlainText()}
    };
}

QJsonObject MainIdyll::privateFields() const
{
    return {
        {"name", ui->nameEdit->text()},
        {"phone", ui->phoneEdit->text()},
        {"email", ui->privateEmailEdit->text()},
        {"device", ui->privateDeviceEdit->text().trimmed()},
        {"model", ui->privateModelEdit->text().trimmed()},
        {"problem", ui->problemEdit->toPlainText()},
        {"preferredContact", ui->preferredContactEdit->text()},
        {"address", ui->addressEdit->toPlainText()}
    };
}

void MainIdyll::saveInquiry()
{
    if (publicSelectedCode.isEmpty() || idToken.isEmpty())
        return;
    loadedCode = publicSelectedCode;
    savingPublic = savingPrivate = true;
    updateDeleteButton();
    ui->saveButton->setEnabled(false);
    ui->savePrivateButton->setEnabled(false);
    ui->statusbar->showMessage(QStringLiteral("Saving public inquiry and shared device/model fields…"));
    const QString code = publicSelectedCode;
    const QJsonObject shared{{"device", ui->publicDeviceEdit->text().trimmed()},
                             {"model", ui->publicModelEdit->text().trimmed()}};
    saveSingleRecord("publicInquiries", publicFields());
    saveSingleRecord("privateInquiries", shared);
}

void MainIdyll::savePrivateInquiry()
{
    if (privateSelectedCode.isEmpty() || idToken.isEmpty())
        return;
    loadedCode = privateSelectedCode;
    savingPrivate = savingPublic = true;
    updateDeleteButton();
    ui->savePrivateButton->setEnabled(false);
    ui->saveButton->setEnabled(false);
    ui->statusbar->showMessage(QStringLiteral("Saving private inquiry and shared device/model fields…"));
    const QJsonObject fields = privateFields();
    const QJsonObject shared{{"device", fields.value("device")},
                             {"model", fields.value("model")}};
    saveSingleRecord("privateInquiries", fields);
    saveSingleRecord("publicInquiries", shared);
}

void MainIdyll::saveSingleRecord(const QString &collection, const QJsonObject &fields)
{
    const bool isPublic = collection == "publicInquiries";
    const bool exists = true;
    const QString code = isPublic ? publicSelectedCode : privateSelectedCode;
    if (code.isEmpty()) {
        (isPublic ? savingPublic : savingPrivate) = false;
        return;
    }
    QJsonObject documentFields{{"inquiryCode", stringField(code)}};
    for (auto it = fields.begin(); it != fields.end(); ++it)
        documentFields.insert(it.key(), stringField(it.value().toString()));
    QUrl url = inquiryUrl(collection, code);
    QUrlQuery query;
    for (auto it = documentFields.begin(); it != documentFields.end(); ++it)
        query.addQueryItem("updateMask.fieldPaths", it.key());
    query.addQueryItem("updateMask.fieldPaths", "updatedAt");
    if (!exists)
        query.addQueryItem("updateMask.fieldPaths", "createdAt");
    url.setQuery(query);

    QJsonObject encoded = documentFields;
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    encoded.insert("updatedAt", QJsonObject{{"timestampValue", now}});
    if (!exists)
        encoded.insert("createdAt", QJsonObject{{"timestampValue", now}});
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *reply = network.sendCustomRequest(
        request, "PATCH", QJsonDocument(QJsonObject{{"fields", encoded}}).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic, code] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(reply->errorString()));
        } else {
            ui->statusbar->showMessage(QStringLiteral("Saved %1 inquiry %2.")
                                           .arg(isPublic ? QStringLiteral("public") : QStringLiteral("private"), code));
        }
        if (isPublic)
            savingPublic = false;
        else
            savingPrivate = false;
        ui->saveButton->setEnabled(!publicSelectedCode.isEmpty() && !savingPublic);
        ui->savePrivateButton->setEnabled(!privateSelectedCode.isEmpty() && !savingPrivate);
        updateDeleteButton();
        reply->deleteLater();
    });
}

void MainIdyll::saveRecord(const QString &collection, const QJsonObject &fields)
{
    QUrl url = inquiryUrl(collection, loadedCode);
    QUrlQuery query;
    for (auto it = fields.begin(); it != fields.end(); ++it)
        query.addQueryItem("updateMask.fieldPaths", it.key());
    query.addQueryItem("updateMask.fieldPaths", "updatedAt");
    if (!publicExists && collection == "publicInquiries")
        query.addQueryItem("updateMask.fieldPaths", "createdAt");
    if (!privateExists && collection == "privateInquiries")
        query.addQueryItem("updateMask.fieldPaths", "createdAt");
    url.setQuery(query);

    QJsonObject encoded = firestoreFields(fields);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    encoded.insert("updatedAt", QJsonObject{{"timestampValue", now}});
    if ((!publicExists && collection == "publicInquiries")
        || (!privateExists && collection == "privateInquiries"))
        encoded.insert("createdAt", QJsonObject{{"timestampValue", now}});

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *reply = network.sendCustomRequest(
        request, "PATCH", QJsonDocument(QJsonObject{{"fields", encoded}}).toJson());
    const bool isPublic = collection == "publicInquiries";
    connect(reply, &QNetworkReply::finished, this, [this, reply, isPublic] {
        const auto response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            showError(response.value("error").toObject().value("message").toString(
                reply->errorString()));
            ui->statusbar->setProperty("saveFailed", true);
        } else if (isPublic) {
            publicExists = true;
        } else {
            privateExists = true;
        }
        if (isPublic)
            savingPublic = false;
        else
            savingPrivate = false;
        if (!savingPublic && !savingPrivate && !ui->statusbar->property("saveFailed").toBool()) {
            ui->statusbar->showMessage(QStringLiteral("Inquiry saved."));
        }
        if (!savingPublic && !savingPrivate)
            ui->statusbar->setProperty("saveFailed", false);
        updateButtons();
        reply->deleteLater();
    });
}

void MainIdyll::showError(const QString &message)
{
    ui->statusbar->showMessage(QStringLiteral("Error: %1").arg(message));
}

void MainIdyll::updateButtons()
{
    const bool loaded = publicLoaded && privateLoaded;
    ui->loadButton->setEnabled(!idToken.isEmpty());
    ui->saveButton->setEnabled(!publicSelectedCode.isEmpty() && !savingPublic);
    ui->savePrivateButton->setEnabled(!privateSelectedCode.isEmpty() && !savingPrivate);
    if (loaded && ui->statusbar->currentMessage() == QStringLiteral("Loading inquiry…"))
        ui->statusbar->showMessage(QStringLiteral("Inquiry loaded. Save to create or update both records."));
}
