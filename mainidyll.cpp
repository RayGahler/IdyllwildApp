#include "mainidyll.h"
#include "preferredcontact.h"

#include <ui_mainidyll.h>

#include <QApplication>
#include <QDateTime>
#include <QFontDatabase>
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
#include <QVariantAnimation>
#include <QtMath>
#include <QUrl>

namespace {
const QUrl defaultPendingSound(QStringLiteral("qrc:/sounds/pending-alert.wav"));
const QString pendingSoundKey = QStringLiteral("notifications/pendingSound");
const QStringList publicStatuses{QStringLiteral("Recieved"), QStringLiteral("In Repair"),
                                 QStringLiteral("Done!"), QStringLiteral("Canceled"),
                                 QStringLiteral("On Hold")};

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

int loginForumTitlePointSize(const QString &fontFamily)
{
    const QString title = QStringLiteral("ADMIN REPAIR INQUIRY FORUM");
    constexpr QSize logicalSize(820, 90);
    int pointSize = 24;
    for (; pointSize < 64; ++pointSize) {
        const QFont candidate(fontFamily, pointSize, QFont::Bold);
        if (QFontMetricsF(candidate).horizontalAdvance(title) > logicalSize.width() - 36)
            break;
    }
    return qMax(24, pointSize - 1);
}

void drawForumTitle(QPainter &painter, const QRectF &area, const QString &fontFamily,
                    qreal pointSize)
{
    const QString title = QStringLiteral("ADMIN REPAIR INQUIRY FORUM");
    const QFont titleFont(fontFamily, qRound(pointSize), QFont::Bold);
    QFontMetricsF metrics(titleFont);
    const QFont trademarkFont(fontFamily, qMax(9, qRound(pointSize * 2 / 5)), QFont::Bold);
    const QFontMetricsF trademarkMetrics(trademarkFont);
    QFont renderedFont = titleFont;
    renderedFont.setStyleStrategy(QFont::PreferAntialias);
    painter.setFont(renderedFont);
    const qreal titleWidth = metrics.horizontalAdvance(title);
    const qreal trademarkWidth = trademarkMetrics.horizontalAdvance(QStringLiteral("TM"));
    const qreal combinedWidth = titleWidth + trademarkWidth + 3;
    const qreal titleX = area.x() + (area.width() - combinedWidth) / 2.0;
    const qreal baseline = area.y() + (area.height() - metrics.height()) / 2.0 + metrics.ascent();
    QPainterPath titlePath;
    titlePath.addText(QPointF(titleX, baseline), renderedFont, title);
    QPainterPath trademarkPath;
    trademarkPath.addText(QPointF(titleX + titleWidth + 3,
                                  baseline - metrics.ascent() * 0.48),
                          trademarkFont, QStringLiteral("TM"));
    const QPen outline(QColor(QStringLiteral("#101C15")), 3.2,
                       Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(outline);
    painter.setBrush(QColor(QStringLiteral("#101C15")));
    painter.drawPath(titlePath);
    painter.drawPath(trademarkPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(titlePath);
    painter.drawPath(trademarkPath);
}

QPixmap renderLoginForumTitle(qreal pixelRatio, const QString &fontFamily)
{
    constexpr QSize logicalSize(820, 90);
    QImage image(QSize(qRound(logicalSize.width() * pixelRatio),
                       qRound(logicalSize.height() * pixelRatio)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(pixelRatio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    drawForumTitle(painter, QRectF(QPointF(0, 0), QSizeF(logicalSize)), fontFamily,
                   loginForumTitlePointSize(fontFamily));
    return QPixmap::fromImage(image);
}

QPixmap renderCompactForumTitle(qreal pixelRatio, const QString &fontFamily)
{
    const QString title = QStringLiteral("ADMIN REPAIR INQUIRY FORUM");
    const QFont titleFont(fontFamily, 32, QFont::Bold);
    const QFontMetricsF metrics(titleFont);
    const QFont trademarkFont(fontFamily, 13, QFont::Bold);
    const QFontMetricsF trademarkMetrics(trademarkFont);
    const qreal titleWidth = metrics.horizontalAdvance(title);
    const qreal trademarkWidth = trademarkMetrics.horizontalAdvance(QStringLiteral("TM"));
    const QSize logicalSize(qCeil(titleWidth + trademarkWidth + 16), 70);
    QImage image(QSize(qRound(logicalSize.width() * pixelRatio),
                       qRound(logicalSize.height() * pixelRatio)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(pixelRatio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    drawForumTitle(painter, QRectF(QPointF(0, 0), QSizeF(logicalSize)), fontFamily, 32);
    return QPixmap::fromImage(image);
}

class FlyingBrand final : public QWidget
{
public:
    FlyingBrand(const QPixmap &logo, const QString &fontFamily, qreal startFontSize,
                qreal endFontSize, const QRectF &logoStart, const QRectF &titleStart,
                const QRectF &logoEnd, const QRectF &titleEnd, QWidget *parent)
        : QWidget(parent), logo(logo), fontFamily(fontFamily), startFontSize(startFontSize),
          endFontSize(endFontSize), logoStart(logoStart), titleStart(titleStart),
          logoEnd(logoEnd), titleEnd(titleEnd)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

    void setProgress(qreal value)
    {
        progress = qBound<qreal>(0.0, value, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const auto interpolate = [this](const QRectF &from, const QRectF &to) {
            return QRectF(from.x() + (to.x() - from.x()) * progress,
                          from.y() + (to.y() - from.y()) * progress,
                          from.width() + (to.width() - from.width()) * progress,
                          from.height() + (to.height() - from.height()) * progress);
        };
        painter.drawPixmap(interpolate(logoStart, logoEnd), logo, QRectF(logo.rect()));
        drawForumTitle(painter, interpolate(titleStart, titleEnd), fontFamily,
                       startFontSize + (endFontSize - startFontSize) * progress);
    }

private:
    QPixmap logo;
    QString fontFamily;
    qreal startFontSize = 0.0;
    qreal endFontSize = 0.0;
    QRectF logoStart;
    QRectF titleStart;
    QRectF logoEnd;
    QRectF titleEnd;
    qreal progress = 0.0;
};
}

MainIdyll::MainIdyll(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainIdyll)
{
    ui->setupUi(this);
    auto *workspaceTopBar = new QFrame(ui->workspaceContents);
    workspaceTopBar->setObjectName(QStringLiteral("workspaceTopBar"));
    workspaceTopBar->setMinimumHeight(44);
    workspaceTopBar->setStyleSheet(QStringLiteral(
        "QFrame#workspaceTopBar { background: transparent; border: none; }"));
    auto *workspaceTopLayout = new QHBoxLayout(workspaceTopBar);
    workspaceTopLayout->setContentsMargins(8, 2, 8, 2);
    workspaceTopLayout->setSpacing(10);
    workspaceTopLayout->addStretch(1);
    workspaceTopLayout->addWidget(ui->greetingLabel);
    workspaceTopLayout->addWidget(ui->signOutButton);
    static_cast<QVBoxLayout *>(ui->workspaceContents->layout())->insertWidget(0, workspaceTopBar);
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget#centralwidget { background: #142A1E; color: #E8EFE9; }"
        "QScrollArea, QScrollArea > QWidget { background: transparent; border: none; }"
        "QWidget#workspaceContents { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #274A34, stop:0.5 #1A3324, stop:1 #142A1E); color: #E8EFE9; }"
        "QDialog, QGroupBox, QTabWidget::pane { background: #1F3D2B; color: #E8EFE9; border: 1px solid #3F6248; border-radius: 14px; }"
        "QGroupBox { margin-top: 12px; padding: 14px; font-weight: 700; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 5px; color: #F4EFE6; }"
        "QFrame#informationCard { background: #1A3324; border: 1px solid #3F6248; border-radius: 11px; }"
        "QLabel { color: #E8EFE9; }"
        "QLineEdit, QPlainTextEdit, QTextBrowser, QComboBox, QListWidget { background: #101C15; color: #E8EFE9; border: 1px solid #49624F; border-radius: 9px; padding: 5px 7px; selection-background-color: #CC7E3E; selection-color: #142A1E; }"
        "QListWidget::item:selected { background: #496F52; color: #F2F7F2; border: 1px solid #8EB795; }"
        "QComboBox QAbstractItemView { background: #101C15; color: #E8EFE9; selection-background-color: #CC7E3E; selection-color: #142A1E; }"
        "QPushButton { background: #CC7E3E; color: #142A1E; border: 1px solid #D99A68; border-radius: 16px; padding: 6px 13px; font-weight: 700; }"
        "QPushButton:hover { background: #D8925A; color: #142A1E; border-color: #E1A976; }"
        "QPushButton:pressed { background: #A96232; color: #FFFFFF; border-color: #A96232; }"
        "QPushButton:disabled { background: #26392B; color: #BAC0B9; border-color: #3B5040; }"
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
        "QPushButton#signOutButton { background: #CC7E3E; color: #142A1E; border: 1px solid #D99A68; border-radius: 16px; }"
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
    titleFontFamily = QStringLiteral("Arial");
    const int titleFontId = QFontDatabase::addApplicationFont(QStringLiteral(":/branding/PricedownBl.otf"));
    if (titleFontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(titleFontId);
        if (!families.isEmpty())
            titleFontFamily = families.first();
    }
    loginTitlePixmap = renderLoginForumTitle(ratio, titleFontFamily);
    ui->panelTitleLabel->setPixmap(loginTitlePixmap);
    QPixmap smallLogo = QPixmap::fromImage(
        logoImage.scaled(QSize(qRound(ui->mainLogoLabel->width() * ratio),
                               qRound(ui->mainLogoLabel->height() * ratio)),
                        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    smallLogo.setDevicePixelRatio(ratio);
    ui->mainLogoLabel->setPixmap(smallLogo);
    compactTitlePixmap = renderCompactForumTitle(ratio, titleFontFamily);
    ui->mainTitleLabel->setPixmap(compactTitlePixmap);
    const QSize compactTitleSize(qRound(compactTitlePixmap.width() / ratio),
                                 qRound(compactTitlePixmap.height() / ratio));
    ui->mainTitleLabel->setFixedSize(compactTitleSize);
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
    ui->greetingLabel->hide();
    ui->workspaceWidget->hide();
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
    const QSize refreshSize = ui->refreshPendingButton->sizeHint();
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
    ui->statusEdit->addItems(publicStatuses);
    ui->pendingStatusEdit->addItems(publicStatuses);
    ui->privateInquiryFilter->addItems({tr("All Inquiries"), tr("Completed"),
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
    ui->deletedSplitter->setCollapsible(0, false);
    ui->deletedSplitter->setCollapsible(1, false);
    ui->deletedSplitter->setSizes({250, 600});
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
    pendingTimer.setInterval(10000);
    connect(&pendingTimer, &QTimer::timeout, this, [this] {
        if (!idToken.isEmpty())
            updateGreeting();
        refreshPendingInquiries(true);
        refreshCollection(QStringLiteral("publicInquiries"));
        refreshCollection(QStringLiteral("privateInquiries"));
        refreshDeletedInquiries(true);
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

void MainIdyll::updateGreeting()
{
    const QString emailName = ui->emailEdit->text().trimmed()
                                  .section(QLatin1Char('@'), 0, 0).trimmed();
    const QString greetingName = emailName.isEmpty() ? tr("there") : emailName;
    const int hour = QDateTime::currentDateTime().time().hour();
    const QString timeGreeting = hour >= 5 && hour < 12 ? tr("Good morning")
                                 : hour < 17 ? tr("Good afternoon")
                                 : hour < 21 ? tr("Good evening")
                                              : tr("Good night");
    ui->greetingLabel->setText(QStringLiteral("%1, %2!").arg(timeGreeting, greetingName));
    ui->greetingLabel->adjustSize();
}

MainIdyll::~MainIdyll()
{
    delete ui;
}

void MainIdyll::showAuthenticatedWorkspace()
{
    const auto rectInCentral = [this](QWidget *widget) {
        return QRectF(widget->mapTo(ui->centralwidget, QPoint(0, 0)), widget->size());
    };
    const QRectF sourceLogoRect = rectInCentral(ui->businessLogoLabel);
    const QRectF sourceTitleRect = rectInCentral(ui->panelTitleLabel);

    ui->centralwidget->layout()->removeWidget(ui->loginBrandContainer);
    ui->centralwidget->layout()->removeWidget(ui->signInGroup);
    ui->websiteLabel->hide();
    ui->copyrightLabel->hide();
    ui->centralwidget->layout()->setContentsMargins(12, 12, 12, 12);

    auto *signInFade = new QGraphicsOpacityEffect(ui->signInGroup);
    signInFade->setOpacity(1.0);
    ui->signInGroup->setGraphicsEffect(signInFade);

    auto *workspaceFade = new QGraphicsOpacityEffect(ui->workspaceWidget);
    workspaceFade->setOpacity(0.0);
    ui->workspaceWidget->setGraphicsEffect(workspaceFade);
    auto *logoFade = new QGraphicsOpacityEffect(ui->mainLogoLabel);
    logoFade->setOpacity(0.0);
    ui->mainLogoLabel->setGraphicsEffect(logoFade);
    auto *titleFade = new QGraphicsOpacityEffect(ui->mainTitleLabel);
    titleFade->setOpacity(0.0);
    ui->mainTitleLabel->setGraphicsEffect(titleFade);
    ui->workspaceWidget->show();
    ui->mainBrandContainer->show();
    ui->workspaceContents->layout()->activate();
    ui->centralwidget->layout()->activate();
    QApplication::processEvents();

    auto *flyingBrand = new FlyingBrand(loginLogoPixmap, titleFontFamily,
                                        loginForumTitlePointSize(titleFontFamily), 32,
                                        sourceLogoRect, sourceTitleRect,
                                        rectInCentral(ui->mainLogoLabel),
                                        rectInCentral(ui->mainTitleLabel), ui->centralwidget);
    flyingBrand->setGeometry(ui->centralwidget->rect());
    flyingBrand->show();
    flyingBrand->raise();
    ui->loginBrandContainer->hide();

    auto *transition = new QParallelAnimationGroup(this);
    auto *fadeWorkspace = new QPropertyAnimation(workspaceFade, "opacity", transition);
    fadeWorkspace->setDuration(560);
    fadeWorkspace->setStartValue(0.0);
    fadeWorkspace->setEndValue(1.0);
    auto *fadeSignIn = new QPropertyAnimation(signInFade, "opacity", transition);
    fadeSignIn->setDuration(560);
    fadeSignIn->setStartValue(1.0);
    fadeSignIn->setEndValue(0.0);
    auto *morphBrand = new QVariantAnimation(transition);
    morphBrand->setDuration(850);
    morphBrand->setStartValue(0.0);
    morphBrand->setEndValue(1.0);
    morphBrand->setEasingCurve(QEasingCurve::InOutCubic);
    connect(morphBrand, &QVariantAnimation::valueChanged, flyingBrand,
            [flyingBrand](const QVariant &value) { flyingBrand->setProgress(value.toReal()); });
    transition->addAnimation(fadeWorkspace);
    transition->addAnimation(fadeSignIn);
    transition->addAnimation(morphBrand);
    connect(transition, &QParallelAnimationGroup::finished, this,
            [this, transition, flyingBrand] {
        ui->workspaceWidget->setGraphicsEffect(nullptr);
        ui->loginBrandContainer->hide();
        ui->signInGroup->hide();
        ui->signInGroup->setGraphicsEffect(nullptr);
        flyingBrand->hide();
        ui->mainLogoLabel->setGraphicsEffect(nullptr);
        ui->mainTitleLabel->setGraphicsEffect(nullptr);
        ui->mainBrandContainer->show();
        ui->signOutButton->show();
        flyingBrand->deleteLater();
        transition->deleteLater();
    });
    transition->start();
}

void MainIdyll::showSignedOutWorkspace()
{
    const auto rectInCentral = [this](QWidget *widget) {
        return QRectF(widget->mapTo(ui->centralwidget, QPoint(0, 0)), widget->size());
    };
    const QRectF sourceLogoRect = rectInCentral(ui->mainLogoLabel);
    const QRectF sourceTitleRect = rectInCentral(ui->mainTitleLabel);
    auto *workspaceLogoFade = new QGraphicsOpacityEffect(ui->mainLogoLabel);
    workspaceLogoFade->setOpacity(0.0);
    ui->mainLogoLabel->setGraphicsEffect(workspaceLogoFade);
    auto *workspaceTitleFade = new QGraphicsOpacityEffect(ui->mainTitleLabel);
    workspaceTitleFade->setOpacity(0.0);
    ui->mainTitleLabel->setGraphicsEffect(workspaceTitleFade);

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
    auto *signInFade = new QGraphicsOpacityEffect(ui->signInGroup);
    signInFade->setOpacity(0.0);
    ui->signInGroup->setGraphicsEffect(signInFade);
    auto *signOutFade = new QGraphicsOpacityEffect(ui->signOutButton);
    signOutFade->setOpacity(1.0);
    ui->signOutButton->setGraphicsEffect(signOutFade);

    ui->centralwidget->layout()->activate();
    auto *loginLogoFade = new QGraphicsOpacityEffect(ui->businessLogoLabel);
    loginLogoFade->setOpacity(0.0);
    ui->businessLogoLabel->setGraphicsEffect(loginLogoFade);
    auto *loginTitleFade = new QGraphicsOpacityEffect(ui->panelTitleLabel);
    loginTitleFade->setOpacity(0.0);
    ui->panelTitleLabel->setGraphicsEffect(loginTitleFade);
    QApplication::processEvents();
    auto *flyingBrand = new FlyingBrand(loginLogoPixmap, titleFontFamily, 32,
                                        loginForumTitlePointSize(titleFontFamily),
                                        sourceLogoRect, sourceTitleRect,
                                        rectInCentral(ui->businessLogoLabel),
                                        rectInCentral(ui->panelTitleLabel), ui->centralwidget);
    flyingBrand->setGeometry(ui->centralwidget->rect());
    flyingBrand->show();
    flyingBrand->raise();

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
    addFade(signInFade, 0.0, 1.0);
    addFade(signOutFade, 1.0, 0.0);
    auto *morphBrand = new QVariantAnimation(transition);
    morphBrand->setDuration(850);
    morphBrand->setStartValue(0.0);
    morphBrand->setEndValue(1.0);
    morphBrand->setEasingCurve(QEasingCurve::InOutCubic);
    connect(morphBrand, &QVariantAnimation::valueChanged, flyingBrand,
            [flyingBrand](const QVariant &value) { flyingBrand->setProgress(value.toReal()); });
    transition->addAnimation(morphBrand);
    connect(transition, &QParallelAnimationGroup::finished, this,
            [this, transition, flyingBrand] {
        ui->workspaceWidget->hide();
        ui->workspaceWidget->setGraphicsEffect(nullptr);
        ui->loginBrandContainer->show();
        ui->mainLogoLabel->setGraphicsEffect(nullptr);
        ui->mainTitleLabel->setGraphicsEffect(nullptr);
        ui->businessLogoLabel->setGraphicsEffect(nullptr);
        ui->panelTitleLabel->setGraphicsEffect(nullptr);
        ui->signInGroup->setGraphicsEffect(nullptr);
        ui->signOutButton->setGraphicsEffect(nullptr);
        ui->signOutButton->hide();
        ui->greetingLabel->hide();
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
