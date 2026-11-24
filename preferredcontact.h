#ifndef PREFERREDCONTACT_H
#define PREFERREDCONTACT_H

#include <QString>

class QComboBox;

void setPreferredContact(QComboBox &box, const QString &storedValue = {});

#endif
