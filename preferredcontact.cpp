#include "preferredcontact.h"

#include <QComboBox>

void setPreferredContact(QComboBox &box, const QString &storedValue)
{
    box.clear();
    box.setPlaceholderText(QComboBox::tr("Select preferred contact"));
    box.addItems({QStringLiteral("email"), QStringLiteral("phone"), QStringLiteral("text message")});
    if (storedValue.trimmed().isEmpty()) {
        box.setCurrentIndex(-1);
        return;
    }

    int index = box.findText(storedValue.trimmed(), Qt::MatchFixedString);
    if (index < 0) {
        // Preserve existing records with an older free-text value until the user changes it.
        index = box.count();
        box.addItem(storedValue);
    }
    box.setCurrentIndex(index);
}
