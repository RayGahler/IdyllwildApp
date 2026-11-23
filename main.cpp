#include "mainidyll.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Idyllwild"));
    QApplication::setApplicationName(QStringLiteral("IdyllwildApp"));
    MainIdyll w;
    w.show();
    return a.exec();
}
