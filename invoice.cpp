#include "invoice.h"

#include <QBuffer>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QPageLayout>
#include <QPageSize>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QPixmap>
#include <QScrollArea>
#include <QUrl>
#include <QFont>
#include <QAbstractItemView>
#include <QtMath>

#include <cstdlib>

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
        setModal(true);
        setMinimumSize(760, 680);
        resize(900, 820);
        setFont(QFont(QStringLiteral("Arial"), 10));
        setStyleSheet(QStringLiteral(
            "QDialog#invoiceEditor { background:#1E3D2B; color:#E8EFE9; }"
            "QScrollArea, QScrollArea QWidget#qt_scrollarea_viewport { background:transparent; border:0; }"
            "QGroupBox { margin-top:14px; padding:14px; color:#1E3D2B; background:#FBF9F3; "
            "border:1px solid #A9C9AE; border-radius:16px; font-weight:700; }"
            "QGroupBox#customerInfoBox { background:#DDEBDD; border:2px solid #A9C9AE; color:#1E3D2B; }"
            "QGroupBox::title { subcontrol-origin:margin; left:14px; padding:0 5px; color:#1E3D2B; }"
            "QGroupBox#customerInfoBox QLabel, QGroupBox#customerInfoBox QLineEdit, "
            "QGroupBox#customerInfoBox QPlainTextEdit, QGroupBox#customerInfoBox QComboBox { color:#1E3D2B; }"
            "QLabel { color:#6B6A5E; font-family:Arial; }"
            "QLineEdit, QPlainTextEdit, QComboBox, QDoubleSpinBox, QDateEdit { background:#FBF9F3; "
            "color:#231F20; border:1.5px solid #DDD6C4; border-radius:6px; padding:8px 10px; "
            "font-family:Arial; font-size:10pt; selection-background-color:#D9782B; }"
            "QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QDoubleSpinBox:focus, QDateEdit:focus { "
            "border:2px solid #D9782B; }"
            "QLineEdit[readOnly=true] { background:#F3EFE4; color:#1E3D2B; font-weight:700; }"
            "QTableWidget { background:#FBF9F3; color:#231F20; border:1px solid #DDD6C4; "
            "gridline-color:#DDD6C4; font-family:Arial; }"
            "QHeaderView::section { background:#1E3D2B; color:#F3EFE4; border:0; padding:8px; "
            "font-family:Arial; font-weight:700; }"
            "QPushButton { background:#2F6B43; color:#F3EFE4; border:1px solid #245735; "
            "border-radius:22px; padding:8px 18px; min-height:30px; font-family:Arial; font-weight:700; }"
            "QPushButton:hover { background:#3D8053; color:#FFF9EF; }"
            "QPushButton:pressed { background:#1E4E31; color:#FFF9EF; }"));
        auto *outer = new QVBoxLayout(this);
        auto *brandBar = new QFrame(this);
        brandBar->setStyleSheet(QStringLiteral("background:#1E3D2B; border:0;"));
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
        brandSubtitle->setStyleSheet(QStringLiteral("color:#F3EFE4;"));
        brandText->addWidget(businessName);
        brandText->addWidget(brandSubtitle);
        brandLayout->addLayout(brandText);
        brandLayout->addStretch(1);
        auto *invoiceTag = new QLabel(tr("INVOICE"), brandBar);
        invoiceTag->setStyleSheet(QStringLiteral("color:#F3EFE4;font-size:14pt;font-weight:700;letter-spacing:3px;"));
        brandLayout->addWidget(invoiceTag);
        outer->addWidget(brandBar);

        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        auto *contents = new QWidget(scroll);
        auto *contentLayout = new QVBoxLayout(contents);

        auto *intro = new QLabel(tr("New invoice · inquiry %1").arg(code), contents);
        intro->setWordWrap(true);
        intro->setStyleSheet(QStringLiteral("color:#F3EFE4;font-size:15pt;font-weight:700;"));
        contentLayout->addWidget(intro);

        auto *customerBox = new QGroupBox(tr("Customer and repair"), contents);
        customerBox->setObjectName(QStringLiteral("customerInfoBox"));
        auto *customerForm = new QFormLayout(customerBox);
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

        auto *optionsBox = new QGroupBox(tr("Discount, tax, and payment"), contents);
        auto *optionsForm = new QFormLayout(optionsBox);
        taxRate = new QDoubleSpinBox(optionsBox);
        taxRate->setRange(0, 20);
        taxRate->setDecimals(2);
        taxRate->setSuffix(QStringLiteral("%"));
        taxRate->setValue(7.75);
        taxRate->setToolTip(tr("Default is the current Idyllwild area rate. Apply the rate for the customer’s taxable sale location."));
        paymentStatus = new QComboBox(optionsBox);
        paymentStatus->addItems({tr("Paid"), tr("Payment due")});
        paymentMethod = new QComboBox(optionsBox);
        paymentMethod->addItems({tr("Cash"), tr("Card"), tr("Mobile payment"), tr("Other")});
        invoiceDate = new QDateEdit(QDate::currentDate(), optionsBox);
        invoiceDate->setCalendarPopup(true);
        invoiceDate->setDisplayFormat(QStringLiteral("MMMM d, yyyy"));
        optionsForm->addRow(tr("Sales tax rate on parts (Idyllwild)"), taxRate);
        optionsForm->addRow(tr("Payment status"), paymentStatus);
        optionsForm->addRow(tr("Payment method"), paymentMethod);
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

        totalsLabel = new QLabel(this);
        totalsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        totalsLabel->setStyleSheet(QStringLiteral("color:#F3EFE4;font-weight:700;padding:4px 8px;"));
        outer->addWidget(totalsLabel);

        auto *buttons = new QDialogButtonBox(this);
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
        connect(items, &QTableWidget::itemChanged, this, [this] { updateTotals(); });
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

