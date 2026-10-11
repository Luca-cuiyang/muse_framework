/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - 账户中心模型（激活码授权）
 */

#include "dbscoreaccountmodel.h"

#include <QDesktopServices>
#include <QUrl>

#include "log.h"

using namespace muse::cloud;

DbScoreAccountModel::DbScoreAccountModel(QObject* parent)
    : QObject(parent)
{
    authorization()->stateChanged().onNotify(this, [this]() {
        emit modelChanged();
    });
}

bool DbScoreAccountModel::hasProAccess() const
{
    return authorization()->hasProAccess();
}

int DbScoreAccountModel::licenseState() const
{
    return static_cast<int>(authorization()->state());
}

QString DbScoreAccountModel::statusText() const
{
    if (authorization()->isPhoneLoggedIn()) {
        switch (authorization()->state()) {
        case IDbScoreAuthorizationService::LicenseState::Active:
            return QStringLiteral("🟢DB-Score年度会员，到期时间：") + expireAtText();
        case IDbScoreAuthorizationService::LicenseState::Expired:
            return QStringLiteral("⚠️会员已过期，请续费解锁增值功能");
        default:
            return QStringLiteral("✅已登录，正在使用免费版");
        }
    }

    switch (authorization()->state()) {
    case IDbScoreAuthorizationService::LicenseState::Active:
        return QStringLiteral("🟢DB-Score年度会员，到期时间：") + expireAtText();
    case IDbScoreAuthorizationService::LicenseState::Trial:
        return QStringLiteral("✅正在3天免费试用期，剩余：") + trialHoursText();
    case IDbScoreAuthorizationService::LicenseState::TrialExpired:
        return QStringLiteral("⚠️免费试用已结束，请激活会员解锁增值功能");
    case IDbScoreAuthorizationService::LicenseState::Expired:
        return QStringLiteral("⚠️会员已过期，请续费解锁增值功能");
    case IDbScoreAuthorizationService::LicenseState::Revoked:
        return QStringLiteral("🔴会员已停用");
    default:
        break;
    }

    return QString();
}

QString DbScoreAccountModel::licenseCode() const
{
    return authorization()->licenseCode();
}

QString DbScoreAccountModel::memberType() const
{
    return authorization()->memberType();
}

QString DbScoreAccountModel::expireAtText() const
{
    QDateTime expireAt = authorization()->expireAt();
    if (!expireAt.isValid()) {
        return QStringLiteral("永久");
    }

    return expireAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"));
}

QString DbScoreAccountModel::trialHoursText() const
{
    double hours = authorization()->trialHoursRemaining();
    if (hours <= 0.0) {
        return QStringLiteral("0小时");
    }

    if (hours >= 1.0) {
        return QString::number(qRound(hours)) + QStringLiteral("小时");
    }

    return QString::number(qMax(1, qRound(hours * 60.0))) + QStringLiteral("分钟");
}

int DbScoreAccountModel::daysUntilExpiry() const
{
    QDateTime expireAt = authorization()->expireAt();
    if (!expireAt.isValid()) {
        return -1;
    }

    return qMax(0, static_cast<int>(QDateTime::currentDateTimeUtc().daysTo(expireAt)));
}

QString DbScoreAccountModel::deviceId() const
{
    return authorization()->deviceId();
}

bool DbScoreAccountModel::phoneLoggedIn() const
{
    return authorization()->isPhoneLoggedIn();
}

QString DbScoreAccountModel::phoneMasked() const
{
    return authorization()->phoneMasked();
}

QString DbScoreAccountModel::phoneNickname() const
{
    return authorization()->phoneNickname();
}

QString DbScoreAccountModel::apiBaseUrl() const
{
    return authorization()->apiBaseUrl();
}

QString DbScoreAccountModel::purchasePageUrl() const
{
    return authorization()->purchasePageUrl();
}

bool DbScoreAccountModel::busy() const
{
    return m_busy;
}

QString DbScoreAccountModel::lastError() const
{
    return m_lastError;
}

QString DbScoreAccountModel::sendCodeMessage() const
{
    return m_sendCodeMessage;
}

void DbScoreAccountModel::activate(const QString& code)
{
    if (m_busy) {
        return;
    }

    setBusy(true);
    setLastError(QString());

    authorization()->activate(code, QString()).onResolve(this, [this](const muse::Ret& ret) {
        setBusy(false);
        if (!ret) {
            setLastError(QString::fromStdString(ret.text()));
            return;
        }

        emit modelChanged();
        emit activated();
    });
}

void DbScoreAccountModel::sendCode(const QString& phone)
{
    if (m_busy) {
        return;
    }

    setBusy(true);
    setLastError(QString());
    setSendCodeMessage(QString());

    if (!authorization()) {
        setBusy(false);
        setLastError(QStringLiteral("授权服务未就绪，请重启软件后重试"));
        return;
    }

    authorization()->sendCode(phone).onResolve(this, [this](const muse::Ret& ret) {
        setBusy(false);
        if (!ret) {
            setLastError(QString::fromStdString(ret.text()));
            return;
        }
        setSendCodeMessage(QStringLiteral("验证码已发送，请注意查收短信"));
    });
}

void DbScoreAccountModel::loginByPhone(const QString& phone, const QString& code)
{
    if (m_busy) {
        return;
    }

    setBusy(true);
    setLastError(QString());

    authorization()->loginByPhone(phone, code).onResolve(this, [this](const muse::Ret& ret) {
        setBusy(false);
        if (!ret) {
            setLastError(QString::fromStdString(ret.text()));
            return;
        }

        emit modelChanged();
    });
}

void DbScoreAccountModel::refresh()
{
    if (m_busy) {
        return;
    }

    setBusy(true);
    setLastError(QString());

    authorization()->refresh().onResolve(this, [this](const muse::Ret& ret) {
        setBusy(false);
        if (!ret) {
            setLastError(QString::fromStdString(ret.text()));
        }

        emit modelChanged();
        emit refreshFinished();
    });
}

void DbScoreAccountModel::clearLicense()
{
    authorization()->clearLicense();
    setLastError(QString());
    emit modelChanged();
}

void DbScoreAccountModel::logout()
{
    authorization()->logout();
    setLastError(QString());
    emit modelChanged();
}

void DbScoreAccountModel::openPurchasePage()
{
    QDesktopServices::openUrl(QUrl(authorization()->purchasePageUrl()));
}

void DbScoreAccountModel::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void DbScoreAccountModel::setLastError(const QString& error)
{
    if (m_lastError == error) {
        return;
    }

    m_lastError = error;
    emit lastErrorChanged();
}

void DbScoreAccountModel::setSendCodeMessage(const QString& message)
{
    if (m_sendCodeMessage == message) {
        return;
    }

    m_sendCodeMessage = message;
    emit sendCodeMessageChanged();
}
