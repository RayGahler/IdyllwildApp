#ifndef INVOICE_H
#define INVOICE_H

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUrl>

class QWidget;

struct GeneratedInvoice
{
    QString number;
    QByteArray pdf;
    qint64 amountDueCents = 0;
};

QUrl customerInvoiceUrl(const QString &inquiryCode, const QString &invoiceNumber,
                        const QString &paymentMethod = {});

bool createInvoice(QWidget *parent, const QString &inquiryCode,
                   const QJsonObject &privateFields, GeneratedInvoice *result);

#ifdef Q_OS_MACOS
struct ICloudInvoiceLink
{
    QUrl url;
    QDateTime expiresAt;
    QString error;
};

ICloudInvoiceLink publishMacInvoice(const QString &code, const QString &number,
                                    const QByteArray &pdfBytes);
#endif

#endif
