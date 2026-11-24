#ifndef IDYLLWILDSYNC_H
#define IDYLLWILDSYNC_H

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QSaveFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>

namespace IdyllwildSync {

struct Customer {
    QString name;
    QString phone;
    QString email;
    QString address;
    QString preferredContact;
};

struct Repair {
    QString inquiryCode;
    QString device;
    QString model;
    QString problem;
};

struct Appointment {
    QDateTime start;
    int durationMinutes = 60;
    int reminderMinutes = 60;
    QString title;
    QString location;
};

struct Result {
    bool ok = false;
    QString filePath;
    QString error;
};

struct ScheduleResult {
    Result calendar;
    Result contact;
    bool ok() const { return calendar.ok && contact.ok; }
};

namespace detail {

inline QString escapeText(QString text)
{
    return text.replace(QStringLiteral("\\"), QStringLiteral("\\\\"))
        .replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
        .replace(QLatin1Char('\r'), QLatin1Char('\n'))
        .replace(QStringLiteral("\n"), QStringLiteral("\\n"))
        .replace(QStringLiteral(";"), QStringLiteral("\\;"))
        .replace(QStringLiteral(","), QStringLiteral("\\,"));
}

inline QByteArray foldLines(const QStringList &lines)
{
    QByteArray output;
    for (const auto &line : lines) {
        const auto bytes = line.toUtf8();
        qsizetype first = 0;
        while (first < bytes.size()) {
            qsizetype next = qMin(first + (first == 0 ? 75 : 74), qsizetype(bytes.size()));
            while (next < bytes.size() && (static_cast<unsigned char>(bytes.at(next)) & 0xc0) == 0x80)
                --next;
            if (first > 0)
                output += ' ';
            output += bytes.mid(first, next - first);
            if (next < bytes.size())
                output += "\r\n";
            first = next;
        }
        output += "\r\n";
    }
    return output;
}

inline QString utcStamp(const QDateTime &dateTime)
{
    return dateTime.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

inline QString newUid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral("@idyllwildelectronics.com");
}

inline QString repairNotes(const Customer &customer, const Repair &repair)
{
    QStringList notes;
    const auto add = [&notes](const QString &label, const QString &value) {
        if (!value.trimmed().isEmpty())
            notes.append(label + QStringLiteral(": ") + value.trimmed());
    };
    add(QStringLiteral("Device"), repair.device);
    add(QStringLiteral("Model"), repair.model);
    add(QStringLiteral("What's wrong"), repair.problem);
    add(QStringLiteral("Customer"), customer.name);
    add(QStringLiteral("Phone"), customer.phone);
    add(QStringLiteral("Email"), customer.email);
    add(QStringLiteral("Preferred contact"), customer.preferredContact);
    add(QStringLiteral("Inquiry code"), repair.inquiryCode);
    return notes.join(QLatin1Char('\n'));
}

inline QTemporaryDir &exportDirectory()
{
    static QTemporaryDir directory(QDir::temp().filePath(QStringLiteral("IdyllwildSync-XXXXXX")));
    return directory;
}

inline Result writeAndOpen(const QString &suffix, const QByteArray &content, bool openApp, const QString &appKind)
{
    auto &directory = exportDirectory();
    if (!directory.isValid())
        return {false, {}, QStringLiteral("Could not create a temporary export folder.")};

    const QString path = QDir(directory.path()).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + suffix);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit())
        return {false, {}, QStringLiteral("Could not save the %1 file: %2").arg(appKind, file.errorString())};
    if (openApp && !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        return {false, path, QStringLiteral("Could not open the default %1 app. The file is saved at %2.")
                                 .arg(appKind, path)};
    return {true, path, {}};
}

}

