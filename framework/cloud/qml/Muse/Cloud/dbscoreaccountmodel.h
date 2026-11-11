/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - 账号模型
 */

#ifndef MUSE_CLOUD_DBSCOREACCOUNTMODEL_H
#define MUSE_CLOUD_DBSCOREACCOUNTMODEL_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"

#include "cloud/dbscoreauthorization/idbscoreauthorizationservice.h"

namespace muse::cloud {
class DbScoreAccountModel : public QObject, public async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool hasProAccess READ hasProAccess NOTIFY modelChanged)
    Q_PROPERTY(bool paidMembershipActive READ paidMembershipActive NOTIFY modelChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY modelChanged)
    Q_PROPERTY(QString shortStatusText READ shortStatusText NOTIFY modelChanged)
    Q_PROPERTY(bool phoneLoggedIn READ phoneLoggedIn NOTIFY modelChanged)
    Q_PROPERTY(QString phoneNumber READ phoneNumber NOTIFY modelChanged)
    Q_PROPERTY(QString phoneMasked READ phoneMasked NOTIFY modelChanged)
    Q_PROPERTY(QString phoneNickname READ phoneNickname NOTIFY modelChanged)
    Q_PROPERTY(QString avatarUrl READ avatarUrl NOTIFY modelChanged)
    Q_PROPERTY(QString avatarLetter READ avatarLetter NOTIFY modelChanged)
    Q_PROPERTY(QString accountCreatedAtText READ accountCreatedAtText NOTIFY modelChanged)
    Q_PROPERTY(QString membershipPlan READ membershipPlan NOTIFY modelChanged)
    Q_PROPERTY(QString membershipKindText READ membershipKindText NOTIFY modelChanged)
    Q_PROPERTY(QString membershipStartedAtText READ membershipStartedAtText NOTIFY modelChanged)
    Q_PROPERTY(QString membershipExpiresAtText READ membershipExpiresAtText NOTIFY modelChanged)
    Q_PROPERTY(QString membershipSourceText READ membershipSourceText NOTIFY modelChanged)
    Q_PROPERTY(QString renewalPriceText READ renewalPriceText NOTIFY modelChanged)
    Q_PROPERTY(QString renewalPlanName READ renewalPlanName NOTIFY modelChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString sendCodeMessage READ sendCodeMessage NOTIFY sendCodeMessageChanged)
    Q_PROPERTY(QVariantList ordersList READ ordersList NOTIFY ordersChanged)
    Q_PROPERTY(bool paymentVisible READ paymentVisible NOTIFY paymentChanged)
    Q_PROPERTY(QString paymentQrSource READ paymentQrSource NOTIFY paymentChanged)
    Q_PROPERTY(QString paymentPlanName READ paymentPlanName NOTIFY paymentChanged)
    Q_PROPERTY(QString paymentAmountText READ paymentAmountText NOTIFY paymentChanged)
    Q_PROPERTY(QString paymentOrderNumber READ paymentOrderNumber NOTIFY paymentChanged)
    Q_PROPERTY(QString paymentStatusText READ paymentStatusText NOTIFY paymentChanged)
    Q_PROPERTY(bool paymentChecking READ paymentChecking NOTIFY paymentChanged)
    Q_PROPERTY(bool paymentCreating READ paymentCreating NOTIFY paymentChanged)

    QML_ELEMENT

    muse::GlobalInject<IDbScoreAuthorizationService> authorization;

public:
    explicit DbScoreAccountModel(QObject* parent = nullptr);

    bool hasProAccess() const;
    bool paidMembershipActive() const;
    QString statusText() const;
    QString shortStatusText() const;
    bool phoneLoggedIn() const;
    QString phoneNumber() const;
    QString phoneMasked() const;
    QString phoneNickname() const;
    QString avatarUrl() const;
    QString avatarLetter() const;
    QString accountCreatedAtText() const;
    QString membershipPlan() const;
    QString membershipKindText() const;
    QString membershipStartedAtText() const;
    QString membershipExpiresAtText() const;
    QString membershipSourceText() const;
    QString renewalPriceText() const;
    QString renewalPlanName() const;
    bool busy() const;
    QString lastError() const;
    QString sendCodeMessage() const;
    QVariantList ordersList() const;
    bool paymentVisible() const;
    QString paymentQrSource() const;
    QString paymentPlanName() const;
    QString paymentAmountText() const;
    QString paymentOrderNumber() const;
    QString paymentStatusText() const;
    bool paymentChecking() const;
    bool paymentCreating() const;

    Q_INVOKABLE void sendCode(const QString& phone);
    Q_INVOKABLE void loginByPhone(const QString& phone, const QString& code);
    Q_INVOKABLE void updateAvatar(const QString& fileUrl);
    Q_INVOKABLE void updateDisplayName(const QString& displayName);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void openPurchasePage();
    Q_INVOKABLE void checkPaymentNow();
    Q_INVOKABLE void closePaymentDialog();
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE void clearTransientMessages();
    Q_INVOKABLE void loadOrders();

signals:
    void modelChanged();
    void busyChanged();
    void lastErrorChanged();
    void sendCodeMessageChanged();
    void ordersChanged();
    void refreshFinished();
    void paymentChanged();

private:
    QString expireAtText() const;
    QString trialHoursText() const;
    void setBusy(bool busy);
    void setLastError(const QString& error);
    void setSendCodeMessage(const QString& message);
    void startPaymentPolling();
    void stopPaymentPolling();
    void setPaymentStatus(const QString& status);

    bool m_busy = false;
    QString m_lastError;
    QString m_sendCodeMessage;
    QVariantList m_ordersList;
    QTimer m_paymentPollTimer;
    QString m_pendingOrderNumber;
    int m_paymentPollCount = 0;
    bool m_paymentVisible = false;
    bool m_paymentChecking = false;
    bool m_paymentCreating = false;
    QString m_paymentQrSource;
    QString m_paymentPlanName;
    QString m_paymentAmountText;
    QString m_paymentStatus;
    quint64 m_paymentRequestGeneration = 0;
};
}

#endif // MUSE_CLOUD_DBSCOREACCOUNTMODEL_H
