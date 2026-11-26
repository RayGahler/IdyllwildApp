#include "mainidyll.h"
#include "IdyllwildSync.h"
#include "firestore.h"
#include "preferredcontact.h"

#include <ui_mainidyll.h>

#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QSpinBox>

void MainIdyll::openRepairExport(bool schedule)
{
    if (idToken.isEmpty())
        return;

    const auto *tab = ui->inquiryTabs->currentWidget();
    QString code;
    QString collection = QStringLiteral("privateInquiries");
    if (tab == ui->pendingTab) {
        code = pendingCode;
        collection = QStringLiteral("pendingInquiries");
    } else if (tab == ui->privateTab) {
        code = privateInquiry.code;
    } else if (tab == ui->publicTab) {
        code = publicInquiry.code;
    }

    if (code.isEmpty()) {
        showRepairExport(schedule, {}, {});
        return;
    }

    const auto session = sessionGeneration;
    QNetworkRequest request(Firestore::inquiryUrl(collection, code));
    request.setTransferTimeout(15000);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(idToken).toUtf8());
    ui->createContactButton->setEnabled(false);
    ui->scheduleRepairButton->setEnabled(false);
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, schedule, code] {
        reply->deleteLater();
        if (session != sessionGeneration)
            return;

        ui->createContactButton->setEnabled(true);
        ui->scheduleRepairButton->setEnabled(true);
        const auto document = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            showError(tr("Could not load inquiry %1 for the export. %2")
                          .arg(code, Firestore::firestoreError(
                              document.value("error").toObject(), reply->errorString())));
            if (!Firestore::isNotFound(reply, document))
                return;
        }

        showRepairExport(schedule, code, document.value("fields").toObject());
    });
}

void MainIdyll::showRepairExport(bool schedule, const QString &inquiryCode, const QJsonObject &fields)
{
    QDialog dialog(this);
    dialog.setWindowFlag(Qt::Dialog, true);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setWindowTitle(schedule ? tr("Schedule repair") : tr("Create contact"));
    auto *form = new QFormLayout(&dialog);
    const auto read = [&fields](const QString &key) { return Firestore::readStringField(fields, key); };

    auto *name = new QLineEdit(read("name"), &dialog);
    auto *phone = new QLineEdit(read("phone"), &dialog);
    auto *email = new QLineEdit(read("email"), &dialog);
    auto *address = new QPlainTextEdit(read("address"), &dialog);
    address->setFixedHeight(64);
    auto *preferredContact = new QComboBox(&dialog);
    setPreferredContact(*preferredContact, read("preferredContact"));
    form->addRow(tr("Customer name"), name);
    form->addRow(tr("Phone"), phone);
    form->addRow(tr("Email"), email);
    form->addRow(tr("Address"), address);
    form->addRow(tr("Preferred contact"), preferredContact);

    QLineEdit *device = nullptr;
    QLineEdit *model = nullptr;
    QPlainTextEdit *problem = nullptr;
    QDateTimeEdit *start = nullptr;
    QSpinBox *duration = nullptr;
    QSpinBox *reminder = nullptr;
    QLineEdit *title = nullptr;
    QLineEdit *location = nullptr;
    QCheckBox *includeContact = nullptr;
    if (schedule) {
        device = new QLineEdit(read("device"), &dialog);
        model = new QLineEdit(read("model"), &dialog);
        problem = new QPlainTextEdit(read("problem"), &dialog);
        problem->setFixedHeight(64);
        start = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), &dialog);
        start->setCalendarPopup(true);
        start->setDisplayFormat(QLocale().dateTimeFormat(QLocale::ShortFormat));
        duration = new QSpinBox(&dialog);
        duration->setRange(1, 1440);
        duration->setValue(60);
        reminder = new QSpinBox(&dialog);
        reminder->setRange(0, 10080);
        reminder->setSpecialValueText(tr("No reminder"));
        reminder->setValue(60);
        title = new QLineEdit(&dialog);
        title->setPlaceholderText(tr("Generated from the repair details"));
        location = new QLineEdit(&dialog);
        includeContact = new QCheckBox(tr("Also open in Contacts"), &dialog);
        form->addRow(tr("Device"), device);
        form->addRow(tr("Model"), model);
        form->addRow(tr("Problem"), problem);
        form->addRow(tr("Appointment"), start);
        form->addRow(tr("Duration (minutes)"), duration);
        form->addRow(tr("Reminder (minutes before)"), reminder);
        form->addRow(tr("Title"), title);
        form->addRow(tr("Location"), location);
        form->addRow(includeContact);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *open = buttons->addButton(schedule ? tr("Open in Calendar") : tr("Open in Contacts"),
                                    QDialogButtonBox::AcceptRole);
    open->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if ((!schedule || includeContact->isChecked()) && name->text().trimmed().isEmpty()) {
            QMessageBox::information(&dialog, tr("Customer name required"),
                                     tr("Enter the customer's name before opening Contacts."));
            name->setFocus();
            return;
        }
        dialog.accept();
    });
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const IdyllwildSync::Customer customer{
        name->text(), phone->text(), email->text(), address->toPlainText(), preferredContact->currentText()
    };
    const IdyllwildSync::Repair repair{
        inquiryCode, schedule ? device->text() : read("device"),
        schedule ? model->text() : read("model"), schedule ? problem->toPlainText() : read("problem")
    };

    QStringList errors;
    if (schedule) {
        const IdyllwildSync::Appointment appointment{
            start->dateTime(), duration->value(), reminder->value(), title->text(), location->text()
        };
        const auto result = IdyllwildSync::scheduleRepair(
            customer, repair, appointment, true, includeContact->isChecked());
        if (!result.calendar.ok)
            errors.append(result.calendar.error);
        if (!result.contact.ok)
            errors.append(result.contact.error);
    } else {
        const auto result = IdyllwildSync::addToContacts(customer, repair);
        if (!result.ok)
            errors.append(result.error);
    }

    if (errors.isEmpty()) {
        ui->statusbar->showMessage(schedule ? tr("Appointment sent to Calendar for import.")
                                           : tr("Contact sent to Contacts for import."));
    } else {
        QMessageBox warning(QMessageBox::Warning, tr("Calendar and Contacts"),
                            errors.join(QLatin1Char('\n')), QMessageBox::Ok, this);
        warning.setTextFormat(Qt::PlainText);
        warning.exec();
    }
}
