/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - 账户中心模型（激活码授权）
 */

#include "dbscoreaccountmodel.h"

#include <QDesktopServices>
#include <QGuiApplication>
#include <QClipboard>
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
    if (!authorization()->isPhoneLoggedIn()) {
        return QStringLiteral("未登录，登录后赠送3天免费试用");
    }

    switch (authorization()->state()) {
    case IDbScoreAuthorizationService::LicenseState::Active:
        return QStringLiteral("🟢DB-Score年度会员，到期时间：") + expireAtText();
    case IDbScoreAuthorizationService::LicenseState::Trial:
        return QStringLiteral("✅正在使用免费版 · 3天试用期，剩余：") + trialHoursText();
    case IDbScoreAuthorizationService::LicenseState::TrialExpired:
        return QStringLiteral("免费版 · 试用期已结束，开通会员解锁全部功能");
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
        return QStringLiteral("—");
    }

    return expireAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"));
}

QString DbScoreAccountModel::trialHoursText() const
{
    double hours = authorization()->trialHoursRemaining();
    if (hours <= 0.0) {
        return QStringLiteral("0天");
    }

    if (hours >= 24.0) {
        int totalHours = qRound(hours);
        int days = totalHours / 24;
        int remainHours = totalHours % 24;
        if (remainHours > 0) {
            return QString::number(days) + QStringLiteral("天")
                   + QString::number(remainHours) + QStringLiteral("小时");
        }
        return QString::number(days) + QStringLiteral("天");
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

QString DbScoreAccountModel::avatarLetter() const
{
    QString nickname = authorization()->phoneNickname();
    if (!nickname.isEmpty()) {
        return nickname.left(1).toUpper();
    }

    QString phone = authorization()->phoneMasked();
    if (!phone.isEmpty()) {
        return phone.left(1);
    }

    return QStringLiteral("D");
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

QString DbScoreAccountModel::activationMessage() const
{
    return m_activationMessage;
}

QString DbScoreAccountModel::currentVersion() const
{
    return authorization()->currentVersion();
}

QString DbScoreAccountModel::latestVersion() const
{
    return m_latestVersion;
}

bool DbScoreAccountModel::updateAvailable() const
{
    return m_updateAvailable;
}

QString DbScoreAccountModel::updateDownloadUrl() const
{
    return m_updateDownloadUrl;
}

QString DbScoreAccountModel::updateNotes() const
{
    return m_updateNotes;
}

void DbScoreAccountModel::activate(const QString& code)
{
    if (m_busy) {
        return;
    }

    if (!authorization()->isPhoneLoggedIn()) {
        setLastError(QStringLiteral("请先登录后，才能激活会员"));
        return;
    }

    setBusy(true);
    setLastError(QString());
    setActivationMessage(QString());

    authorization()->activate(code, QString()).onResolve(this, [this](const muse::Ret& ret) {
        setBusy(false);
        if (!ret) {
            setLastError(QString::fromStdString(ret.text()));
            return;
        }

        setActivationMessage(QStringLiteral("激活成功，会员到期时间：") + expireAtText());
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

void DbScoreAccountModel::copyText(const QString& text)
{
    if (text.isEmpty()) {
        return;
    }

    QGuiApplication::clipboard()->setText(text);
}

void DbScoreAccountModel::checkForUpdate()
{
    if (m_busy) {
        return;
    }

    setBusy(true);
    setLastError(QString());

    authorization()->checkForUpdate().onResolve(this, [this](const muse::RetVal<QVariantMap>& result) {
        setBusy(false);
        if (!result.ret) {
            setLastError(QString::fromStdString(result.ret.text()));
            return;
        }

        m_latestVersion = result.val.value("latestVersion").toString();
        m_updateDownloadUrl = result.val.value("downloadUrl").toString();
        m_updateNotes = result.val.value("notes").toString();
        m_updateAvailable = result.val.value("hasUpdate").toBool();
        emit updateInfoChanged();
    });
}

void DbScoreAccountModel::openUpdateDownload()
{
    if (!m_updateDownloadUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_updateDownloadUrl));
    }
}

QString DbScoreAccountModel::inviteCode() const
{
    return m_inviteCode;
}

QString DbScoreAccountModel::inviteUrl() const
{
    return m_inviteUrl;
}

int DbScoreAccountModel::commissionBalanceCents() const
{
    return m_commissionBalanceCents;
}

int DbScoreAccountModel::totalEarnedCents() const
{
    return m_totalEarnedCents;
}

int DbScoreAccountModel::inviteeCount() const
{
    return m_inviteeCount;
}

QVariantList DbScoreAccountModel::ordersList() const
{
    return m_ordersList;
}

static QVariantMap dataMap(const QVariantMap& obj)
{
    QVariantMap data = obj.value("data").toMap();
    if (data.isEmpty()) {
        data = obj;
    }
    return data;
}

void DbScoreAccountModel::loadInvite()
{
    authorization()->apiGet("/api/invite").onResolve(this, [this](const muse::RetVal<QVariantMap>& result) {
        if (!result.ret) {
            return;
        }

        QVariantMap data = dataMap(result.val);
        m_inviteCode = data.value("inviteCode").toString();
        m_inviteUrl = data.value("inviteUrl").toString();
        m_commissionBalanceCents = data.value("commissionBalanceCents").toInt();
        m_totalEarnedCents = data.value("totalEarnedCents").toInt();
        m_inviteeCount = data.value("inviteeCount").toInt();
        emit inviteChanged();
    });
}

void DbScoreAccountModel::loadOrders()
{
    authorization()->apiGet("/api/orders?page=1&pageSize=50").onResolve(this, [this](const muse::RetVal<QVariantMap>& result) {
        if (!result.ret) {
            return;
        }

        QVariantMap data = dataMap(result.val);
        m_ordersList = data.value("list").toList();
        emit ordersChanged();
    });
}

void DbScoreAccountModel::submitFeedback(const QString& content, const QString& contact)
{
    if (m_busy || content.trimmed().isEmpty()) {
        return;
    }

    setBusy(true);
    setLastError(QString());

    QVariantMap body;
    body["content"] = content;
    if (!contact.isEmpty()) {
        body["contact"] = contact;
    }

    authorization()->apiPost("/api/feedback", body).onResolve(this, [this](const muse::RetVal<QVariantMap>& result) {
        setBusy(false);
        if (!result.ret) {
            setLastError(QString::fromStdString(result.ret.text()));
            return;
        }

        emit feedbackSent();
    });
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

void DbScoreAccountModel::setActivationMessage(const QString& message)
{
    if (m_activationMessage == message) {
        return;
    }

    m_activationMessage = message;
    emit activationMessageChanged();
}
