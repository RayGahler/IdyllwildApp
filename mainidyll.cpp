#include "mainidyll.h"
#include "repairstatus.h"
#include "brandstyle.h"
#include "preferredcontact.h"

#include <ui_mainidyll.h>

#include <QApplication>
#include <QDateTime>
#include <QFontMetricsF>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QLayout>
#include <QMenuBar>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPlainTextEdit>
#include <QFormLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QPixmap>
#include <QSequentialAnimationGroup>
#include <QSettings>
#include <QRegularExpression>
#include <QSlider>
#include <QWidgetAction>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWindow>
#include <QHBoxLayout>
#include <QTimer>
#include <QtMath>

#include <utility>
#include <QUrl>

namespace {
const QUrl defaultPendingSound(QStringLiteral("qrc:/sounds/pending-alert.wav"));
const QString pendingSoundKey = QStringLiteral("notifications/pendingSound");
const QString volumeKey = QStringLiteral("notifications/volume");
const QString inquiryFormTitle = QStringLiteral("ADMIN INQUIRY FIREBASE PANEL");

void convertFormToCards(QFormLayout *form)
{
    if (!form)
        return;
    auto *owner = qobject_cast<QLayout *>(form->parent());
    QWidget *formContainer = form->parentWidget();
    int formIndex = -1;
    if (owner) {
        for (int i = 0; i < owner->count(); ++i) {
            if (owner->itemAt(i)->layout() == form) {
                formIndex = i;
                break;
            }
        }
    }
    if (owner && formIndex < 0)
        return;

    struct Field { QLabel *label; QWidget *editor; bool wide; };
    QList<Field> fields;
    for (int row = 0; row < form->rowCount(); ++row) {
        auto *label = qobject_cast<QLabel *>(form->itemAt(row, QFormLayout::LabelRole)
                                                ? form->itemAt(row, QFormLayout::LabelRole)->widget()
                                                : nullptr);
        QWidget *editor = form->itemAt(row, QFormLayout::FieldRole)
            ? form->itemAt(row, QFormLayout::FieldRole)->widget() : nullptr;
        if (label && editor)
            fields.append({label, editor, qobject_cast<QPlainTextEdit *>(editor) != nullptr});
    }

    if (owner)
        owner->takeAt(formIndex);
    delete form;
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(9);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    int gridRow = 0;
    int column = 0;
    for (const Field &field : fields) {
        auto *card = new QFrame(formContainer);
        card->setObjectName(QStringLiteral("informationCard"));
        card->setFrameShape(QFrame::StyledPanel);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(10, 8, 10, 9);
        cardLayout->setSpacing(5);
        field.label->setParent(card);
        field.label->setAlignment(field.wide ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
        cardLayout->addWidget(field.label);
        field.editor->setParent(card);
        if (auto *line = qobject_cast<QLineEdit *>(field.editor))
            line->setPlaceholderText(QStringLiteral("—"));
        if (auto *text = qobject_cast<QPlainTextEdit *>(field.editor))
            text->setPlaceholderText(QObject::tr("N/A"));
        if (auto *combo = qobject_cast<QComboBox *>(field.editor))
            combo->setPlaceholderText(QStringLiteral("—"));
        cardLayout->addWidget(field.editor);
        if (field.wide) {
            if (column != 0) {
                ++gridRow;
                column = 0;
            }
            grid->addWidget(card, gridRow++, 0, 1, 2);
        } else {
            grid->addWidget(card, gridRow, column);
            if (++column == 2) {
                ++gridRow;
                column = 0;
            }
        }
    }
    if (column != 0)
        ++gridRow;
    grid->setRowStretch(gridRow, 1);
    if (owner)
        qobject_cast<QBoxLayout *>(owner)->insertLayout(formIndex, grid);
    else if (formContainer)
        formContainer->setLayout(grid);
}

struct FormTitle {
    FormTitle(const QString &family, qreal pointSize)
        : font(family, qRound(pointSize), QFont::Bold),
          markFont(family, qMax(9, qRound(pointSize * 0.4)), QFont::Bold),
          metrics(font), textWidth(metrics.horizontalAdvance(inquiryFormTitle)),
          width(textWidth + 3 + QFontMetricsF(markFont).horizontalAdvance(QStringLiteral("TM")))
    {
        font.setStyleStrategy(QFont::PreferAntialias);
    }

    QFont font;
    QFont markFont;
    QFontMetricsF metrics;
    qreal textWidth;
    qreal width;
};

int loginFormTitlePointSize(const QString &fontFamily, QSize canvas)
{
    for (int size = 63; size > 1; --size) {
        const FormTitle title(fontFamily, size);
        if (title.width + 12 <= canvas.width() && title.metrics.height() + 4 <= canvas.height())
            return size;
    }
    return 1;
}

void drawFormTitle(QPainter &painter, const QRectF &area, int pointSize, qsizetype visibleCharacters)
{
    const FormTitle title(BrandStyle::titleFontFamily(), pointSize);
    const qreal left = area.x() + (area.width() - title.width) / 2;
    const qreal baseline = area.y() + (area.height() - title.metrics.height()) / 2
        + title.metrics.ascent();
    QPainterPath lettering;
    lettering.addText(QPointF(left, baseline), title.font, inquiryFormTitle.left(visibleCharacters));
    if (visibleCharacters == inquiryFormTitle.size()) {
        lettering.addText(QPointF(left + title.textWidth + 3, baseline - title.metrics.ascent() * 0.48),
                          title.markFont, QStringLiteral("TM"));
    }
    const QColor outline(QStringLiteral("#101C15"));
    painter.setPen(QPen(outline, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(outline);
    painter.drawPath(lettering);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(lettering);
}

QPixmap renderFormTitle(QSize size, qreal ratio, int pointSize,
                        qsizetype visibleCharacters = inquiryFormTitle.size())
{
    QImage image(size * ratio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    drawFormTitle(painter, QRectF(QPointF(), QSizeF(size)), pointSize, visibleCharacters);
    painter.end();
    return QPixmap::fromImage(image);
}

QPixmap fadedPixmap(const QPixmap &source, qreal opacity)
{
    if (opacity >= 1.0 || source.isNull())
        return source;

    QPixmap frame(source.size());
    frame.setDevicePixelRatio(source.devicePixelRatio());
    frame.fill(Qt::transparent);
    QPainter painter(&frame);
    painter.setOpacity(opacity);
    painter.drawPixmap(0, 0, source);
    painter.end();
    return frame;
}

} // namespace

MainIdyll::MainIdyll(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainIdyll)
{
    ui->setupUi(this);
    qApp->installEventFilter(this);
    ui->screenStack->setCurrentWidget(ui->loginPage);
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    setUnifiedTitleAndToolBarOnMac(true);
    setWindowFlag(Qt::ExpandedClientAreaHint, true);
    setWindowFlag(Qt::NoTitleBarBackgroundHint, true);
    if (ui->centralwidget->layout()) {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        ui->centralwidget->layout()->getContentsMargins(&left, &top, &right, &bottom);
        baseCentralMargins = QMargins(left, top, right, bottom);
    }
#endif
    ui->signOutButton->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    ui->centralwidget->setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget#centralwidget, QDialog { background: %1; color: #E8EFE9; }"
        "QScrollArea, QScrollArea > QWidget, QWidget#workspaceContents { background: transparent; border: none; }"
        "QGroupBox, QTabWidget::pane { background: #1F3D2B; color: #E8EFE9; border: 1px solid #3F6248; border-radius: 14px; }"
        "QGroupBox { margin-top: 12px; padding: 14px; font-weight: 700; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 5px; color: #F4EFE6; }"
        "QFrame#informationCard { background: #1A3324; border: 1px solid #3F6248; border-radius: 11px; }"
        "QLabel { color: #E8EFE9; }"
        "QLineEdit, QPlainTextEdit, QTextBrowser, QComboBox, QListWidget { background: #101C15; color: #E8EFE9; border: 1px solid #49624F; border-radius: 9px; padding: 5px 7px; selection-background-color: #CC7E3E; selection-color: #142A1E; }"
        "QListWidget::item:selected { background: #496F52; color: #F2F7F2; border: 1px solid #8EB795; }"
        "QComboBox QAbstractItemView { background: #101C15; color: #E8EFE9; selection-background-color: #CC7E3E; selection-color: #142A1E; }"
        "QPushButton { background: #D9782B; color: #F3EFE4; border: 1px solid #C96824; border-radius: 22px; padding: 0 18px; min-height: 44px; max-height: 44px; font-weight: 700; }"
        "QPushButton:hover { background: #E4863D; color: #FFF9EF; border-color: #E99A5B; }"
        "QPushButton:pressed { background: #B85D20; color: #FFF9EF; border-color: #A94F18; }"
        "QPushButton:disabled { background: #A85B28; color: #F3EFE4; border-color: #984D21; }"
        "QTabBar::tab { background: #1F3D2B; color: #DCE8DF; border: 1px solid #3F6248; border-bottom: none; border-top-left-radius: 9px; border-top-right-radius: 9px; padding: 9px 15px; margin-right: 3px; }"
        "QTabBar::tab:selected { background: #CC7E3E; color: #142A1E; border-color: #D99A68; }"
        "QTabBar::tab:hover:!selected { background: #3B413B; }"
        "QMenuBar, QMenu { background: #142A1E; color: #E8EFE9; }"
        "QMenu::item:selected { background: #CC7E3E; color: #142A1E; }"
        "QScrollBar:vertical, QScrollBar:horizontal { background: rgba(225, 233, 228, 24); border: 1px solid rgba(220, 230, 224, 48); border-radius: 7px; margin: 3px; }"
        "QScrollBar:vertical { width: 12px; } QScrollBar:horizontal { height: 12px; }"
        "QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 rgba(222, 229, 225, 220), stop:0.45 rgba(155, 166, 159, 205), stop:1 rgba(112, 126, 117, 220)); border: 1px solid rgba(245, 250, 247, 135); border-radius: 6px; min-height: 30px; min-width: 30px; }"
        "QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 rgba(238, 243, 240, 240), stop:1 rgba(147, 160, 152, 235)); }"
        "QScrollBar::add-line, QScrollBar::sub-line { border: none; background: transparent; width: 0px; height: 0px; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
        "QStatusBar { background: #101C15; color: #DCE8DF; }"
        "QPushButton#signOutButton { min-height: 42px; max-height: 42px; background: #D9782B; color: #F3EFE4; border: 1px solid #C96824; border-radius: 22px; }"
        "QPushButton#signOutButton:hover { background: #E4863D; color: #FFF9EF; border-color: #E99A5B; }")
        .arg(BrandStyle::windowBackground));
    const QString explicitPillStyle = QStringLiteral(
        "QPushButton { background: #D9782B; color: #F3EFE4; border: 1px solid #C96824; "
        "border-radius: 22px; padding: 0 18px; font-weight: 700; }"
        "QPushButton:hover { background: #E4863D; color: #FFF9EF; border-color: #E99A5B; }"
        "QPushButton:pressed { background: #B85D20; color: #FFF9EF; border-color: #A94F18; }"
        "QPushButton:disabled { background: #A85B28; color: #F3EFE4; border-color: #984D21; }");
    for (QPushButton *button : findChildren<QPushButton *>()) {
        button->setStyleSheet(explicitPillStyle);
        button->setFixedHeight(44);
    }
    QPalette websitePalette = ui->websiteLabel->palette();
    websitePalette.setColor(QPalette::Link, QColor(QStringLiteral("#A7D5A8")));
    websitePalette.setColor(QPalette::LinkVisited, QColor(QStringLiteral("#A7D5A8")));
    ui->websiteLabel->setPalette(websitePalette);
    logoImage.load(QStringLiteral(":/branding/idyllwild-logo.png"));
    QRect visibleLogoBounds;
    for (int y = 0; y < logoImage.height(); ++y) {
        for (int x = 0; x < logoImage.width(); ++x) {
            if (logoImage.pixelColor(x, y).alpha() > 8)
                visibleLogoBounds |= QRect(x, y, 1, 1);
        }
    }
    if (!visibleLogoBounds.isNull())
        logoImage = logoImage.copy(visibleLogoBounds);
    loginTitleSize = loginFormTitlePointSize(BrandStyle::titleFontFamily(), ui->panelTitleLabel->size());
    loginBrandAnimation.setDuration(900);
    loginBrandAnimation.setStartValue(0.0);
    loginBrandAnimation.setEndValue(1.0);
    connect(&loginBrandAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        renderLoginBrand(value.toReal());
    });
    ui->panelTitleLabel->setAccessibleName(inquiryFormTitle);
    ui->mainTitleLabel->setAccessibleName(inquiryFormTitle);
    ui->mainLogoLabel->setAccessibleName(tr("Idyllwild Electronic Repair logo"));
    refreshBrandPixmaps();
    ui->mainBrandContainer->setAutoFillBackground(false);
    ui->mainBrandContainer->setAttribute(Qt::WA_NoSystemBackground);
    ui->mainLogoLabel->setAutoFillBackground(false);
    ui->mainLogoLabel->setAttribute(Qt::WA_NoSystemBackground);
    ui->mainTitleLabel->setAutoFillBackground(false);
    ui->mainTitleLabel->setAttribute(Qt::WA_NoSystemBackground);
    ui->mainBrandContainer->adjustSize();
    ui->permanentlyDeleteButton->hide();
    for (QLayout *layout : {static_cast<QLayout *>(ui->pendingLayout),
                            static_cast<QLayout *>(ui->publicCollectionLayout),
                            static_cast<QLayout *>(ui->privateCollectionLayout),
                            static_cast<QLayout *>(ui->deletedLayout)}) {
        layout->setContentsMargins(9, 8, 9, 8);
        layout->setSpacing(7);
    }
    for (QFormLayout *form : {ui->pendingForm, ui->publicForm, ui->privateForm}) {
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(5);
    }
    convertFormToCards(ui->pendingForm);
    convertFormToCards(ui->publicForm);
    convertFormToCards(ui->privateForm);
    ui->pendingCodeLabel->hide();
    const QSize refreshSize(ui->refreshPendingButton->sizeHint().width(), 44);
    for (QPushButton *button : {ui->refreshPendingButton, ui->refreshPublicButton,
                                ui->refreshPrivateButton, ui->refreshDeletedButton})
        button->setFixedSize(refreshSize);
    ui->pendingList->setMaximumWidth(230);
    ui->publicInquiryList->setMaximumWidth(230);
    ui->privateInquiryList->setMaximumWidth(230);
    ui->deletedInquiryList->setMaximumWidth(260);
    const QList<QPlainTextEdit *> compactTextFields{
        ui->pendingProblemEdit, ui->pendingAddressEdit, ui->pendingMessageEdit,
        ui->publicMessageEdit, ui->problemEdit, ui->addressEdit
    };
    for (QPlainTextEdit *field : compactTextFields) {
        field->setMinimumHeight(54);
        field->setMaximumHeight(76);
    }
    for (QListWidget *list : {ui->pendingList, ui->publicInquiryList,
                              ui->privateInquiryList, ui->deletedInquiryList}) {
        list->setStyleSheet(QStringLiteral(
            "QListWidget { padding: 4px; }"
            "QListWidget::item { border-radius: 9px; margin: 2px; padding: 5px 7px; }"
            "QListWidget::item:selected { border: 1px solid #D99A68; }"));
    }
    for (QHBoxLayout *toolbar : {ui->pendingToolbar, ui->publicCollectionToolbar,
                                 ui->privateCollectionToolbar, ui->deletedToolbar}) {
        toolbar->setContentsMargins(0, 0, 0, 0);
        toolbar->setSpacing(8);
    }
    ui->pendingLayout->setStretch(1, 1);
    ui->publicCollectionLayout->setStretch(1, 1);
    ui->privateCollectionLayout->setStretch(1, 1);
    ui->deletedLayout->setStretch(1, 1);
    RepairStatus::addOptions(*ui->statusEdit);
    RepairStatus::addOptions(*ui->pendingStatusEdit);
    updateInvoiceButtons();
    RepairStatus::addOptions(*ui->privateInquiryFilter,
        {tr("All Inquiries"), tr("Pending Review/In-Repair"), RepairStatus::readyForPickup,
         RepairStatus::pendingPaymentReview, tr("Completed"), tr("Canceled")});
    ui->privateInquiryList->setViewMode(QListView::IconMode);
    ui->privateInquiryList->setFlow(QListView::TopToBottom);
    ui->privateInquiryList->setWrapping(true);
    ui->privateInquiryList->setResizeMode(QListView::Adjust);
    ui->privateInquiryList->setMovement(QListView::Static);
    ui->privateInquiryList->setGridSize(QSize(204, 62));
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
        updatePermanentDeleteButton();
        updateInvoiceButtons();
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
    ui->deletedSplitter->setCollapsible(0, false);
    ui->deletedSplitter->setCollapsible(1, false);
    ui->deletedSplitter->setSizes({250, 600});
    connect(ui->publicInquiryList, &QListWidget::itemSelectionChanged, this, &MainIdyll::updateDeleteButton);
    connect(ui->privateInquiryList, &QListWidget::itemSelectionChanged, this, &MainIdyll::updateDeleteButton);
    connect(ui->saveButton, &QPushButton::clicked, this, &MainIdyll::saveInquiry);
    connect(ui->generateSquareLinkButton, &QPushButton::clicked, this, &MainIdyll::generateSquarePaymentLink);
    connect(ui->reviewPaymentButton, &QPushButton::clicked, this, &MainIdyll::reviewInvoicePayment);
    connect(ui->savePrivateButton, &QPushButton::clicked, this, &MainIdyll::savePrivateInquiry);
    connect(ui->createInvoiceButton, &QPushButton::clicked, this, &MainIdyll::openInvoiceMaker);
    connect(ui->uploadInvoiceButton, &QPushButton::clicked, this, &MainIdyll::publishExistingInvoice);
    connect(ui->refreshPublicButton, &QPushButton::clicked, this, [this] {
        refreshCollection("publicInquiries");
    });
    connect(ui->refreshPrivateButton, &QPushButton::clicked, this, [this] {
        refreshCollection("privateInquiries");
    });
    connect(ui->publicInquiryList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        loadCollectionRecord(true, item ? item->data(Qt::UserRole).toString() : QString());
    });
    connect(ui->privateInquiryList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        loadCollectionRecord(false, item ? item->data(Qt::UserRole).toString() : QString());
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
        }
        loadPendingInquiry();
    });
    pendingTimer.setInterval(10000);
    connect(&pendingTimer, &QTimer::timeout, this, [this] {
        if (!idToken.isEmpty())
            updateGreeting();
        refreshPendingInquiries(true);
        refreshCollection(QStringLiteral("publicInquiries"));
        refreshCollection(QStringLiteral("privateInquiries"));
        refreshDeletedInquiries(true);
        if (QDateTime::currentSecsSinceEpoch() - squareConnectionCheckedAt >= 60)
            refreshSquareConnection();
        checkPendingCardPayments();
    });

    const QString soundPath = QSettings().value(pendingSoundKey).toString();
    connect(&pendingSound, &QSoundEffect::statusChanged, this, [this] {
        if (pendingSound.status() == QSoundEffect::Error && pendingSound.source() != defaultPendingSound) {
            showError(tr("Could not load the selected alert sound. Using the default sound."));
            QSettings().remove(pendingSoundKey);
            pendingSound.setSource(defaultPendingSound);
        }
    });
    pendingSound.setSource(soundPath.isEmpty() ? defaultPendingSound : QUrl::fromLocalFile(soundPath));
    const qreal storedVolume = QSettings().value(volumeKey, 0.7).toDouble();
    const qreal volume = qBound(0.0, storedVolume > 1.0 ? storedVolume / 100.0 : storedVolume, 1.0);
    pendingSound.setVolume(volume);
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
#ifdef Q_OS_MACOS
    squareConnectionStatus = preferences->addAction(tr("Square API: Not connected"));
    squareConnectionStatus->setObjectName(QStringLiteral("squareConnectionStatus"));
    squareConnectionStatus->setEnabled(false);
    connect(preferences, &QMenu::aboutToShow, this, &MainIdyll::refreshSquareConnection);
    auto *square = menuBar()->addMenu(tr("Square"));
    square->addAction(tr("Set up Square…"), this, &MainIdyll::configureSquarePayments);
