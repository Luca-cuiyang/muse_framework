/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - 授权服务实现（激活码模式）
 */

#ifndef MUSE_CLOUD_DBSCOREAUTHORIZATIONSERVICE_H
#define MUSE_CLOUD_DBSCOREAUTHORIZATIONSERVICE_H

#include <QJsonObject>
#include <QObject>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "icloudconfiguration.h"
#include "global/iapplication.h"
#include "network/inetworkmanagercreator.h"

#include "dbscoreauthorization/idbscoreauthorizationservice.h"

namespace muse::cloud {
class DbScoreAuthorizationService : public QObject, public IDbScoreAuthorizationService, public async::Asyncable
{
    Q_OBJECT

    muse::GlobalInject<ICloudConfiguration> cloudConfiguration;
    muse::GlobalInject<IApplication> application;
    muse::GlobalInject<network::INetworkManagerCreator> networkManagerCreator;

public:
    explicit DbScoreAuthorizationService(QObject* parent = nullptr);

    void init() override;

    LicenseState state() const override;
    bool hasProAccess() const override;

    QString licenseCode() const override;
    QString memberType() const override;
    QDateTime expireAt() const override;
    QString deviceId() const override;

    double trialHoursRemaining() const override;
    bool isTrialActive() const override;

    QString apiBaseUrl() const override;
    QString purchasePageUrl() const override;

    async::Promise<muse::Ret> activate(const QString& code, const QString& deviceName) override;
    async::Promise<muse::Ret> refresh() override;
    void clearLicense() override;

    async::Notification stateChanged() const override;

signals:
    void stateChanged();

private:
    QString machineDeviceId() const;
    QString defaultDeviceName() const;
    QString appVersion() const;

    void setTrialStartNowIfNeeded();
    qint64 trialStartMs() const;

    QJsonObject loadLicense() const;
    void saveLicense(const QJsonObject& license) const;
    void clearStoredLicense();

    LicenseState computeState() const;
    void notifyStateChanged();

    async::Promise<muse::RetVal<QJsonObject> > doRequest(const QUrl& url, const QJsonObject& body);

    network::INetworkManagerPtr networkManager();

    mutable QString m_deviceId;
    mutable bool m_deviceIdLoaded = false;
    mutable async::Notification m_stateChanged;
};
}

#endif // MUSE_CLOUD_DBSCOREAUTHORIZATIONSERVICE_H
