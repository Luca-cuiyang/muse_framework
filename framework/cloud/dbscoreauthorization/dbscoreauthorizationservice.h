/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - DB后台端账号服务
 */

#ifndef MUSE_CLOUD_DBSCOREAUTHORIZATIONSERVICE_H
#define MUSE_CLOUD_DBSCOREAUTHORIZATIONSERVICE_H

#include <QJsonObject>
#include <QObject>
#include <QTimer>

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
    bool isPermanentMember() const override;

    QDateTime expireAt() const override;

    double trialHoursRemaining() const override;

    bool isPhoneLoggedIn() const override;
    QString phoneNumber() const override;
    QString phoneMasked() const override;
    QString phoneNickname() const override;
    QString avatarUrl() const override;
    QString accountCreatedAt() const override;
    QString membershipPlan() const override;
    QString membershipKind() const override;
    QString membershipStartsAt() const override;
    QString membershipSource() const override;
    int renewalPriceFen() const override;
    QString renewalPlanName() const override;
    QString lastOtpDebugCode() const override;

    QString apiBaseUrl() const override;
    QString currentVersion() const override;

    async::Promise<muse::Ret> sendCode(const QString& phone) override;
    async::Promise<muse::Ret> loginByPhone(const QString& phone, const QString& code) override;
    async::Promise<muse::Ret> updateProfile(const QVariantMap& data) override;
    async::Promise<muse::Ret> refresh() override;
    async::Promise<muse::RetVal<QVariantMap> > accountGet(const QString& path) override;
    async::Promise<muse::RetVal<QVariantMap> > accountPost(const QString& path, const QVariantMap& body) override;
    void logout() override;

    async::Notification stateChanged() const override;

signals:
    void stateChanged();

private:
    QString machineDeviceId() const;
    QString defaultDeviceName() const;
    QString appVersion() const;
    QString platformCode() const;
    QString authTokenHeader() const;
    network::RequestHeaders authHeaders() const;

    void scheduleStartupRefresh();
    QJsonObject loadPhoneUser() const;
    void savePhoneSession();
    async::Promise<muse::Ret> refreshPhoneUser();
    bool isTrialActive() const;

    LicenseState computeState() const;
    void notifyStateChanged();

    async::Promise<muse::RetVal<QJsonObject> > doRequest(const QUrl& url, const QJsonObject& body,
                                                         const network::RequestHeaders& extraHeaders = network::RequestHeaders());

    network::INetworkManagerPtr networkManager();

    mutable QString m_deviceId;
    mutable bool m_deviceIdLoaded = false;
    mutable async::Notification m_stateChanged;
    QTimer m_refreshTimer;
    bool m_phoneLoggedIn = false;
    QString m_phoneToken;
    QString m_otpDebugCode;
    QJsonObject m_phoneUser;
    network::INetworkManagerPtr m_networkManager;
};
}

#endif // MUSE_CLOUD_DBSCOREAUTHORIZATIONSERVICE_H