#endif
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
    volumeSlider->setValue(qRound(volume * 100));
    volumeLayout->addWidget(volumeSlider);
    volumeAction->setDefaultWidget(volumeControl);
    preferences->addAction(volumeAction);
    connect(volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        const qreal volume = value / 100.0;
        pendingSound.setVolume(volume);
        QSettings().setValue(volumeKey, volume);
    });
    menuBar()->hide();
    connect(ui->promotePendingButton, &QPushButton::clicked, this, &MainIdyll::promotePendingInquiry);
    ui->createInquiryButton->setEnabled(false);
    ui->promotePendingButton->setEnabled(false);
    setPendingFieldsEnabled(false);
}

void MainIdyll::updateGreeting()
{
    const QString emailName = ui->emailEdit->text().trimmed()
                                  .section(QLatin1Char('@'), 0, 0).trimmed();
    const QString greetingName = emailName.isEmpty() ? tr("there") : emailName;
    const int hour = QDateTime::currentDateTime().time().hour();
    const QString timeGreeting = hour < 5 ? tr("Good night")
                                 : hour < 12 ? tr("Good morning")
                                 : hour < 17 ? tr("Good afternoon")
                                 : hour < 21 ? tr("Good evening")
                                              : tr("Good night");
    ui->greetingLabel->setText(QStringLiteral("%1, %2!").arg(timeGreeting, greetingName));
}

