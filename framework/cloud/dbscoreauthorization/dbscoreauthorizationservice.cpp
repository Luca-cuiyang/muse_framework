/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - 授权服务实现（激活码模式）
 */

#include "dbscoreauthorizationservice.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSysInfo>
#include <QUrl>
#include <QUrlQuery>

#include "defer.h"
#include "log.h"
#include "settings.h"

using namespace muse;
using namespace muse::cloud;
using namespace muse::network;
using namespace muse::async;

static const std::string dbscore_authorization_module_name("cloud");

static const Settings::Key TRIAL_START_MS_KEY(dbscore_authorization_module_name, "dbscoreAuthorization/trialStartMs");
static const Settings::Key LICENSE_CACHE_KEY(dbscore_authorization_module_name, "dbscoreAuthorization/licenseCache");

static const QString API_BASE_URL("https://drumbearai.com");
static const QString ACTIVATE_URL(API_BASE_URL + "/api/dbscore/activate");
static const QString LICENSE_URL(API_BASE_URL + "/api/dbscore/license");
static const QString HEARTBEAT_URL(API_BASE_URL + "/api/dbscore/heartbeat");
static const QString PURCHASE_PAGE_URL(API_BASE_URL + "/");

static constexpr qint64 TRIAL_DURATION_MS = 3 * 24 * 60 * 60 * 1000LL; // 3 天

static QString hashDeviceId(const QByteArray& raw)
{
    if (raw.isEmpty()) {
        return QString();
    }

    QByteArray normalized = raw.toLower();
    normalized.replace('-', "");

    return QString::fromLatin1(QCryptographicHash::hash(normalized, QCryptographicHash::Sha256).toHex()).left(32);
}

static QJsonObject jsonObjectFromData(const QByteArray& data, QJsonParseError* err = nullptr)
{
    QJsonParseError localErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &localErr);
    if (err) {
        *err = localErr;
    }

    if (localErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject();
    }

    return doc.object();
}

DbScoreAuthorizationService::DbScoreAuthorizationService(QObject* parent)
    : QObject(parent)
{
}

void DbScoreAuthorizationService::init()
{
    settings()->setDefaultValue(TRIAL_START_MS_KEY, Val(int64_t(0)));
    settings()->setDefaultValue(LICENSE_CACHE_KEY, Val(std::string()));

    setTrialStartNowIfNeeded();
    (void)machineDeviceId();
    notifyStateChanged();
}

IDbScoreAuthorizationService::LicenseState DbScoreAuthorizationService::state() const
{
    return computeState();
}

bool DbScoreAuthorizationService::hasProAccess() const
{
    LicenseState st = computeState();
    return st == LicenseState::Active || st == LicenseState::Trial;
}

QString DbScoreAuthorizationService::licenseCode() const
{
    return loadLicense().value("code").toString();
}

QString DbScoreAuthorizationService::memberType() const
{
    return loadLicense().value("memberType").toString();
}

QDateTime DbScoreAuthorizationService::expireAt() const
{
    QString value = loadLicense().value("expireAt").toString();
    if (value.isEmpty()) {
        return QDateTime();
    }

    return QDateTime::fromString(value, Qt::ISODate);
}

QString DbScoreAuthorizationService::deviceId() const
{
    return machineDeviceId();
}

double DbScoreAuthorizationService::trialHoursRemaining() const
{
    qint64 start = trialStartMs();
    if (start <= 0) {
        return 0.0;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 remaining = TRIAL_DURATION_MS - (now - start);
    if (remaining <= 0) {
        return 0.0;
    }

    return double(remaining) / (60.0 * 60.0 * 1000.0);
}

bool DbScoreAuthorizationService::isTrialActive() const
{
    return trialHoursRemaining() > 0.0;
}

QString DbScoreAuthorizationService::apiBaseUrl() const
{
    return API_BASE_URL;
}

QString DbScoreAuthorizationService::purchasePageUrl() const
{
    return PURCHASE_PAGE_URL;
}

async::Promise<Ret> DbScoreAuthorizationService::activate(const QString& code, const QString& deviceName)
{
    return make_promise<Ret>([this, code, deviceName](auto resolve, auto /*reject*/) {
        QString trimmed = code.trimmed();
        if (trimmed.isEmpty()) {
            resolve(make_ret(Ret::Code::BadArgs, std::string("激活码不能为空")));
            return Promise<Ret>::dummy_result();
        }

        QJsonObject body;
        body["code"] = trimmed;
        body["deviceId"] = machineDeviceId();
        body["deviceName"] = deviceName.isEmpty() ? defaultDeviceName() : deviceName;
        body["appVersion"] = appVersion();

        doRequest(QUrl(ACTIVATE_URL), body).onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                resolve(result.ret);
                return;
            }

            QJsonObject license = result.val.value("license").toObject();
            if (!license.isEmpty()) {
                saveLicense(license);
                notifyStateChanged();
            }

            resolve(make_ok());
        });

        return Promise<Ret>::dummy_result();
    });
}

async::Promise<Ret> DbScoreAuthorizationService::refresh()
{
    return make_promise<Ret>([this](auto resolve, auto /*reject*/) {
        QUrl url(LICENSE_URL);
        QUrlQuery query;
        query.addQueryItem("deviceId", machineDeviceId());
        url.setQuery(query);

        doRequest(url, QJsonObject()).onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                resolve(result.ret);
                return;
            }

            resolve(make_ok());
        });

        return Promise<Ret>::dummy_result();
    });
}

void DbScoreAuthorizationService::clearLicense()
{
    clearStoredLicense();
    notifyStateChanged();
}

async::Notification DbScoreAuthorizationService::stateChanged() const
{
    return m_stateChanged;
}

