#ifndef BRANDSTYLE_H
#define BRANDSTYLE_H

#include <QFontDatabase>
#include <QString>
#include <QStringList>

namespace BrandStyle {

inline const QString windowBackground = QStringLiteral("#181A1B");

inline const QString &titleFontFamily()
{
    static const QString family = [] {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/branding/PricedownBl.otf"));
        return QFontDatabase::applicationFontFamilies(id).value(0, QStringLiteral("Arial"));
    }();
    return family;
}

} // namespace BrandStyle

#endif
