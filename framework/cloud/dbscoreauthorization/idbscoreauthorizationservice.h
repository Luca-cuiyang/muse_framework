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
#include <QVariantMap>

#include "modularity/imoduleinterface.h"
#include "async/promise.h"
#include "async/notification.h"
#include "types/ret.h"
#include "types/retval.h"

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
    virtual bool isPermanentMember() const = 0;

    virtual QDateTime expireAt() const = 0;

    virtual double trialHoursRemaining() const = 0;

    virtual bool isPhoneLoggedIn() const = 0;
    virtual QString phoneNumber() const = 0;
    virtual QString phoneMasked() const = 0;
    virtual QString phoneNickname() const = 0;
    virtual QString avatarUrl() const = 0;
    virtual QString accountCreatedAt() const = 0;
    virtual QString membershipPlan() const = 0;
    virtual QString membershipKind() const = 0;
    virtual QString membershipStartsAt() const = 0;
    virtual QString membershipSource() const = 0;
    virtual int renewalPriceFen() const = 0;
    virtual QString renewalPlanName() const = 0;
    virtual QString lastOtpDebugCode() const = 0;

    virtual QString apiBaseUrl() const = 0;
    virtual QString currentVersion() const = 0;

    virtual async::Promise<muse::Ret> sendCode(const QString& phone) = 0;
    virtual async::Promise<muse::Ret> loginByPhone(const QString& phone, const QString& code) = 0;
    virtual async::Promise<muse::Ret> updateProfile(const QVariantMap& data) = 0;
    virtual async::Promise<muse::Ret> refresh() = 0;
    virtual async::Promise<muse::RetVal<QVariantMap> > accountGet(const QString& path) = 0;
    virtual async::Promise<muse::RetVal<QVariantMap> > accountPost(const QString& path, const QVariantMap& body) = 0;
    virtual void logout() = 0;

    virtual async::Notification stateChanged() const = 0;
};

using IDbScoreAuthorizationServicePtr = std::shared_ptr<IDbScoreAuthorizationService>;
}

#endif // MUSE_CLOUD_IDBSCOREAUTHORIZATIONSERVICE_H
