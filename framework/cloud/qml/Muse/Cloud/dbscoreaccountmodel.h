/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - 账户中心模型（激活码授权）
 */

#ifndef MUSE_CLOUD_DBSCOREACCOUNTMODEL_H
#define MUSE_CLOUD_DBSCOREACCOUNTMODEL_H

#include <QObject>
#include <QString>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"

#include "cloud/dbscoreauthorization/idbscoreauthorizationservice.h"

namespace muse::cloud {
class DbScoreAccountModel : public QObject, public async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool hasProAccess READ hasProAccess NOTIFY modelChanged)
    Q_PROPERTY(int licenseState READ licenseState NOTIFY modelChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY modelChanged)
    Q_PROPERTY(QString licenseCode READ licenseCode NOTIFY modelChanged)
    Q_PROPERTY(QString memberType READ memberType NOTIFY modelChanged)
    Q_PROPERTY(QString expireAtText READ expireAtText NOTIFY modelChanged)
    Q_PROPERTY(QString trialHoursText READ trialHoursText NOTIFY modelChanged)
    Q_PROPERTY(int daysUntilExpiry READ daysUntilExpiry NOTIFY modelChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY modelChanged)
    Q_PROPERTY(bool phoneLoggedIn READ phoneLoggedIn NOTIFY modelChanged)
    Q_PROPERTY(QString phoneMasked READ phoneMasked NOTIFY modelChanged)
    Q_PROPERTY(QString phoneNickname READ phoneNickname NOTIFY modelChanged)
    Q_PROPERTY(QString apiBaseUrl READ apiBaseUrl CONSTANT)
    Q_PROPERTY(QString purchasePageUrl READ purchasePageUrl CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    QML_ELEMENT

    muse::GlobalInject<IDbScoreAuthorizationService> authorization;

public:
    explicit DbScoreAccountModel(QObject* parent = nullptr);

    bool hasProAccess() const;
    int licenseState() const;
    QString statusText() const;
    QString licenseCode() const;
    QString memberType() const;
    QString expireAtText() const;
    QString trialHoursText() const;
    int daysUntilExpiry() const;
    QString deviceId() const;
    bool phoneLoggedIn() const;
    QString phoneMasked() const;
    QString phoneNickname() const;
    QString apiBaseUrl() const;
    QString purchasePageUrl() const;
    bool busy() const;
    QString lastError() const;

    Q_INVOKABLE void activate(const QString& code);
    Q_INVOKABLE void sendCode(const QString& phone);
    Q_INVOKABLE void loginByPhone(const QString& phone, const QString& code);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearLicense();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void openPurchasePage();

signals:
    void modelChanged();
    void busyChanged();
    void lastErrorChanged();
    void activated();
    void refreshFinished();

private:
    void setBusy(bool busy);
    void setLastError(const QString& error);

    bool m_busy = false;
    QString m_lastError;
};
}

#endif // MUSE_CLOUD_DBSCOREACCOUNTMODEL_H
