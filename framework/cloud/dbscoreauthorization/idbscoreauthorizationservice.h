/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - 授权服务接口
 */

#ifndef MUSE_CLOUD_IDBSCOREAUTHORIZATIONSERVICE_H
#define MUSE_CLOUD_IDBSCOREAUTHORIZATIONSERVICE_H

#include <QDateTime>
#include <QString>

#include "modularity/imoduleinterface.h"
#include "async/promise.h"
#include "async/notification.h"
#include "types/ret.h"

namespace muse::cloud {
class IDbScoreAuthorizationService : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IDbScoreAuthorizationService)

public:
    enum class LicenseState {
        Unknown,
        Trial,
        TrialExpired,
        Active,
        Expired,
        Revoked
    };

    virtual ~IDbScoreAuthorizationService() = default;

    virtual void init() = 0;

    virtual LicenseState state() const = 0;
    virtual bool hasProAccess() const = 0;

    virtual QString licenseCode() const = 0;
    virtual QString memberType() const = 0;
    virtual QDateTime expireAt() const = 0;
    virtual QString deviceId() const = 0;

    virtual double trialHoursRemaining() const = 0;
    virtual bool isTrialActive() const = 0;

    virtual QString apiBaseUrl() const = 0;
    virtual QString purchasePageUrl() const = 0;

    virtual async::Promise<muse::Ret> activate(const QString& code, const QString& deviceName) = 0;
    virtual async::Promise<muse::Ret> refresh() = 0;
    virtual void clearLicense() = 0;

    virtual async::Notification stateChanged() const = 0;
};

using IDbScoreAuthorizationServicePtr = std::shared_ptr<IDbScoreAuthorizationService>;
}

#endif // MUSE_CLOUD_IDBSCOREAUTHORIZATIONSERVICE_H
