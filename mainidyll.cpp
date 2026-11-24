#include "mainidyll.h"
#include "preferredcontact.h"

#include <ui_mainidyll.h>

#include <QApplication>
#include <QFontDatabase>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QMenuBar>
#include <QPalette>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QSettings>
#include <QSlider>
#include <QWidgetAction>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QHBoxLayout>
#include <QEasingCurve>
#include <QTimer>
#include <QtMath>
#include <QUrl>

namespace {
const QUrl defaultPendingSound(QStringLiteral("qrc:/sounds/pending-alert.wav"));
const QString pendingSoundKey = QStringLiteral("notifications/pendingSound");
const QStringList publicStatuses{QStringLiteral("Recieved"), QStringLiteral("In Repair"),
                                 QStringLiteral("Done!"), QStringLiteral("Canceled"),
                                 QStringLiteral("On Hold")};

QPixmap renderStackedForumTitle(qreal pixelRatio, const QString &fontFamily,
                                int pointSize = 25, int lineHeight = 48,
                                QSize logicalSize = QSize(340, 210))
{
    const QStringList lines{QStringLiteral("A.DMIN"), QStringLiteral("R.EPAIR"),
                            QStringLiteral("I.NQUIRY"), QStringLiteral("F.ORUM")};
    QImage image(QSize(qRound(logicalSize.width() * pixelRatio),
                       qRound(logicalSize.height() * pixelRatio)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(pixelRatio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::white);
    QFont titleFont(fontFamily, pointSize, QFont::Bold);
    titleFont.setStyleStrategy(QFont::PreferAntialias);
    for (int row = 0; row < lines.size(); ++row) {
        const int baseline = pointSize + 4 + row * lineHeight;
        painter.setFont(titleFont);
        painter.drawText(QPointF(4, baseline), lines.at(row));
    }
    return QPixmap::fromImage(image);
}

QPixmap renderCompactForumTitle(qreal pixelRatio, const QString &fontFamily)
{
    constexpr QSize logicalSize(210, 64);
    QImage image(QSize(qRound(logicalSize.width() * pixelRatio),
                       qRound(logicalSize.height() * pixelRatio)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(pixelRatio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::white);
    painter.setFont(QFont(fontFamily, 32, QFont::Bold));
    painter.drawText(QRect(0, 0, logicalSize.width(), logicalSize.height()),
                     Qt::AlignCenter, QStringLiteral("A.R.I.F"));
    return QPixmap::fromImage(image);
}

class FlyingBrand final : public QWidget
{
public:
    FlyingBrand(const QPixmap &logo, const QPixmap &title, const QPixmap &compactTitle, QWidget *parent)
        : QWidget(parent), logo(logo), title(title), compactTitle(compactTitle)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const int padding = qBound(5, height() / 9, 14);
        const qreal compactProgress = qBound<qreal>(0.0, (height() - 78.0) / 142.0, 1.0);
        const int expandedIconSide = qMin(height() - padding * 2, width() / 3);
        const int iconSide = qRound(64 + (expandedIconSide - 64) * compactProgress);
        const QRect logoRect(padding, (height() - iconSide) / 2, iconSide, iconSide);
        painter.drawPixmap(logoRect, logo, logo.rect());

        const QRect titleArea(logoRect.right() + padding + 3, padding,
                              width() - logoRect.right() - padding * 2 - 3,
                              height() - padding * 2);
        const QSize titleSize = title.size().scaled(titleArea.size(), Qt::KeepAspectRatio);
        const QRect titleRect(titleArea.center().x() - titleSize.width() / 2,
                              titleArea.center().y() - titleSize.height() / 2,
                              titleSize.width(), titleSize.height());
        painter.setOpacity(compactProgress);
        painter.drawPixmap(titleRect, title, title.rect());
        painter.setOpacity(1.0 - compactProgress);
        painter.drawPixmap(titleRect, compactTitle, compactTitle.rect());
        painter.setOpacity(1.0);
    }

private:
    QPixmap logo;
    QPixmap title;
    QPixmap compactTitle;
};
}

MainIdyll::MainIdyll(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainIdyll)
{
    ui->setupUi(this);
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget#centralwidget, QWidget#workspaceContents { background: #142A1E; color: #E8EFE9; }"
        "QScrollArea, QScrollArea > QWidget > QWidget { background: #142A1E; border: none; }"
        "QDialog, QGroupBox, QTabWidget::pane { background: #1F3D2B; color: #E8EFE9; border: 1px solid #3F6248; border-radius: 14px; }"
        "QGroupBox { margin-top: 12px; padding: 14px; font-weight: 700; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 5px; color: #F4EFE6; }"
        "QLabel { color: #E8EFE9; }"
        "QLineEdit, QPlainTextEdit, QComboBox, QListWidget { background: #101C15; color: #E8EFE9; border: 1px solid #49624F; border-radius: 8px; padding: 7px; selection-background-color: #CC7E3E; selection-color: #142A1E; }"
        "QComboBox QAbstractItemView { background: #101C15; color: #E8EFE9; selection-background-color: #CC7E3E; selection-color: #142A1E; }"
        "QPushButton { background: #CC7E3E; color: #142A1E; border: 1px solid #D99A68; border-radius: 18px; padding: 8px 16px; font-weight: 700; }"
        "QPushButton:hover { background: #D8925A; color: #142A1E; border-color: #E1A976; }"
        "QPushButton:pressed { background: #A96232; color: #FFFFFF; border-color: #A96232; }"
        "QPushButton:disabled { background: #26392B; color: #BAC0B9; border-color: #3B5040; }"
        "QTabBar::tab { background: #1F3D2B; color: #DCE8DF; border: 1px solid #3F6248; border-bottom: none; border-top-left-radius: 9px; border-top-right-radius: 9px; padding: 9px 15px; margin-right: 3px; }"
        "QTabBar::tab:selected { background: #CC7E3E; color: #142A1E; border-color: #D99A68; }"
        "QTabBar::tab:hover:!selected { background: #3B413B; }"
        "QMenuBar, QMenu { background: #142A1E; color: #E8EFE9; }"
        "QMenu::item:selected { background: #CC7E3E; color: #142A1E; }"
        "QStatusBar { background: #101C15; color: #DCE8DF; }"
        "QPushButton#signOutButton { background: #CC7E3E; color: #142A1E; border: 1px solid #D99A68; }"
        "QPushButton#signOutButton:hover { background: #D8925A; color: #142A1E; border-color: #E1A976; }"));
    QPalette websitePalette = ui->websiteLabel->palette();
    websitePalette.setColor(QPalette::Link, QColor(QStringLiteral("#A7D5A8")));
    websitePalette.setColor(QPalette::LinkVisited, QColor(QStringLiteral("#A7D5A8")));
    ui->websiteLabel->setPalette(websitePalette);
    QImage logoImage(QStringLiteral(":/branding/idyllwild-logo.png"));
    QRect visibleLogoBounds;
    for (int y = 0; y < logoImage.height(); ++y) {
        for (int x = 0; x < logoImage.width(); ++x) {
            if (logoImage.pixelColor(x, y).alpha() > 8)
                visibleLogoBounds |= QRect(x, y, 1, 1);
        }
    }
    if (!visibleLogoBounds.isNull())
        logoImage = logoImage.copy(visibleLogoBounds);
    const qreal pixelRatio = devicePixelRatioF();
    const QSize logoPixels(qRound(ui->businessLogoLabel->width() * pixelRatio),
                           qRound(ui->businessLogoLabel->height() * pixelRatio));
    QPixmap logoPixmap = QPixmap::fromImage(
        logoImage.scaled(logoPixels, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoPixmap.setDevicePixelRatio(pixelRatio);
    loginLogoPixmap = logoPixmap;
    ui->businessLogoLabel->setPixmap(loginLogoPixmap);
    const qreal ratio = devicePixelRatioF();
    QString titleFontFamily = QStringLiteral("Arial");
    const int titleFontId = QFontDatabase::addApplicationFont(QStringLiteral(":/branding/PricedownBl.otf"));
    if (titleFontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(titleFontId);
        if (!families.isEmpty())
            titleFontFamily = families.first();
    }
    loginTitlePixmap = renderStackedForumTitle(ratio, titleFontFamily);
    ui->panelTitleLabel->setPixmap(loginTitlePixmap);
    QPixmap smallLogo = QPixmap::fromImage(
        logoImage.scaled(QSize(qRound(ui->mainLogoLabel->width() * ratio),
                               qRound(ui->mainLogoLabel->height() * ratio)),
                        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    smallLogo.setDevicePixelRatio(ratio);
    ui->mainLogoLabel->setPixmap(smallLogo);
    compactTitlePixmap = renderCompactForumTitle(ratio, titleFontFamily);
    ui->mainTitleLabel->setPixmap(compactTitlePixmap);
    ui->signOutButton->setParent(ui->centralwidget);
    ui->workspaceContents->layout()->removeWidget(ui->mainBrandContainer);
    ui->mainBrandContainer->setParent(ui->centralwidget);
    ui->mainBrandContainer->setAutoFillBackground(false);
    ui->mainBrandContainer->setAttribute(Qt::WA_NoSystemBackground);
    ui->mainLogoLabel->setAutoFillBackground(false);
    ui->mainLogoLabel->setAttribute(Qt::WA_NoSystemBackground);
    ui->mainTitleLabel->setAutoFillBackground(false);
    ui->mainTitleLabel->setAttribute(Qt::WA_NoSystemBackground);
    ui->mainBrandContainer->adjustSize();
    ui->mainBrandContainer->hide();
    ui->signOutButton->hide();
    ui->signOutButton->adjustSize();
    ui->workspaceWidget->hide();
    ui->statusEdit->addItems(publicStatuses);
    ui->pendingStatusEdit->addItems(publicStatuses);
    ui->privateInquiryFilter->addItems({tr("All Inquries"), tr("Completed"),
                                        tr("Pending Review/In-Repair"), tr("Canceled")});
    ui->privateInquiryList->setViewMode(QListView::IconMode);
    ui->privateInquiryList->setFlow(QListView::TopToBottom);
    ui->privateInquiryList->setWrapping(true);
    ui->privateInquiryList->setResizeMode(QListView::Adjust);
    ui->privateInquiryList->setMovement(QListView::Static);
    ui->privateInquiryList->setGridSize(QSize(176, 62));
    ui->privateInquiryList->setIconSize(QSize(0, 0));
    ui->privateInquiryList->setWordWrap(true);
    ui->privateInquiryList->setStyleSheet(QStringLiteral(
        "QListWidget::item { border-radius: 9px; margin: 3px; padding: 3px; }"));
    ui->privateInquiryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->privateInquiryList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
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
    const QRect sourceRect = ui->loginBrandContainer->geometry();
    auto *flyingBrand = new FlyingBrand(loginLogoPixmap, loginTitlePixmap, compactTitlePixmap, ui->centralwidget);
    flyingBrand->setGeometry(sourceRect);
    flyingBrand->show();
    flyingBrand->raise();

    ui->centralwidget->layout()->removeWidget(ui->loginBrandContainer);
    ui->centralwidget->layout()->removeWidget(ui->signInGroup);
    ui->websiteLabel->hide();
    ui->copyrightLabel->hide();
    ui->centralwidget->layout()->setContentsMargins(12, 96, 12, 12);

    auto *loginBrandFade = new QGraphicsOpacityEffect(ui->loginBrandContainer);
    loginBrandFade->setOpacity(1.0);
    ui->loginBrandContainer->setGraphicsEffect(loginBrandFade);
    auto *signInFade = new QGraphicsOpacityEffect(ui->signInGroup);
    signInFade->setOpacity(1.0);
    ui->signInGroup->setGraphicsEffect(signInFade);

    auto *workspaceFade = new QGraphicsOpacityEffect(ui->workspaceWidget);
    workspaceFade->setOpacity(0.0);
    ui->workspaceWidget->setGraphicsEffect(workspaceFade);
    ui->workspaceWidget->show();
    ui->workspaceContents->layout()->activate();
    ui->centralwidget->layout()->activate();
    QApplication::processEvents();

    const QRect targetRect(16, 14, ui->mainBrandContainer->width(), ui->mainBrandContainer->height());

    auto *transition = new QParallelAnimationGroup(this);
    auto *fadeWorkspace = new QPropertyAnimation(workspaceFade, "opacity", transition);
    fadeWorkspace->setDuration(560);
    fadeWorkspace->setStartValue(0.0);
    fadeWorkspace->setEndValue(1.0);
    auto *fadeLoginBrand = new QPropertyAnimation(loginBrandFade, "opacity", transition);
    fadeLoginBrand->setDuration(560);
    fadeLoginBrand->setStartValue(1.0);
    fadeLoginBrand->setEndValue(0.0);
    auto *fadeSignIn = new QPropertyAnimation(signInFade, "opacity", transition);
    fadeSignIn->setDuration(560);
    fadeSignIn->setStartValue(1.0);
    fadeSignIn->setEndValue(0.0);
    auto *landBrand = new QPropertyAnimation(flyingBrand, "geometry", transition);
    landBrand->setDuration(850);
    landBrand->setStartValue(sourceRect);
    landBrand->setEndValue(targetRect);
    landBrand->setEasingCurve(QEasingCurve::InOutCubic);
    transition->addAnimation(fadeWorkspace);
    transition->addAnimation(fadeLoginBrand);
    transition->addAnimation(fadeSignIn);
    transition->addAnimation(landBrand);
    connect(transition, &QParallelAnimationGroup::finished, this, [this, transition, flyingBrand] {
        ui->workspaceWidget->setGraphicsEffect(nullptr);
        ui->loginBrandContainer->hide();
        ui->loginBrandContainer->setGraphicsEffect(nullptr);
        ui->signInGroup->hide();
        ui->signInGroup->setGraphicsEffect(nullptr);
        ui->mainBrandContainer->setGeometry(16, 14, ui->mainBrandContainer->width(), ui->mainBrandContainer->height());
        ui->mainBrandContainer->show();
        ui->mainBrandContainer->raise();
        ui->signOutButton->show();
        ui->signOutButton->adjustSize();
        ui->signOutButton->raise();
        ui->signOutButton->move(ui->centralwidget->width() - ui->signOutButton->width() - 18,
                                ui->centralwidget->height() - ui->signOutButton->height() - 18);
        flyingBrand->hide();
        flyingBrand->deleteLater();
        transition->deleteLater();
    });
    transition->start();
}

void MainIdyll::showSignedOutWorkspace()
{
    const QRect sourceRect = ui->mainBrandContainer->geometry();
    auto *flyingBrand = new FlyingBrand(loginLogoPixmap, loginTitlePixmap, compactTitlePixmap, ui->centralwidget);
    flyingBrand->setGeometry(sourceRect);
    flyingBrand->show();
    flyingBrand->raise();
    ui->mainBrandContainer->hide();

    auto *mainLayout = static_cast<QVBoxLayout *>(ui->centralwidget->layout());
    mainLayout->insertWidget(0, ui->loginBrandContainer, 0, Qt::AlignHCenter);
    mainLayout->insertWidget(1, ui->signInGroup, 0, Qt::AlignHCenter);
    ui->centralwidget->layout()->setContentsMargins(9, 9, 9, 9);
    ui->loginBrandContainer->show();
    ui->signInGroup->show();
    ui->websiteLabel->hide();
    ui->copyrightLabel->hide();
    ui->signInButton->setEnabled(false);
    ui->signOutButton->setEnabled(false);

    auto *workspaceFade = new QGraphicsOpacityEffect(ui->workspaceWidget);
    workspaceFade->setOpacity(1.0);
    ui->workspaceWidget->setGraphicsEffect(workspaceFade);
    auto *loginBrandFade = new QGraphicsOpacityEffect(ui->loginBrandContainer);
    loginBrandFade->setOpacity(0.0);
    ui->loginBrandContainer->setGraphicsEffect(loginBrandFade);
    auto *signInFade = new QGraphicsOpacityEffect(ui->signInGroup);
    signInFade->setOpacity(0.0);
    ui->signInGroup->setGraphicsEffect(signInFade);
    auto *signOutFade = new QGraphicsOpacityEffect(ui->signOutButton);
    signOutFade->setOpacity(1.0);
    ui->signOutButton->setGraphicsEffect(signOutFade);

    ui->centralwidget->layout()->activate();
    QApplication::processEvents();
    const QRect targetRect = ui->loginBrandContainer->geometry();

    auto *transition = new QParallelAnimationGroup(this);
    auto addFade = [transition](QObject *object, qreal from, qreal to) {
        auto *animation = new QPropertyAnimation(object, "opacity", transition);
        animation->setDuration(760);
        animation->setStartValue(from);
        animation->setEndValue(to);
        animation->setEasingCurve(QEasingCurve::InOutCubic);
        transition->addAnimation(animation);
    };
    addFade(workspaceFade, 1.0, 0.0);
    addFade(loginBrandFade, 0.0, 1.0);
    addFade(signInFade, 0.0, 1.0);
    addFade(signOutFade, 1.0, 0.0);
    auto *landBrand = new QPropertyAnimation(flyingBrand, "geometry", transition);
    landBrand->setDuration(850);
    landBrand->setStartValue(sourceRect);
    landBrand->setEndValue(targetRect);
    landBrand->setEasingCurve(QEasingCurve::InOutCubic);
    transition->addAnimation(landBrand);
    connect(transition, &QParallelAnimationGroup::finished, this,
            [this, transition, flyingBrand] {
        ui->workspaceWidget->hide();
        ui->workspaceWidget->setGraphicsEffect(nullptr);
        ui->loginBrandContainer->setGraphicsEffect(nullptr);
        ui->signInGroup->setGraphicsEffect(nullptr);
        ui->signOutButton->setGraphicsEffect(nullptr);
        ui->signOutButton->hide();
        ui->websiteLabel->show();
        ui->copyrightLabel->show();
        ui->signInButton->setEnabled(true);
        flyingBrand->hide();
        flyingBrand->deleteLater();
        transition->deleteLater();
    });
    transition->start();
}

void MainIdyll::resizeEvent(QResizeEvent *event)
{
    if (event)
        QMainWindow::resizeEvent(event);
    if (ui && ui->centralwidget) {
        if (ui->mainBrandContainer->isVisible())
            ui->mainBrandContainer->move(16, 14);
        if (ui->signOutButton->isVisible())
            ui->signOutButton->move(ui->centralwidget->width() - ui->signOutButton->width() - 18,
                                    ui->centralwidget->height() - ui->signOutButton->height() - 18);
    }
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