MainIdyll::~MainIdyll()
{
    qApp->removeEventFilter(this);
    delete ui;
}

void MainIdyll::transitionTo(QWidget *page, std::function<void()> onSwitch)
{
    screenTransitionInProgress = true;
    ui->screenStack->setAttribute(Qt::WA_TransparentForMouseEvents);

    QList<QWidget *> surfaces{ui->screenStack, ui->statusbar};
    if (!menuBar()->isNativeMenuBar())
        surfaces.append(menuBar());

    QList<QGraphicsOpacityEffect *> effects;
    for (auto *surface : surfaces) {
        auto *opacity = new QGraphicsOpacityEffect(surface);
        opacity->setOpacity(1.0);
        surface->setGraphicsEffect(opacity);
        effects.append(opacity);
    }

    auto *transition = new QSequentialAnimationGroup(this);
    const auto addFade = [transition, effects](qreal from, qreal to) {
        auto *fade = new QVariantAnimation;
        fade->setStartValue(from);
        fade->setEndValue(to);
        fade->setDuration(240);
        fade->setEasingCurve(QEasingCurve::InOutCubic);
        connect(fade, &QVariantAnimation::valueChanged, fade,
                [effects](const QVariant &value) {
            for (auto *effect : effects)
                effect->setOpacity(value.toReal());
        });
        transition->addAnimation(fade);
        return fade;
    };

    auto *fadeOut = addFade(1.0, 0.0);
    addFade(0.0, 1.0);
    connect(fadeOut, &QVariantAnimation::finished, this,
            [this, page, onSwitch = std::move(onSwitch)] {
        onSwitch();
        if (page == ui->loginPage) {
            loginBrandAnimation.stop();
            loginBrandAnimation.setCurrentTime(0);
            loginBrandAnimation.start();
        }
        ui->screenStack->setCurrentWidget(page);
        ui->screenStack->layout()->activate();
    });
    connect(transition, &QSequentialAnimationGroup::finished, this, [this, surfaces] {
        for (auto *surface : surfaces)
            surface->setGraphicsEffect(nullptr);
        ui->screenStack->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        screenTransitionInProgress = false;
    });
    transition->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainIdyll::resizeEvent(QResizeEvent *event)
{
    if (event)
        QMainWindow::resizeEvent(event);
    applyMacSafeArea();
    QSettings().setValue(QStringLiteral("window/size"), size());
}

void MainIdyll::refreshBrandPixmaps()
{
    const qreal ratio = devicePixelRatioF();
    if (!loginLogoPixmap.isNull() && qFuzzyCompare(loginLogoPixmap.devicePixelRatio(), ratio))
        return;
    const auto scaledLogo = [this, ratio](QSize size) {
        QPixmap pixmap = QPixmap::fromImage(
            logoImage.scaled(size * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        pixmap.setDevicePixelRatio(ratio);
        return pixmap;
    };
    loginLogoPixmap = scaledLogo(ui->businessLogoLabel->size());
    renderLoginBrand(loginBrandAnimation.currentValue().toReal());
    ui->mainLogoLabel->setPixmap(scaledLogo(ui->mainLogoLabel->size() - QSize(14, 14)));
    const int compactWidth = qCeil(FormTitle(BrandStyle::titleFontFamily(), 32).width) + 16;
    const QSize compactSize(compactWidth, 68);
    ui->mainTitleLabel->setFixedSize(compactSize + QSize(2, 2));
    ui->mainTitleLabel->setPixmap(renderFormTitle(compactSize, ratio, 32));
}

void MainIdyll::renderLoginBrand(qreal progress)
{
    const qreal logoOpacity = QEasingCurve(QEasingCurve::OutCubic)
                                 .valueForProgress(qMin(progress * 1.7, 1.0));
    ui->businessLogoLabel->setPixmap(fadedPixmap(loginLogoPixmap, logoOpacity));

    // Keep the finished title centered while revealing its characters from the left.
    const qreal typing = qBound(0.0, (progress - 0.1) / 0.9, 1.0);
    const auto visibleCharacters = qsizetype(typing * inquiryFormTitle.size());
    const auto title = renderFormTitle(ui->panelTitleLabel->size(), devicePixelRatioF(),
                                       loginTitleSize, visibleCharacters);
    ui->panelTitleLabel->setPixmap(fadedPixmap(title, qMin(typing * 2.0, 1.0)));
}

void MainIdyll::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    refreshBrandPixmaps();
    applyMacSafeArea();
    if (ui->screenStack->currentWidget() == ui->loginPage && loginBrandAnimation.currentTime() == 0
        && loginBrandAnimation.state() == QAbstractAnimation::Stopped) {
        loginBrandAnimation.start();
    }
}

bool MainIdyll::eventFilter(QObject *watched, QEvent *event)
{
    if (screenTransitionInProgress
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease
            || event->type() == QEvent::ShortcutOverride)) {
        const auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->window() == this) {
            event->accept();
            return true;
        }
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (watched == this && event->type() == QEvent::DevicePixelRatioChange && loginTitleSize > 0)
        refreshBrandPixmaps();
#endif
    return QMainWindow::eventFilter(watched, event);
}

void MainIdyll::applyMacSafeArea()
{
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    if (!windowHandle() || !ui->centralwidget->layout())
        return;
    const QMargins safe = windowHandle()->safeAreaMargins();
    ui->centralwidget->layout()->setContentsMargins(
        baseCentralMargins.left() + safe.left(), baseCentralMargins.top() + safe.top(),
        baseCentralMargins.right() + safe.right(), baseCentralMargins.bottom() + safe.bottom());
#endif
}

void MainIdyll::showError(const QString &message)
{
    ui->statusbar->showMessage(QStringLiteral("Error: %1").arg(message));
}

void MainIdyll::updateInquiryActions()
{
    const bool canSave = !idToken.isEmpty() && !savingInquiry;
    ui->saveButton->setEnabled(canSave && publicInquiry.loaded);
    ui->savePrivateButton->setEnabled(canSave && privateInquiry.loaded);
    ui->reviewPaymentButton->setEnabled(canSave && publicInquiry.loaded && !paymentReviewInProgress);
    updateInvoiceButtons();
    updateDeleteButton();
}

QString MainIdyll::invoiceInquiryCode() const
{
    const auto *tab = ui->inquiryTabs->currentWidget();
    if (tab == ui->privateTab && privateInquiry.loaded)
        return privateInquiry.code;
    if (tab == ui->publicTab && publicInquiry.loaded)
        return publicInquiry.code;
    return {};
}

QString MainIdyll::invoiceInquiryStatus() const
{
    return publicStatusByCode.value(invoiceInquiryCode()).trimmed();
}

bool MainIdyll::invoiceStatusEligible() const
{
    QString status = invoiceInquiryStatus().toLower();
    status.replace(QLatin1Char('-'), QLatin1Char(' '));
    status.replace(QLatin1Char('_'), QLatin1Char(' '));
    status.replace(QRegularExpression(QStringLiteral("\\s*/\\s*")), QStringLiteral("/"));
    status = status.simplified();
    static const QSet<QString> statuses{
        QStringLiteral("done"), QStringLiteral("done!"), QStringLiteral("completed"),
        QStringLiteral("received"), QStringLiteral("recieved"), QStringLiteral("pending review"),
        QStringLiteral("pending review/in repair"), QStringLiteral("in repair"), QStringLiteral("in progress"),
        QStringLiteral("awaiting payment"), QStringLiteral("ready for pickup"),
        QStringLiteral("pending payment review")
    };
    return statuses.contains(status);
}

void MainIdyll::updateInvoiceButtons()
{
    const QString code = invoiceInquiryCode();
    const bool available = !code.isEmpty() && invoiceStatusEligible();
    const bool enabled = available && !idToken.isEmpty() && !localTestMode
        && !invoiceUploadInProgress && !generatingSquareLink;
    ui->createInvoiceButton->setEnabled(enabled);
    ui->uploadInvoiceButton->setEnabled(enabled);
    const QString unavailable = tr("Load an active repair or completed inquiry to publish an invoice.");
    ui->createInvoiceButton->setToolTip(available ? tr("Create an invoice for %1.").arg(code) : unavailable);
    ui->uploadInvoiceButton->setToolTip(available ? tr("Publish an existing PDF or renew an invoice link.") : unavailable);

    const QString publicStatus = publicStatusByCode.value(publicInquiry.code);
    const bool active = publicStatus != RepairStatus::done && publicStatus != QStringLiteral("Canceled")
        && publicStatus != RepairStatus::pendingPaymentReview;
    const bool canGenerate = publicInquiry.loaded && publicInquiry.hasPayableInvoice && active
        && squareConnection == SquareConnection::Connected
        && !idToken.isEmpty() && !localTestMode && !generatingSquareLink && !invoiceUploadInProgress;
    ui->generateSquareLinkButton->setEnabled(canGenerate && !publicInquiry.hasSquareLink);
    ui->generateSquareLinkButton->setText(publicInquiry.hasSquareLink
        ? tr("Square link published") : tr("Generate Square link"));
    ui->generateSquareLinkButton->setToolTip(publicInquiry.hasSquareLink
        ? tr("The checkout appears on the website when the repair is ready for pickup.")
        : tr("Publish the invoice and connect Square to generate a checkout for its grand total."));
}
