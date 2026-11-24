#include "mainidyll.h"
#include "preferredcontact.h"

#include <ui_mainidyll.h>

#include <QApplication>
#include <QFontDatabase>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QMenuBar>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QSettings>
#include <QSlider>
#include <QWidgetAction>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QUrl>

namespace {
const QUrl defaultPendingSound(QStringLiteral("qrc:/sounds/pending-alert.wav"));
const QString pendingSoundKey = QStringLiteral("notifications/pendingSound");
const QStringList publicStatuses{QStringLiteral("Recieved"), QStringLiteral("In Repair"),
                                 QStringLiteral("Done!"), QStringLiteral("Canceled"),
                                 QStringLiteral("On Hold")};
}

MainIdyll::MainIdyll(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainIdyll)
{
    ui->setupUi(this);
    ui->businessLogoLabel->setPixmap(QPixmap(QStringLiteral(":/branding/idyllwild-logo.png"))
                                         .scaled(ui->businessLogoLabel->size(), Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation));
    const int panelFontId = QFontDatabase::addApplicationFont(QStringLiteral(":/branding/PricedownBl.otf"));
    if (panelFontId >= 0) {
        const QStringList fontFamilies = QFontDatabase::applicationFontFamilies(panelFontId);
        if (!fontFamilies.isEmpty()) {
            QFont panelFont(fontFamilies.first());
            panelFont.setPointSize(18);
            ui->panelTitleLabel->setFont(panelFont);
        }
    }
    ui->workspaceWidget->hide();
    ui->signOutButton->hide();
    ui->statusEdit->addItems(publicStatuses);
    ui->pendingStatusEdit->addItems(publicStatuses);
    ui->privateInquiryFilter->addItems({tr("All inquiries"), tr("Completed"), tr("New Inquiry"), tr("Canceled")});
    resize(QSettings().value(QStringLiteral("window/size"), QSize(900, 720)).toSize());
    setPreferredContact(*ui->pendingPreferredEdit);
    setPreferredContact(*ui->preferredContactEdit);
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
    connect(ui->permanentlyDeleteButton, &QPushButton::clicked, this, &MainIdyll::permanentlyDeleteInquiry);
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
    connect(ui->publicInquiryList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        const QString code = item ? item->data(Qt::UserRole + 1).toString() : QString();
        publicSelectedCode = code;
        ui->saveButton->setEnabled(false);
        if (!code.isEmpty())
            loadCollectionRecord("publicInquiries", code);
    });
    connect(ui->privateInquiryList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        const QString code = item ? item->data(Qt::UserRole + 1).toString() : QString();
        privateSelectedCode = code;
        ui->savePrivateButton->setEnabled(false);
        if (!code.isEmpty())
            loadCollectionRecord("privateInquiries", code);
    });
    connect(ui->privateInquiryFilter, &QComboBox::currentTextChanged,
            this, &MainIdyll::applyPrivateInquiryFilter);
    connect(ui->createInquiryButton, &QPushButton::clicked, this, &MainIdyll::openCreateInquiry);
    connect(ui->createContactButton, &QPushButton::clicked, this, [this] { openRepairExport(false); });
    connect(ui->scheduleRepairButton, &QPushButton::clicked, this, [this] { openRepairExport(true); });
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
    pendingSound.setVolume(QSettings().value(QStringLiteral("notifications/volume"), 70).toDouble() / 100.0);
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
    auto *preferences = menuBar()->addMenu(tr("Preferences"));
    auto *windowSize = preferences->addMenu(tr("Application size"));
    const QList<QPair<QString, QSize>> sizes{{tr("Compact (800 × 640)"), QSize(800, 640)},
                                             {tr("Standard (900 × 720)"), QSize(900, 720)},
                                             {tr("Large (1200 × 900)"), QSize(1200, 900)}};
    for (const auto &entry : sizes) {
        auto *action = windowSize->addAction(entry.first);
        connect(action, &QAction::triggered, this, [this, size = entry.second] {
            resize(size);
            QSettings().setValue(QStringLiteral("window/size"), size);
        });
    }
    auto *volumeAction = new QWidgetAction(this);
    auto *volumeControl = new QWidget(this);
    auto *volumeLayout = new QVBoxLayout(volumeControl);
    volumeLayout->setContentsMargins(12, 6, 12, 6);
    volumeLayout->addWidget(new QLabel(tr("Notification sound volume"), volumeControl));
    auto *volumeSlider = new QSlider(Qt::Horizontal, volumeControl);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(qRound(QSettings().value(QStringLiteral("notifications/volume"), 70).toDouble() * 100));
    volumeLayout->addWidget(volumeSlider);
    volumeAction->setDefaultWidget(volumeControl);
    preferences->addAction(volumeAction);
    connect(volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        const qreal volume = value / 100.0;
        pendingSound.setVolume(volume);
        QSettings().setValue(QStringLiteral("notifications/volume"), volume);
    });
    menuBar()->hide();
    connect(ui->promotePendingButton, &QPushButton::clicked, this, &MainIdyll::promotePendingInquiry);
    ui->createInquiryButton->setEnabled(false);
    ui->promotePendingButton->setEnabled(false);
    setPendingFieldsEnabled(false);
}

MainIdyll::~MainIdyll()
{
    delete ui;
}

void MainIdyll::showAuthenticatedWorkspace()
{
    ui->signInGroup->setTitle(tr("Admin session"));
    ui->emailEdit->hide();
    ui->passwordEdit->hide();
    ui->signInButton->hide();
    ui->signOutButton->show();

    auto *sessionFade = new QGraphicsOpacityEffect(ui->signInGroup);
    auto *workspaceFade = new QGraphicsOpacityEffect(ui->workspaceWidget);
    sessionFade->setOpacity(1.0);
    workspaceFade->setOpacity(0.0);
    ui->signInGroup->setGraphicsEffect(sessionFade);
    ui->workspaceWidget->setGraphicsEffect(workspaceFade);
    ui->workspaceWidget->show();

    auto *transition = new QParallelAnimationGroup(this);
    auto *fadeSession = new QPropertyAnimation(sessionFade, "opacity", transition);
    fadeSession->setDuration(220);
    fadeSession->setStartValue(1.0);
    fadeSession->setEndValue(0.0);
    auto *fadeWorkspace = new QPropertyAnimation(workspaceFade, "opacity", transition);
    fadeWorkspace->setDuration(360);
    fadeWorkspace->setStartValue(0.0);
    fadeWorkspace->setEndValue(1.0);
    transition->addAnimation(fadeSession);
    transition->addAnimation(fadeWorkspace);
    connect(transition, &QParallelAnimationGroup::finished, this, [this, transition] {
        ui->signInGroup->setGraphicsEffect(nullptr);
        ui->workspaceWidget->setGraphicsEffect(nullptr);
        transition->deleteLater();
    });
    transition->start();
}

void MainIdyll::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    QSettings().setValue(QStringLiteral("window/size"), size());
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