inline QByteArray calendarFile(const Customer &customer, const Repair &repair, const Appointment &appointment)
{
    const QString device = (repair.device.trimmed() + QLatin1Char(' ') + repair.model.trimmed()).trimmed();
    QString title = appointment.title.trimmed();
    if (title.isEmpty()) {
        title = QStringLiteral("Repair: ") + (device.isEmpty() ? QStringLiteral("device") : device);
        if (!customer.name.trimmed().isEmpty())
            title += QStringLiteral(" - ") + customer.name.trimmed();
    }

    QStringList lines{
        QStringLiteral("BEGIN:VCALENDAR"), QStringLiteral("VERSION:2.0"),
        QStringLiteral("PRODID:-//Idyllwild Electronic Repair & More//Repair Scheduler//EN"),
        QStringLiteral("CALSCALE:GREGORIAN"), QStringLiteral("BEGIN:VEVENT"),
        QStringLiteral("UID:") + detail::newUid(),
        QStringLiteral("DTSTAMP:") + detail::utcStamp(QDateTime::currentDateTimeUtc()),
        QStringLiteral("DTSTART:") + detail::utcStamp(appointment.start),
        QStringLiteral("DTEND:") + detail::utcStamp(appointment.start.addSecs(qint64(qMax(1, appointment.durationMinutes)) * 60)),
        QStringLiteral("SUMMARY:") + detail::escapeText(title),
        QStringLiteral("DESCRIPTION:") + detail::escapeText(detail::repairNotes(customer, repair)),
        QStringLiteral("STATUS:CONFIRMED"), QStringLiteral("TRANSP:OPAQUE")
    };
    if (!appointment.location.trimmed().isEmpty())
        lines.append(QStringLiteral("LOCATION:") + detail::escapeText(appointment.location.trimmed()));
    if (appointment.reminderMinutes > 0) {
        lines << QStringLiteral("BEGIN:VALARM") << QStringLiteral("ACTION:DISPLAY")
              << QStringLiteral("DESCRIPTION:") + detail::escapeText(title)
              << QStringLiteral("TRIGGER:-PT%1M").arg(appointment.reminderMinutes)
              << QStringLiteral("END:VALARM");
    }
    lines << QStringLiteral("END:VEVENT") << QStringLiteral("END:VCALENDAR");
    return detail::foldLines(lines);
}

inline QByteArray contactFile(const Customer &customer, const Repair &repair = {})
{
    const QString fullName = customer.name.simplified();
    const auto separator = fullName.lastIndexOf(QLatin1Char(' '));
    const QString given = separator < 0 ? fullName : fullName.left(separator);
    const QString family = separator < 0 ? QString() : fullName.mid(separator + 1);
    QStringList notes{QStringLiteral("Customer of Idyllwild Electronic Repair & More")};
    if (!repair.inquiryCode.trimmed().isEmpty())
        notes << QStringLiteral("Inquiry: ") + repair.inquiryCode.trimmed();
    const QString device = (repair.device.trimmed() + QLatin1Char(' ') + repair.model.trimmed()).trimmed();
    if (!device.isEmpty())
        notes << QStringLiteral("Device: ") + device;
    if (!customer.preferredContact.trimmed().isEmpty())
        notes << QStringLiteral("Preferred contact: ") + customer.preferredContact.trimmed();

    QStringList lines{
        QStringLiteral("BEGIN:VCARD"), QStringLiteral("VERSION:3.0"),
        QStringLiteral("PRODID:-//Idyllwild Electronic Repair & More//Repair Scheduler//EN"),
        QStringLiteral("N:%1;%2;;;").arg(detail::escapeText(family), detail::escapeText(given)),
        QStringLiteral("FN:") + detail::escapeText(fullName)
    };
    if (!customer.phone.trimmed().isEmpty())
        lines << QStringLiteral("TEL;TYPE=CELL,VOICE:") + detail::escapeText(customer.phone.trimmed());
    if (!customer.email.trimmed().isEmpty())
        lines << QStringLiteral("EMAIL;TYPE=INTERNET:") + detail::escapeText(customer.email.trimmed());
    if (!customer.address.trimmed().isEmpty())
        lines << QStringLiteral("ADR;TYPE=HOME:;;") + detail::escapeText(customer.address.trimmed()) + QStringLiteral(";;;;");
    lines << QStringLiteral("NOTE:") + detail::escapeText(notes.join(QLatin1Char('\n')))
          << QStringLiteral("UID:") + detail::newUid()
          << QStringLiteral("REV:") + detail::utcStamp(QDateTime::currentDateTimeUtc())
          << QStringLiteral("END:VCARD");
    return detail::foldLines(lines);
}

inline Result addToCalendar(const Customer &customer, const Repair &repair, const Appointment &appointment,
                            bool openInDefaultApp = true)
{
    if (!appointment.start.isValid())
        return {false, {}, QStringLiteral("Pick a valid appointment date and time first.")};
    return detail::writeAndOpen(QStringLiteral(".ics"), calendarFile(customer, repair, appointment),
                                openInDefaultApp, QStringLiteral("calendar"));
}

inline Result addToContacts(const Customer &customer, const Repair &repair = {}, bool openInDefaultApp = true)
{
    if (customer.name.trimmed().isEmpty())
        return {false, {}, QStringLiteral("Enter the customer's name first.")};
    return detail::writeAndOpen(QStringLiteral(".vcf"), contactFile(customer, repair),
                                openInDefaultApp, QStringLiteral("contacts"));
}

inline ScheduleResult scheduleRepair(const Customer &customer, const Repair &repair, const Appointment &appointment,
                                     bool addCalendar = true, bool addContact = true)
{
    ScheduleResult result;
    result.calendar = addCalendar ? addToCalendar(customer, repair, appointment) : Result{true, {}, {}};
    result.contact = addContact ? addToContacts(customer, repair) : Result{true, {}, {}};
    return result;
}

}

#endif