async::Promise<RetVal<QJsonObject> > DbScoreAuthorizationService::doRequest(const QUrl& url, const QJsonObject& body)
{
    return make_promise<RetVal<QJsonObject> >([this, url, body](auto resolve, auto /*reject*/) {
        auto received = std::make_shared<QBuffer>();
        RequestHeaders headers;
        headers.rawHeaders["Content-Type"] = "application/json";
        headers.rawHeaders["Accept"] = "application/json";
        headers.rawHeaders["User-Agent"] = QString("DBScore/" + appVersion()).toUtf8();

        RetVal<Progress> progress;
        if (body.isEmpty()) {
            progress = networkManager()->get(url, received, headers);
        } else {
            QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
            auto outgoing = std::make_shared<QBuffer>();
            outgoing->setData(json);
            progress = networkManager()->post(url, outgoing, received, headers);
        }

        if (!progress.ret) {
            resolve(RetVal<QJsonObject>::make_ret(progress.ret));
            return Promise<RetVal<QJsonObject> >::dummy_result();
        }

        progress.val.finished().onReceive(this, [this, received, resolve](const ProgressResult& res) {
            if (!res.ret) {
                resolve(RetVal<QJsonObject>::make_ret(res.ret));
                return;
            }

            QJsonParseError err;
            QJsonObject obj = jsonObjectFromData(received->data(), &err);
            if (err.error != QJsonParseError::NoError || obj.isEmpty()) {
                resolve(RetVal<QJsonObject>::make_ret(Ret::Code::InternalError, std::string("服务器返回异常")));
                return;
            }

            bool success = obj.value("success").toBool(false);
            QString code = obj.value("code").toString();
            QString message = obj.value("message").toString();

            if (!success) {
                if (message.isEmpty()) {
                    message = code;
                }
                resolve(RetVal<QJsonObject>::make_ret(Ret::Code::InternalError, message.toStdString()));
                return;
            }

            QJsonObject data = obj.value("data").toObject();
            resolve(RetVal<QJsonObject>::make_ok(data));
        });

        return Promise<RetVal<QJsonObject> >::dummy_result();
    });
}

network::INetworkManagerPtr DbScoreAuthorizationService::networkManager()
{
    return networkManagerCreator()->makeNetworkManager();
}

QString DbScoreAuthorizationService::machineDeviceId() const
{
    if (m_deviceIdLoaded) {
        return m_deviceId;
    }

    QByteArray raw(QSysInfo::machineUniqueId());
    m_deviceId = hashDeviceId(raw);
    if (m_deviceId.isEmpty()) {
        // 极少数平台可能拿不到 machineUniqueId，退回 clientId
        m_deviceId = hashDeviceId(QByteArray::fromStdString(cloudConfiguration()->clientId()));
    }
    m_deviceIdLoaded = true;
    return m_deviceId;
}

QString DbScoreAuthorizationService::defaultDeviceName() const
{
    QString name = QSysInfo::machineHostName();
    QString product = QSysInfo::prettyProductName();
    if (product.isEmpty()) {
        product = QSysInfo::productType();
    }

    if (name.isEmpty()) {
        return product;
    }
    if (product.isEmpty()) {
        return name;
    }

    return name + " (" + product + ")";
}

QString DbScoreAuthorizationService::appVersion() const
{
    QString version = QString::fromStdString(application()->version().toStdString());
    if (version.isEmpty()) {
        return QStringLiteral("5.0.0");
    }

    return version;
}

void DbScoreAuthorizationService::setTrialStartNowIfNeeded()
{
    qint64 start = trialStartMs();
    if (start <= 0) {
        settings()->setLocalValue(TRIAL_START_MS_KEY, Val(QDateTime::currentMSecsSinceEpoch()));
    }
}

qint64 DbScoreAuthorizationService::trialStartMs() const
{
    return settings()->value(TRIAL_START_MS_KEY).toInt64();
}

QJsonObject DbScoreAuthorizationService::loadLicense() const
{
    QString json = settings()->value(LICENSE_CACHE_KEY).toQString();
    if (json.isEmpty()) {
        return QJsonObject();
    }

    QJsonParseError err;
    QJsonObject obj = jsonObjectFromData(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        return QJsonObject();
    }

    return obj;
}

void DbScoreAuthorizationService::saveLicense(const QJsonObject& license) const
{
    QByteArray json = QJsonDocument(license).toJson(QJsonDocument::Compact);
    settings()->setLocalValue(LICENSE_CACHE_KEY, Val(std::string(json.constData(), json.size())));
}

void DbScoreAuthorizationService::clearStoredLicense()
{
    settings()->setLocalValue(LICENSE_CACHE_KEY, Val(std::string()));
}

IDbScoreAuthorizationService::LicenseState DbScoreAuthorizationService::computeState() const
{
    QJsonObject license = loadLicense();
    if (!license.isEmpty()) {
        QString status = license.value("status").toString();
        if (status == "revoked") {
            return LicenseState::Revoked;
        }

        if (status == "active") {
            QString expire = license.value("expireAt").toString();
            if (expire.isEmpty()) {
                return LicenseState::Active;
            }

            QDateTime expireAt = QDateTime::fromString(expire, Qt::ISODate);
            if (expireAt.isValid() && expireAt > QDateTime::currentDateTimeUtc()) {
                return LicenseState::Active;
            }
            return LicenseState::Expired;
        }

        return LicenseState::Expired;
    }

    return isTrialActive() ? LicenseState::Trial : LicenseState::TrialExpired;
}

void DbScoreAuthorizationService::notifyStateChanged()
{
    m_stateChanged.notify();
    emit stateChanged();
}
