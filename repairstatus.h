#ifndef REPAIRSTATUS_H
#define REPAIRSTATUS_H

#include <QColor>
#include <QComboBox>

namespace RepairStatus {

inline const QString readyForPickup = QStringLiteral("Ready For Pickup");
inline const QString pendingPaymentReview = QStringLiteral("Pending Payment Review");
inline const QString done = QStringLiteral("Done!");
inline const QStringList workflow{
    QStringLiteral("Received"), QStringLiteral("In Repair"), QStringLiteral("On Hold"),
    readyForPickup, pendingPaymentReview, done, QStringLiteral("Canceled")
};

inline QString normalized(const QString &status)
{
    const QString trimmed = status.trimmed();
    const QString key = trimmed.toLower();
    if (key == QStringLiteral("recieved"))
        return workflow.first();
    if (key == QStringLiteral("in progress") || key == QStringLiteral("in-progress")
        || key == QStringLiteral("in-repair"))
        return QStringLiteral("In Repair");
    if (key == QStringLiteral("completed") || key == QStringLiteral("done"))
        return done;
    if (key == QStringLiteral("awaiting payment"))
        return readyForPickup;
    for (const auto &label : workflow) {
        if (trimmed.compare(label, Qt::CaseInsensitive) == 0)
            return label;
    }
    return trimmed;
}

inline QColor accent(const QString &status)
{
    if (status == readyForPickup)
        return QColor(QStringLiteral("#8ED8FF"));
    if (status == pendingPaymentReview)
        return QColor(QStringLiteral("#FFBE80"));
    return {};
}

inline void addOptions(QComboBox &combo, const QStringList &statuses = workflow)
{
    for (const auto &status : statuses) {
        combo.addItem(status);
        const QColor color = accent(status);
        if (color.isValid())
            combo.setItemData(combo.count() - 1, color, Qt::ForegroundRole);
    }
}

}

#endif
