#include "invoice.h"
#include "brandstyle.h"

#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextDocument>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtMath>

#include <array>

namespace {
QString moneyText(qint64 cents)
{
    const bool negative = cents < 0;
    const qint64 absolute = qAbs(cents);
    const QString dollars = QLocale(QLocale::English, QLocale::UnitedStates)
                               .toString(absolute / 100);
    return QStringLiteral("%1$%2.%3")
        .arg(negative ? QStringLiteral("-") : QString(), dollars)
        .arg(absolute % 100, 2, 10, QLatin1Char('0'));
}

QString htmlText(const QString &value)
{
    return value.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

QString htmlBreak(const QString &value)
{
    return value.isEmpty() ? QString() : QStringLiteral("<br>") + htmlText(value);
}

QString deviceTypeFor(const QString &device)
{
    const QString normalized = device.trimmed().toLower();
    if (normalized.contains(QStringLiteral("phone")) || normalized.contains(QStringLiteral("iphone")))
        return QStringLiteral("Phone");
    if (normalized.contains(QStringLiteral("tablet")) || normalized.contains(QStringLiteral("ipad")))
        return QStringLiteral("Tablet");
    if (normalized == QStringLiteral("pc") || normalized.contains(QStringLiteral("desktop")))
        return QStringLiteral("PC");
    if (normalized.contains(QStringLiteral("mac")) || normalized.contains(QStringLiteral("laptop")))
        return QStringLiteral("MacBook / laptop");
    if (normalized.contains(QStringLiteral("console")) || normalized.contains(QStringLiteral("ps5"))
        || normalized.contains(QStringLiteral("xbox")) || normalized.contains(QStringLiteral("switch")))
        return QStringLiteral("Gaming console");
    return QStringLiteral("Other");
}

class InvoiceEditor final : public QDialog
{
public:
    InvoiceEditor(const QString &inquiryCode, const QJsonObject &fields, QWidget *parent)
        : QDialog(parent), code(inquiryCode)
    {
        setObjectName(QStringLiteral("invoiceEditor"));
        setWindowTitle(tr("Invoice Form · %1").arg(code));
        setWindowModality(Qt::WindowModal);
        setMinimumSize(760, 680);
        resize(900, 820);
        setFont(QFont(QStringLiteral("Arial"), 10));
        setStyleSheet(QStringLiteral(
            "QFrame#invoiceSurface { background:%1; color:#E8EFE9; border:none; }"
            "QScrollArea#invoiceScroll, QWidget#invoiceContents, "
            "QScrollArea#invoiceScroll > QWidget#qt_scrollarea_viewport "
            "{ background:transparent; border:none; }"
            "QGroupBox { margin-top:14px; padding:14px; color:#E8EFE9; background:#1F3D2B; "
            "border:1px solid #3F6248; border-radius:14px; font-weight:700; }"
            "QGroupBox::title { subcontrol-origin:margin; left:14px; padding:0 5px; color:#F4EFE6; }"
            "QLabel { color:#E8EFE9; }"
            "QLineEdit, QPlainTextEdit, QComboBox, QDoubleSpinBox, QDateEdit { "
            "background:#101C15; color:#E8EFE9; placeholder-text-color:#A6B9AB; "
            "border:1px solid #49624F; border-radius:8px; padding:5px 7px; "
            "selection-background-color:#CC7E3E; selection-color:#142A1E; }"
            "QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QDoubleSpinBox:focus, QDateEdit:focus "
            "{ border-color:#D9782B; }"
            "QDoubleSpinBox QLineEdit, QDateEdit QLineEdit { background:transparent; border:none; padding:0; }"
            "QLineEdit[readOnly=true] { background:#1A3324; color:#C6D7C9; }"
            "QComboBox QAbstractItemView, QCalendarWidget QAbstractItemView { background:#101C15; color:#E8EFE9; "
            "selection-background-color:#496F52; selection-color:#F2F7F2; }"
            "QCalendarWidget QWidget#qt_calendar_navigationbar { background:#1F3D2B; }"
            "QCalendarWidget QToolButton { color:#E8EFE9; }"
            "QTableWidget { background:#101C15; color:#E8EFE9; border:1px solid #49624F; "
            "gridline-color:#3F6248; selection-background-color:#496F52; selection-color:#F2F7F2; }"
            "QHeaderView::section { background:#274A34; color:#F4EFE6; border:0; padding:8px; font-weight:700; }"
            "QPushButton { background:#D9782B; color:#F3EFE4; border:1px solid #C96824; "
            "border-radius:20px; padding:0 18px; min-height:38px; max-height:38px; font-weight:700; }"
            "QPushButton:hover { background:#E4863D; color:#FFF9EF; border-color:#E99A5B; }"
            "QPushButton:pressed { background:#B85D20; color:#FFF9EF; border-color:#A94F18; }"
            "QScrollBar:vertical { width:10px; margin:2px; background:#101C15; border-radius:4px; }"
            "QScrollBar::handle:vertical { min-height:24px; background:#6D8B73; border-radius:4px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
            "QFrame#invoiceTotals { background:#1A3324; border:1px solid #3F6248; border-radius:11px; }"
            "QLabel[totalCaption=true] { color:#C6D7C9; }"
            "QLabel[totalValue=true] { color:#F4EFE6; font-weight:700; }"
            "QLabel#grandTotal { color:#FFC58C; }").arg(BrandStyle::windowBackground));
        auto *dialogLayout = new QVBoxLayout(this);
        dialogLayout->setContentsMargins(0, 0, 0, 0);
        auto *surface = new QFrame(this);
        surface->setObjectName(QStringLiteral("invoiceSurface"));
        dialogLayout->addWidget(surface);
        auto *outer = new QVBoxLayout(surface);
        auto *brandBar = new QFrame(surface);
        auto *brandLayout = new QHBoxLayout(brandBar);
        brandLayout->setContentsMargins(18, 12, 18, 12);
        auto *logo = new QLabel(brandBar);
        const QPixmap logoPixmap(QStringLiteral(":/branding/invoice-logo.png"));
        if (!logoPixmap.isNull())
            logo->setPixmap(logoPixmap.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        brandLayout->addWidget(logo);
        auto *brandText = new QVBoxLayout;
        auto *businessName = new QLabel(tr("Idyllwild Electronic Repair & More"), brandBar);
        businessName->setStyleSheet(QStringLiteral("color:#F3EFE4;font-size:12pt;font-weight:700;"));
        auto *brandSubtitle = new QLabel(tr("Invoice desk"), brandBar);
        brandSubtitle->setFont(QFont(BrandStyle::titleFontFamily(), 18, QFont::Bold));
        brandSubtitle->setStyleSheet(QStringLiteral("color:#F3EFE4;"));
        brandText->addWidget(businessName);
        brandText->addWidget(brandSubtitle);
        brandLayout->addLayout(brandText);
        brandLayout->addStretch(1);
        auto *invoiceTag = new QLabel(tr("INVOICE"), brandBar);
        invoiceTag->setStyleSheet(QStringLiteral("color:#F3EFE4;font-size:14pt;font-weight:700;letter-spacing:3px;"));
        brandLayout->addWidget(invoiceTag);
        outer->addWidget(brandBar);

        auto *scroll = new QScrollArea(surface);
        scroll->setObjectName(QStringLiteral("invoiceScroll"));
        scroll->setWidgetResizable(true);
        auto *contents = new QWidget(scroll);
        contents->setObjectName(QStringLiteral("invoiceContents"));
        auto *contentLayout = new QVBoxLayout(contents);

        auto *intro = new QLabel(tr("New invoice · inquiry %1").arg(code), contents);
        intro->setWordWrap(true);
        intro->setStyleSheet(QStringLiteral("color:#F3EFE4;font-size:15pt;font-weight:700;"));
        contentLayout->addWidget(intro);

        auto *customerBox = new QGroupBox(tr("Customer and repair"), contents);
        auto *customerForm = new QFormLayout(customerBox);
        customerForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto *inquiryCodeEdit = new QLineEdit(code, customerBox);
        inquiryCodeEdit->setObjectName(QStringLiteral("inquiryCodeEdit"));
        inquiryCodeEdit->setReadOnly(true);
        nameEdit = new QLineEdit(read(fields, "name"), customerBox);
        nameEdit->setObjectName(QStringLiteral("customerNameEdit"));
        phoneEdit = new QLineEdit(read(fields, "phone"), customerBox);
        phoneEdit->setObjectName(QStringLiteral("customerPhoneEdit"));
        emailEdit = new QLineEdit(read(fields, "email"), customerBox);
        emailEdit->setObjectName(QStringLiteral("customerEmailEdit"));
        deviceType = new QComboBox(customerBox);
        deviceType->setObjectName(QStringLiteral("deviceTypeEdit"));
        deviceType->addItems({tr("Phone"), tr("Tablet"), tr("PC"), tr("MacBook / laptop"),
                              tr("Gaming console"), tr("Other")});
        deviceType->setCurrentText(deviceTypeFor(read(fields, "device")));
        modelEdit = new QLineEdit(read(fields, "model"), customerBox);
        modelEdit->setObjectName(QStringLiteral("deviceModelEdit"));
        problemEdit = new QPlainTextEdit(read(fields, "problem"), customerBox);
        problemEdit->setObjectName(QStringLiteral("problemEdit"));
        problemEdit->setMaximumHeight(76);
        customerForm->addRow(tr("Inquiry code"), inquiryCodeEdit);
        customerForm->addRow(tr("Customer name"), nameEdit);
        customerForm->addRow(tr("Phone"), phoneEdit);
        customerForm->addRow(tr("Email"), emailEdit);
        customerForm->addRow(tr("Type"), deviceType);
        customerForm->addRow(tr("Make and model"), modelEdit);
        customerForm->addRow(tr("Problem reported"), problemEdit);
        contentLayout->addWidget(customerBox);

        auto *itemsBox = new QGroupBox(tr("Invoice items"), contents);
        auto *itemsLayout = new QVBoxLayout(itemsBox);
        items = new QTableWidget(0, 5, itemsBox);
        items->setObjectName(QStringLiteral("invoiceItemsTable"));
        items->setHorizontalHeaderLabels({tr("Description"), tr("Type"), tr("Qty"),
                                          tr("Unit price"), tr("Amount")});
        items->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        items->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        items->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        items->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        items->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        items->verticalHeader()->setDefaultSectionSize(36);
        items->verticalHeader()->hide();
        items->setSelectionBehavior(QAbstractItemView::SelectRows);
        items->setMinimumHeight(166);
        itemsLayout->addWidget(items);
        auto *itemActions = new QHBoxLayout;
        auto *addLineButton = new QPushButton(tr("+ Add line"), itemsBox);
        auto *removeLineButton = new QPushButton(tr("Remove selected line"), itemsBox);
        itemActions->addWidget(addLineButton);
        itemActions->addWidget(removeLineButton);
        itemActions->addStretch(1);
        itemsLayout->addLayout(itemActions);
        contentLayout->addWidget(itemsBox);

        auto *optionsBox = new QGroupBox(tr("Tax and invoice date"), contents);
        auto *optionsForm = new QFormLayout(optionsBox);
        taxRate = new QDoubleSpinBox(optionsBox);
        taxRate->setRange(0, 20);
        taxRate->setDecimals(2);
        taxRate->setSuffix(QStringLiteral("%"));
        taxRate->setValue(7.75);
        taxRate->setToolTip(tr("Default is the current Idyllwild area rate. Apply the rate for the customer’s taxable sale location."));
        invoiceDate = new QDateEdit(QDate::currentDate(), optionsBox);
        invoiceDate->setCalendarPopup(true);
        invoiceDate->setDisplayFormat(QStringLiteral("MMM d, yyyy"));

        optionsForm->addRow(tr("Sales tax rate on parts (Idyllwild)"), taxRate);
        optionsForm->addRow(tr("Invoice date"), invoiceDate);
        contentLayout->addWidget(optionsBox);

        auto *notesBox = new QGroupBox(tr("Notes"), contents);
        auto *notesLayout = new QVBoxLayout(notesBox);
        notesEdit = new QPlainTextEdit(notesBox);
        notesEdit->setPlaceholderText(tr("Warranty, pickup details, or other notes"));
        notesEdit->setMaximumHeight(84);
        notesLayout->addWidget(notesEdit);
        contentLayout->addWidget(notesBox);
        contentLayout->addStretch(1);
        scroll->setWidget(contents);
        outer->addWidget(scroll, 1);

        outer->addWidget(createTotalsPanel(surface));

        auto *buttons = new QDialogButtonBox(surface);
        auto *closeButton = buttons->addButton(QDialogButtonBox::Close);
        closeButton->setObjectName(QStringLiteral("closeInvoiceButton"));
        auto *submitButton = buttons->addButton(tr("Submit"), QDialogButtonBox::AcceptRole);
        submitButton->setObjectName(QStringLiteral("submitInvoiceButton"));
        outer->addWidget(buttons);

        connect(taxRate, &QDoubleSpinBox::valueChanged, this, [this] { updateTotals(); });
        connect(addLineButton, &QPushButton::clicked, this, [this] { addLine(); });
        connect(removeLineButton, &QPushButton::clicked, this, [this] {
            const int row = items->currentRow();
            if (row >= 0)
                items->removeRow(row);
            updateTotals();
        });
        connect(items, &QTableWidget::itemChanged, this, [this](const QTableWidgetItem *item) {
            if (item->column() == 0)
                updateTotals();
        });
        connect(submitButton, &QPushButton::clicked, this, [this] { createPdf(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        addLine();
        addLine();
        addLine();
        updateTotals();
        nameEdit->setFocus();
    }

    QString invoiceNumber() const { return number; }
    QByteArray pdfBytes() const { return pdf; }
    qint64 amountDue() const { return amountDueCents; }

private:
    static QString read(const QJsonObject &fields, const QString &key)
    {
        return fields.value(key).toObject().value(QStringLiteral("stringValue")).toString();
    }

    QFrame *createTotalsPanel(QWidget *parent)
    {
        auto *panel = new QFrame(parent);
        panel->setObjectName(QStringLiteral("invoiceTotals"));
        auto *grid = new QGridLayout(panel);
        grid->setContentsMargins(16, 10, 16, 10);
        grid->setHorizontalSpacing(18);
        grid->setVerticalSpacing(3);

        const std::array<QString, 6> captions{
            tr("Parts"), tr("Labor"), tr("Discounts"),
            tr("Subtotal"), tr("Sales tax"), tr("Grand total")
        };
        const std::array<QString, 6> valueNames{
            QStringLiteral("partsTotal"), QStringLiteral("laborTotal"), QStringLiteral("discountTotal"),
            QStringLiteral("subtotal"), QStringLiteral("taxTotal"), QStringLiteral("grandTotal")
        };
        for (int index = 0; index < int(captions.size()); ++index) {
            const int row = index / 3 * 2;
            const int column = index % 3;
            auto *caption = new QLabel(captions[index], panel);
            caption->setProperty("totalCaption", true);
            grid->addWidget(caption, row, column);

            auto *value = new QLabel(panel);
            value->setObjectName(valueNames[index]);
            value->setTextFormat(Qt::PlainText);
            value->setProperty("totalValue", true);
            value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            grid->addWidget(value, row + 1, column);
            totalValues[index] = value;
            grid->setColumnStretch(column, 1);
        }
        return panel;
    }

    void addLine()
    {
        const int row = items->rowCount();
        items->insertRow(row);
        auto *description = new QTableWidgetItem;
        items->setItem(row, 0, description);
        auto *type = new QComboBox(items);
        type->addItems({tr("Labor"), tr("Part"), tr("Coupon / discount")});
        items->setCellWidget(row, 1, type);
        auto *quantity = new QDoubleSpinBox(items);
        quantity->setRange(0, 100000);
        quantity->setDecimals(2);
        quantity->setValue(1);
        quantity->setMinimumWidth(72);
        items->setCellWidget(row, 2, quantity);
        auto *price = new QDoubleSpinBox(items);
        price->setRange(0, 10000000);
        price->setDecimals(2);
        price->setPrefix(QStringLiteral("$"));
        price->setMinimumWidth(100);
        items->setCellWidget(row, 3, price);
        auto *amount = new QTableWidgetItem(QStringLiteral("$0.00"));
        amount->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        amount->setFlags(amount->flags() & ~Qt::ItemIsEditable);
        items->setItem(row, 4, amount);
        connect(type, &QComboBox::currentTextChanged, this, [this] { updateTotals(); });
        connect(quantity, &QDoubleSpinBox::valueChanged, this, [this] { updateTotals(); });
        connect(price, &QDoubleSpinBox::valueChanged, this, [this] { updateTotals(); });
    }

    struct Line {
        QString description;
        bool part = false;
        bool discount = false;
        double quantity = 0;
        qint64 unitCents = 0;
        qint64 amountCents = 0;
    };

    QList<Line> readLines()
    {
        QList<Line> result;
        QSignalBlocker blocker(items);
        for (int row = 0; row < items->rowCount(); ++row) {
            auto *quantity = qobject_cast<QDoubleSpinBox *>(items->cellWidget(row, 2));
            auto *price = qobject_cast<QDoubleSpinBox *>(items->cellWidget(row, 3));
            auto *type = qobject_cast<QComboBox *>(items->cellWidget(row, 1));
            auto *descriptionItem = items->item(row, 0);
            if (!quantity || !price || !type || !descriptionItem)
                continue;
            const QString description = descriptionItem->text().trimmed();
            if (description.isEmpty() && qFuzzyIsNull(price->value())) {
                items->item(row, 4)->setText(moneyText(0));
                continue;
            }
            Line line;
            line.description = description.isEmpty() ? tr("(no description)") : description;
            line.part = type->currentIndex() == 1;
            line.discount = type->currentIndex() == 2;
            line.quantity = quantity->value();
            line.unitCents = qRound64(price->value() * 100.0);
            line.amountCents = qRound64(line.quantity * line.unitCents)
                * (line.discount ? -1 : 1);
            items->item(row, 4)->setText(moneyText(line.amountCents));
            result.append(line);
        }
        return result;
    }

    struct Totals {
        qint64 parts = 0;
        qint64 labor = 0;
        qint64 discounts = 0;
        qint64 subtotal = 0;
        qint64 tax = 0;
        qint64 grandTotal = 0;
    };

    Totals calculateTotals(const QList<Line> &lines) const
    {
        Totals totals;
        for (const auto &line : lines) {
            if (line.discount)
                totals.discounts -= line.amountCents;
            else if (line.part)
                totals.parts += line.amountCents;
            else
                totals.labor += line.amountCents;
        }
        totals.subtotal = totals.parts + totals.labor - totals.discounts;
        const qint64 taxableParts = qMax<qint64>(0, totals.parts - totals.discounts);
        totals.tax = qRound64(taxableParts * taxRate->value() / 100.0);
        totals.grandTotal = totals.subtotal + totals.tax;
        return totals;
    }

    void updateTotals()
    {
        const auto totals = calculateTotals(readLines());
        const std::array<qint64, 6> values{
            totals.parts, totals.labor, -totals.discounts,
            totals.subtotal, totals.tax, totals.grandTotal
        };
        for (std::size_t index = 0; index < values.size(); ++index)
            totalValues[index]->setText(moneyText(values[index]));
    }

    QString renderHtml(const QList<Line> &lines, const Totals &totals, const QString &invoiceId) const
    {
        QString html = QStringLiteral(
            "<html><body style='font-family:Arial,Helvetica,sans-serif;color:#203229;font-size:10pt;'>"
            "<table width='100%' cellpadding='10' cellspacing='0' bgcolor='#1F3D2B'>"
            "<tr><td width='45'><img src='invoice-logo' width='42' height='42'></td>"
            "<td style='color:#FFFFFF;font-size:13pt'><b>Idyllwild Electronic Repair &amp; More</b></td>"
            "<td align='right' style='color:#FFFFFF'><b>INVOICE</b><br>%1</td></tr></table>"
            "<table width='100%' cellpadding='8' cellspacing='0' bgcolor='#EEF4EF'>"
            "<tr><td width='50%'><b>Invoice date</b> &nbsp; %2</td>"
            "<td width='50%'><b>Inquiry code</b> &nbsp; %3</td></tr></table>"
            "<table width='100%' cellpadding='8' cellspacing='0'><tr>"
            "<td width='50%' valign='top'><b style='color:#28583A'>BILL TO</b><br>%4%5%6</td>"
            "<td width='50%' valign='top'><b style='color:#28583A'>DEVICE</b><br>%7%8</td></tr></table>"
            "<table width='100%' cellpadding='4' cellspacing='0'>"
            "<tr bgcolor='#1F3D2B' style='color:#FFFFFF'><th align='left'>Description</th>"
            "<th align='left'>Type</th><th align='right'>Qty</th>"
            "<th align='right' style='white-space:nowrap'>Unit price</th><th align='right'>Amount</th></tr>")
            .arg(htmlText(invoiceId), htmlText(invoiceDate->date().toString(QStringLiteral("MMMM d, yyyy"))),
                 htmlText(code), htmlText(nameEdit->text().trimmed()), htmlBreak(phoneEdit->text().trimmed()),
                 htmlBreak(emailEdit->text().trimmed()),
                 htmlText(deviceType->currentText()) + htmlBreak(modelEdit->text().trimmed()),
                 htmlBreak(problemEdit->toPlainText().trimmed()));

        int row = 0;
        for (const auto &line : lines) {
            QString quantity = QString::number(line.quantity, 'f', 2);
            while (quantity.endsWith(QLatin1Char('0')))
                quantity.chop(1);
            if (quantity.endsWith(QLatin1Char('.')))
                quantity.chop(1);
            html += QStringLiteral("<tr bgcolor='%1'><td>%2</td><td>%3</td>"
                                   "<td align='right' style='white-space:nowrap'>%4</td>"
                                   "<td align='right' style='white-space:nowrap'>%5</td>"
                                   "<td align='right' style='white-space:nowrap'>%6</td></tr>")
                        .arg(row++ % 2 == 0 ? QStringLiteral("#F0F5F1") : QStringLiteral("#FFFFFF"),
                             htmlText(line.description),
                             line.discount ? tr("Discount") : line.part ? tr("Part") : tr("Labor"),
                             quantity, moneyText(line.discount ? -line.unitCents : line.unitCents),
                             moneyText(line.amountCents));
        }

        const QString notes = notesEdit->toPlainText().trimmed();
        const QString notesSection = notes.isEmpty()
            ? QString()
            : QStringLiteral("<p><b>Notes</b><br>%1</p>").arg(htmlText(notes));
        html += QStringLiteral(
            "</table><br><table width='100%' cellpadding='0' cellspacing='0'><tr>"
            "<td width='48%' valign='top'>%1</td>"
            "<td width='52%' valign='top'><table width='100%' cellpadding='4' cellspacing='0' bgcolor='#EEF4EF'>"
            "<tr><td>Parts</td><td align='right'>%2</td></tr>"
            "<tr><td>Labor</td><td align='right'>%3</td></tr>"
            "<tr><td>Discounts</td><td align='right'>%4</td></tr>"
            "<tr><td>Subtotal</td><td align='right'>%5</td></tr>"
            "<tr><td>Sales tax (%6%)</td><td align='right'>%7</td></tr>"
            "<tr bgcolor='#1F3D2B' style='color:#FFFFFF;font-size:12pt'>"
            "<td><b>Grand total</b></td><td align='right'><b>%8</b></td></tr>"
            "</table></td></tr></table></body></html>")
                    .arg(notesSection, moneyText(totals.parts), moneyText(totals.labor),
                         moneyText(-totals.discounts), moneyText(totals.subtotal),
                         QString::number(taxRate->value(), 'f', 2), moneyText(totals.tax),
                         moneyText(totals.grandTotal));
        return html;
    }

    void createPdf()
    {
        if (nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Customer name required"),
                                 tr("Enter a customer name before creating the invoice."));
            return;
        }
        const auto lines = readLines();
        if (lines.isEmpty()) {
            QMessageBox::warning(this, tr("Invoice item required"),
                                 tr("Add at least one invoice item before creating the PDF."));
            return;
        }
        const auto totals = calculateTotals(lines);
        if (totals.subtotal < 0) {
            QMessageBox::warning(this, tr("Discount exceeds invoice subtotal"),
                                 tr("Coupons and discounts cannot be greater than the invoice items."));
            return;
        }

        const int sequence = QSettings().value(QStringLiteral("invoice/lastNumber"), 0).toInt() + 1;
        const QString invoiceId = QStringLiteral("INV-%1").arg(sequence, 4, 10, QLatin1Char('0'));
        QByteArray output;
        QBuffer buffer(&output);
        if (!buffer.open(QIODevice::WriteOnly))
            return;

        {
            QPdfWriter writer(&buffer);
            writer.setResolution(72);
            writer.setTitle(tr("Invoice %1").arg(invoiceId));
            writer.setCreator(QStringLiteral("Idyllwild Electronic Repair & More"));
            writer.setPageSize(QPageSize(QPageSize::Letter));
            writer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
            const QRect pageRect = writer.pageLayout().paintRectPixels(writer.resolution());
            const QSize pageSize = pageRect.size();

            const QImage signature(QStringLiteral(":/branding/invoice-signature.png"));
            // Draw the contact portion of the supplied image, starting after its logo.
            const QRect contactArea = signature.rect().adjusted(292, 0, 0, 0);
            const QSizeF signatureSize = QSizeF(contactArea.size()).scaled(
                QSizeF(290, 124), Qt::KeepAspectRatio);
            const qreal footerTop = pageSize.height() - signatureSize.height() - 10;

            QTextDocument document;
            document.documentLayout()->setPaintDevice(&writer);
            document.setDocumentMargin(0);
            document.setDefaultFont(QFont(QStringLiteral("Arial"), 10));
            document.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("invoice-logo")),
                                 QImage(QStringLiteral(":/branding/invoice-logo.png")));
            document.setHtml(renderHtml(lines, totals, invoiceId));
            document.setTextWidth(pageSize.width());

            // Reserve space for the shop information on the same Letter page.
            constexpr qreal minimumScale = 0.75;
            const qreal bodyHeight = footerTop - 14;
            const qreal suggestedScale = qBound(minimumScale, bodyHeight / document.size().height(), 1.0);
            if (suggestedScale < 1.0)
                document.setTextWidth(pageSize.width() / suggestedScale);
            const QSizeF contentSize = document.size();
            const qreal scale = qMin(1.0, qMin(pageSize.width() / contentSize.width(),
                                             bodyHeight / contentSize.height()));
            if (scale + 0.001 < minimumScale) {
                QMessageBox::warning(this, tr("Invoice exceeds one page"),
                                     tr("Shorten the descriptions or notes, or use fewer line items, so the invoice fits legibly on one page."));
                return;
            }

            QPainter painter(&writer);
            if (painter.isActive()) {
                painter.save();
                painter.scale(scale, scale);
                document.drawContents(&painter);
                painter.restore();
                painter.setPen(QColor(QStringLiteral("#B6C7BB")));
                painter.drawLine(QLineF(0, footerTop, pageSize.width(), footerTop));
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.drawImage(QRectF(QPointF(0, footerTop + 10), signatureSize),
                                  signature, contactArea);
                painter.setPen(QColor(QStringLiteral("#304D3C")));
                painter.setFont(QFont(QStringLiteral("Arial"), 9));
                const QRectF address(signatureSize.width() + 10, footerTop + 10,
                                     pageSize.width() - signatureSize.width() - 10, signatureSize.height());
                painter.drawText(address, Qt::AlignRight | Qt::AlignVCenter,
                                 tr("Located at Idyllwild Head Shop\n54200 N Circle Drive, Unit D1\nIdyllwild, CA 92549"));
            }
        }
        if (!output.startsWith("%PDF-")) {
            QMessageBox::warning(this, tr("Could not create PDF"),
                                 tr("The invoice PDF could not be generated."));
            return;
        }
        number = invoiceId;
        amountDueCents = totals.grandTotal;
        QSettings().setValue(QStringLiteral("invoice/lastNumber"), sequence);
        pdf = output;
        accept();
    }

    QString code;
    QString number;
    QByteArray pdf;
    qint64 amountDueCents = 0;
    QLineEdit *nameEdit = nullptr;
    QLineEdit *phoneEdit = nullptr;
    QLineEdit *emailEdit = nullptr;
    QComboBox *deviceType = nullptr;
    QLineEdit *modelEdit = nullptr;
    QPlainTextEdit *problemEdit = nullptr;
    QTableWidget *items = nullptr;
    QDoubleSpinBox *taxRate = nullptr;
    QDateEdit *invoiceDate = nullptr;
    QPlainTextEdit *notesEdit = nullptr;
    std::array<QLabel *, 6> totalValues{};
};
} // namespace

QUrl customerInvoiceUrl(const QString &inquiryCode, const QString &invoiceNumber,
                        const QString &paymentMethod)
{
    QUrl url(QStringLiteral("https://idyllwildelectronics.com/invoice%1/")
                 .arg(QString::fromLatin1(QUrl::toPercentEncoding(invoiceNumber))));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("code"), inquiryCode);
    if (!paymentMethod.isEmpty())
        query.addQueryItem(QStringLiteral("pay"), paymentMethod);
    url.setQuery(query);
    return url;
}

bool createInvoice(QWidget *parent, const QString &inquiryCode,
                   const QJsonObject &privateFields, GeneratedInvoice *result)
{
    InvoiceEditor dialog(inquiryCode, privateFields, parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    result->number = dialog.invoiceNumber();
    result->pdf = dialog.pdfBytes();
    result->amountDueCents = dialog.amountDue();
    return !result->number.isEmpty() && result->pdf.startsWith("%PDF-");
}