private:
    static QString read(const QJsonObject &fields, const QString &key)
    {
        return fields.value(key).toObject().value(QStringLiteral("stringValue")).toString();
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
            if (description.isEmpty() && qFuzzyIsNull(price->value()))
                continue;
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

    void updateTotals()
    {
        const auto lines = readLines();
        qint64 parts = 0;
        qint64 labor = 0;
        qint64 discounts = 0;
        for (const auto &line : lines) {
            if (line.discount)
                discounts -= line.amountCents;
            else
                (line.part ? parts : labor) += line.amountCents;
        }
        const qint64 subtotal = parts + labor - discounts;
        const qint64 taxableParts = qMax<qint64>(0, parts - discounts);
        const qint64 tax = qRound64(taxableParts * taxRate->value() / 100.0);
        totalsLabel->setText(tr("Parts: %1    Labor: %2    Discounts: -%3    Subtotal: %4\nSales tax: %5    Grand Total: %6")
                                 .arg(moneyText(parts), moneyText(labor), moneyText(discounts),
                                      moneyText(subtotal), moneyText(tax), moneyText(subtotal + tax)));
    }

    QString renderHtml(const QList<Line> &lines, const QString &invoiceId) const
    {
        qint64 parts = 0;
        qint64 labor = 0;
        qint64 discounts = 0;
        for (const auto &line : lines) {
            if (line.discount)
                discounts -= line.amountCents;
            else
                (line.part ? parts : labor) += line.amountCents;
        }
        const qint64 subtotal = parts + labor - discounts;
        const qint64 taxableParts = qMax<qint64>(0, parts - discounts);
        const qint64 tax = qRound64(taxableParts * taxRate->value() / 100.0);
        const qint64 total = subtotal + tax;

        const QString phone = phoneEdit->text().trimmed();
        const QString email = emailEdit->text().trimmed();
        const QString model = modelEdit->text().trimmed();
        const QString device = deviceType->currentText()
            + (model.isEmpty() ? QString() : QStringLiteral(": ") + model);

        QString html = QStringLiteral(
            "<html><body style='font-family:Arial,Helvetica,sans-serif;color:#231F20;background:#F3EFE4;font-size:10pt;'>"
            "<table width='100%' cellpadding='12' cellspacing='0' style='background:#1E3D2B;color:#F3EFE4;'>"
            "<tr><td width='68'><img src='invoice-logo' width='56' height='56'></td>"
            "<td><b style='font-size:12pt'>Idyllwild Electronic Repair &amp; More</b><br>idyllwildelectronics.com</td>"
            "<td align='right'><b style='font-size:15pt;letter-spacing:3px'>INVOICE</b></td></tr></table>"
            "<table width='100%' cellpadding='20' cellspacing='0' style='background:#FBF9F3;border:1px solid #DDD6C4;'>"
            "<tr><td><table width='100%' cellpadding='7' cellspacing='0'><tr>"
            "<td width='34%'><font color='#6B6A5E' size='2'><b>INVOICE</b></font><br><b style='font-size:15pt;color:#1E3D2B;white-space:nowrap'>%1</b></td>"
            "<td width='33%'><font color='#6B6A5E' size='2'><b>INQUIRY CODE</b></font><br><b>%2</b></td>"
            "<td width='33%'><font color='#6B6A5E' size='2'><b>DATE</b></font><br>%3</td>"
            "</tr></table><table width='100%' cellpadding='7' cellspacing='0'><tr>"
            "<td width='50%'><font color='#6B6A5E' size='2'><b>BILLED TO</b></font><br><b>%4</b><br>%5<br>%6</td>"
            "<td width='50%'><font color='#6B6A5E' size='2'><b>DEVICE</b></font><br>%7</td>"
            "</tr><tr><td colspan='2'><font color='#6B6A5E' size='2'><b>PROBLEM REPORTED</b></font><br>%8</td>"
            "</tr></table><br>"
            "<table width='100%' cellspacing='0' cellpadding='9' style='border-collapse:collapse;'>"
            "<tr bgcolor='#1E3D2B'><th></th><th align='left'><font color='#F3EFE4'>Description</font></th>"
            "<th><font color='#F3EFE4'>Type</font></th><th align='right'><font color='#F3EFE4'>Qty</font></th>"
            "<th align='right'><font color='#F3EFE4'>Unit price</font></th><th align='right'><font color='#F3EFE4'>Amount</font></th></tr>")
            .arg(htmlText(invoiceId), htmlText(code),
                 htmlText(invoiceDate->date().toString(QStringLiteral("MMMM d, yyyy"))),
                 htmlText(nameEdit->text()), htmlText(phone), htmlText(email),
                 htmlText(device), htmlText(problemEdit->toPlainText()));

        for (const auto &line : lines) {
            QString quantity = QString::number(line.quantity, 'f', 2);
            while (quantity.endsWith(QLatin1Char('0')))
                quantity.chop(1);
            if (quantity.endsWith(QLatin1Char('.')))
                quantity.chop(1);
            html += QStringLiteral("<tr><td style='border-bottom:1px solid #DDD6C4;color:#2F6B43'>■</td>"
                                   "<td style='border-bottom:1px solid #DDD6C4'>%1</td>"
                                   "<td align='center' style='border-bottom:1px solid #DDD6C4'>%2</td>"
                                   "<td align='right' style='border-bottom:1px solid #DDD6C4'>%3</td>"
                                   "<td align='right' style='border-bottom:1px solid #DDD6C4'>%4</td>"
                                   "<td align='right' style='border-bottom:1px solid #DDD6C4'>%5</td></tr>")
                        .arg(htmlText(line.description), line.discount ? tr("Coupon / discount")
                             : (line.part ? tr("Part") : tr("Labor")), quantity,
                             moneyText(line.discount ? -line.unitCents : line.unitCents),
                             moneyText(line.amountCents));
        }
        html += QStringLiteral("</table><br><table width='52%' align='right' cellpadding='7' cellspacing='0'>"
                               "<tr><td>Parts</td><td align='right'>%1</td></tr>"
                               "<tr><td>Labor</td><td align='right'>%2</td></tr>"
                               "<tr><td>Coupons / discounts</td><td align='right'>-%3</td></tr>"
                               "<tr><td>Subtotal</td><td align='right'>%4</td></tr>"
                               "<tr><td>Sales tax (%5% on taxable parts)</td><td align='right'>%6</td></tr>"
                               "<tr bgcolor='#DDEBDD'><td style='border-top:3px solid #2F6B43;color:#1E3D2B;font-size:14pt'><b>Grand Total</b></td>"
                               "<td align='right' style='border-top:3px solid #2F6B43;color:#1E3D2B;font-size:14pt'><b>%7</b></td></tr>"
                               "</table><br clear='all'>")
                    .arg(moneyText(parts), moneyText(labor), moneyText(discounts), moneyText(subtotal),
                         QString::number(taxRate->value(), 'f', 2), moneyText(tax), moneyText(total));

        if (paymentStatus->currentIndex() == 0) {
            html += QStringLiteral("<table width='34%' cellpadding='7' cellspacing='0'><tr bgcolor='#1E3D2B'>"
                                   "<td><font color='#F3EFE4'><b>PAID · %1</b></font></td></tr></table>")
                        .arg(htmlText(paymentMethod->currentText()));
        } else {
            html += QStringLiteral("<table width='34%' cellpadding='7' cellspacing='0'><tr bgcolor='#D9782B'>"
                                   "<td><font color='#F3EFE4'><b>PAYMENT DUE</b></font></td></tr></table>");
        }
        if (!notesEdit->toPlainText().trimmed().isEmpty())
            html += QStringLiteral("<p style='border-left:4px solid #D9782B;padding-left:12px;white-space:pre-wrap;'>%1</p>")
                        .arg(htmlText(notesEdit->toPlainText()));
        html += QStringLiteral("<p align='center' style='color:#1E3D2B'><b>Thank you for choosing Idyllwild Electronic Repair &amp; More!</b></p>"
                               "</td></tr></table><br><table width='100%' cellpadding='12' cellspacing='0' "
                               "style='background:#1E3D2B;color:#F3EFE4;text-align:center;'>"
                               "<tr><td><b style='font-size:15pt'>(760) 218-5838</b><br>Located @ Idyllwild Head Shop<br>"
                               "54200 N Circle Drive, Unit D1, Idyllwild, CA 92549<br>"
                               "<i>Service with a smile. Never an upsell. #RightToRepair</i></td></tr></table>"
                               "</body></html>");
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
        qint64 discounts = 0;
        qint64 itemTotal = 0;
        for (const auto &line : lines) {
            if (line.discount)
                discounts -= line.amountCents;
            else
                itemTotal += line.amountCents;
        }
        if (discounts > itemTotal) {
            QMessageBox::warning(this, tr("Discount exceeds invoice subtotal"),
                                 tr("Coupons and discounts cannot be greater than the invoice items."));
            return;
        }

        const int sequence = QSettings().value(QStringLiteral("invoice/lastNumber"), 0).toInt() + 1;
        number = QStringLiteral("INV-%1").arg(sequence, 4, 10, QLatin1Char('0'));
        QByteArray output;
        QBuffer buffer(&output);
        if (!buffer.open(QIODevice::WriteOnly))
            return;
        QPdfWriter writer(&buffer);
        writer.setTitle(tr("Invoice %1").arg(number));
        writer.setCreator(QStringLiteral("Idyllwild Electronic Repair & More"));
        writer.setPageSize(QPageSize(QPageSize::Letter));
        writer.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);
        QTextDocument document;
        QFont invoiceFont(QStringLiteral("Arial"));
        invoiceFont.setPointSizeF(9.5);
        document.setDefaultFont(invoiceFont);
        const QImage logo(QStringLiteral(":/branding/invoice-logo.png"));
        if (!logo.isNull())
            document.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("invoice-logo")), logo);
        document.setHtml(renderHtml(lines, number));
        document.setPageSize(writer.pageLayout().paintRect(QPageLayout::Point).size());
        document.print(&writer);
        buffer.close();
        if (output.isEmpty()) {
            QMessageBox::warning(this, tr("Could not create PDF"),
                                 tr("The invoice PDF could not be generated."));
            return;
        }
        QSettings().setValue(QStringLiteral("invoice/lastNumber"), sequence);
        pdf = output;
        accept();
    }

    QString code;
    QString number;
    QByteArray pdf;
    QLineEdit *nameEdit = nullptr;
    QLineEdit *phoneEdit = nullptr;
    QLineEdit *emailEdit = nullptr;
    QComboBox *deviceType = nullptr;
    QLineEdit *modelEdit = nullptr;
    QPlainTextEdit *problemEdit = nullptr;
    QTableWidget *items = nullptr;
    QDoubleSpinBox *taxRate = nullptr;
    QComboBox *paymentStatus = nullptr;
    QComboBox *paymentMethod = nullptr;
    QDateEdit *invoiceDate = nullptr;
    QPlainTextEdit *notesEdit = nullptr;
    QLabel *totalsLabel = nullptr;
};
} // namespace

bool createInvoice(QWidget *parent, const QString &inquiryCode,
                   const QJsonObject &privateFields, GeneratedInvoice *result)
{
    InvoiceEditor dialog(inquiryCode, privateFields, parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    result->number = dialog.invoiceNumber();
    result->pdf = dialog.pdfBytes();
    return !result->number.isEmpty() && result->pdf.startsWith("%PDF-");
}
