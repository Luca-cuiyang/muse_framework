/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - DB后台端账号服务
 */

#include "dbscoreauthorizationservice.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include "log.h"
#include "settings.h"

using namespace muse;
using namespace muse::cloud;
using namespace muse::network;
using namespace muse::async;

static const std::string dbscore_authorization_module_name("cloud");

static const Settings::Key PHONE_TOKEN_KEY(
    dbscore_authorization_module_name,
    "dbscoreAuthorization/phoneToken");
static const Settings::Key PHONE_USER_KEY(
    dbscore_authorization_module_name,
    "dbscoreAuthorization/phoneUser");
static const Settings::Key LEGACY_LICENSE_KEY(
    dbscore_authorization_module_name,
    "dbscoreAuthorization/licenseCache");
static const Settings::Key LEGACY_TRIAL_KEY(
    dbscore_authorization_module_name,
    "dbscoreAuthorization/trialStartMs");
static const Settings::Key LEGACY_SYNC_KEY(
    dbscore_authorization_module_name,
    "dbscoreAuthorization/lastSyncMs");

// Runtime environments may override the production endpoint for local backend testing.
static const QString PLATFORM_API_BASE_URL(
    qEnvironmentVariable(
        "DB_PLATFORM_API_BASE_URL",
        qEnvironmentVariable("DB_STUDIO_API_BASE_URL", "https://drumbear.cn")));
static const QString PRODUCT_CODE("db-score");

static QString hashDeviceId(const QByteArray& raw)
{
    if (raw.isEmpty()) {
        return QString();
    }

    QByteArray normalized = raw.toLower();
    normalized.replace('-', "");
    return QString::fromLatin1(
        QCryptographicHash::hash(normalized, QCryptographicHash::Sha256).toHex()).left(32);
}

static QJsonObject jsonObjectFromData(const QByteArray& data, QJsonParseError* err = nullptr)
{
    QJsonParseError localErr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &localErr);
    if (err) {
        *err = localErr;
    }

    if (localErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject();
    }
    return doc.object();
}

static bool dateIsActive(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }
    const QDateTime end = QDateTime::fromString(value, Qt::ISODate);
    return end.isValid() && end > QDateTime::currentDateTimeUtc();
}

DbScoreAuthorizationService::DbScoreAuthorizationService(QObject* parent)
    : QObject(parent)
{
    m_refreshTimer.setInterval(5 * 60 * 1000);
    m_refreshTimer.setSingleShot(false);
}

void DbScoreAuthorizationService::init()
{
    settings()->setDefaultValue(PHONE_TOKEN_KEY, Val(std::string()));
    settings()->setDefaultValue(PHONE_USER_KEY, Val(std::string()));
    settings()->setLocalValue(LEGACY_LICENSE_KEY, Val(std::string()));
    settings()->setLocalValue(LEGACY_TRIAL_KEY, Val(int64_t(0)));
    settings()->setLocalValue(LEGACY_SYNC_KEY, Val(int64_t(0)));

    m_phoneToken = settings()->value(PHONE_TOKEN_KEY).toQString();
    m_phoneUser = loadPhoneUser();
    m_phoneLoggedIn = !m_phoneToken.isEmpty();
    notifyStateChanged();

    scheduleStartupRefresh();
    QObject::connect(&m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (isPhoneLoggedIn()) {
            (void)refresh();
        }
    });
    m_refreshTimer.start();
}

IDbScoreAuthorizationService::LicenseState DbScoreAuthorizationService::state() const
{
    return computeState();
}

bool DbScoreAuthorizationService::hasProAccess() const
{
    const LicenseState current = computeState();
    return current == LicenseState::Active || current == LicenseState::Trial;
}

bool DbScoreAuthorizationService::isPermanentMember() const
{
    return isPhoneLoggedIn()
           && (m_phoneUser.value("membershipKind").toString() == QStringLiteral("lifetime")
               || m_phoneUser.value("isLifetime").toBool());
}

QDateTime DbScoreAuthorizationService::expireAt() const
{
    const QString value = m_phoneUser.value("membershipExpiresAt").toString();
    if (value.isEmpty()) {
        return QDateTime();
    }
    return QDateTime::fromString(value, Qt::ISODate);
}

double DbScoreAuthorizationService::trialHoursRemaining() const
{
    if (m_phoneUser.value("membershipKind").toString() != QStringLiteral("trial")) {
        return 0.0;
    }

    const QDateTime end = expireAt();
    if (!end.isValid()) {
        return 0.0;
    }
    const qint64 remainingMs = end.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch();
    return remainingMs > 0 ? double(remainingMs) / (60.0 * 60.0 * 1000.0) : 0.0;
}

bool DbScoreAuthorizationService::isPhoneLoggedIn() const
{
    return m_phoneLoggedIn && !m_phoneToken.isEmpty();
}

QString DbScoreAuthorizationService::phoneNumber() const
{
    return m_phoneUser.value("phone").toString();
}

QString DbScoreAuthorizationService::phoneMasked() const
{
    const QString phone = phoneNumber();
    if (phone.size() == 11 && phone.at(0).isDigit()) {
        return phone.left(3) + QStringLiteral("****") + phone.right(4);
    }
    return phone;
}

QString DbScoreAuthorizationService::phoneNickname() const
{
    return m_phoneUser.value("displayName").toString();
}

QString DbScoreAuthorizationService::avatarUrl() const
{
    return m_phoneUser.value("avatarDataUrl").toString();
}

QString DbScoreAuthorizationService::accountCreatedAt() const
{
    return m_phoneUser.value("createdAt").toString();
}

QString DbScoreAuthorizationService::membershipPlan() const
{
    return m_phoneUser.value("membershipPlan").toString();
}

QString DbScoreAuthorizationService::membershipKind() const
{
    return m_phoneUser.value("membershipKind").toString();
}

QString DbScoreAuthorizationService::membershipStartsAt() const
{
    return m_phoneUser.value("membershipStartsAt").toString();
}

QString DbScoreAuthorizationService::membershipSource() const
{
    return m_phoneUser.value("membershipSource").toString();
}

int DbScoreAuthorizationService::renewalPriceFen() const
{
    return m_phoneUser.value("renewalPriceFen").toInt();
}

QString DbScoreAuthorizationService::renewalPlanName() const
{
    return m_phoneUser.value("renewalPlanName").toString();
}

QString DbScoreAuthorizationService::lastOtpDebugCode() const
{
    return m_otpDebugCode;
}

QString DbScoreAuthorizationService::apiBaseUrl() const
{
    return PLATFORM_API_BASE_URL;
}

QString DbScoreAuthorizationService::currentVersion() const
{
    return appVersion();
}

async::Promise<Ret> DbScoreAuthorizationService::sendCode(const QString& phone)
{
    return make_promise<Ret>([this, phone](auto resolve, auto /*reject*/) {
        QJsonObject body;
        body["phone"] = phone;
        m_otpDebugCode.clear();
        doRequest(QUrl(PLATFORM_API_BASE_URL + "/api/auth/otp/send"), body)
        .onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (result.ret) {
                m_otpDebugCode = result.val.value("debugCode").toString();
            }
            (void)resolve(result.ret);
        });
        return Promise<Ret>::dummy_result();
    });
}

async::Promise<Ret> DbScoreAuthorizationService::loginByPhone(
    const QString& phone,
    const QString& code)
{
    return make_promise<Ret>([this, phone, code](auto resolve, auto /*reject*/) {
        QJsonObject body;
        body["phone"] = phone;
        body["code"] = code;
        body["deviceId"] = machineDeviceId();
        body["deviceName"] = defaultDeviceName();
        body["platform"] = platformCode();
        body["productCode"] = PRODUCT_CODE;

        doRequest(QUrl(PLATFORM_API_BASE_URL + "/api/auth/otp/login"), body)
        .onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                if (QString::fromStdString(result.ret.text()).contains(
                        QStringLiteral("请先登录"))) {
                    logout();
                }
                (void)resolve(result.ret);
                return;
            }

            m_phoneToken = result.val.value("token").toString();
            if (m_phoneToken.isEmpty()) {
                (void)resolve(make_ret(Ret::Code::InternalError, std::string("登录返回异常")));
                return;
            }

            const QJsonObject user = result.val.value("user").toObject();
            m_phoneUser["id"] = user.value("id");
            m_phoneUser["phone"] = user.value("phone").toString();
            m_phoneUser["displayName"] = user.value("displayName").toString();
            m_phoneUser["createdAt"] = user.value("createdAt").toString();
            m_phoneUser["avatarDataUrl"] = user.value("avatarDataUrl").toString();
            m_phoneLoggedIn = true;
            m_otpDebugCode.clear();
            savePhoneSession();

            refreshPhoneUser().onResolve(this, [resolve](const Ret& refreshRet) {
                (void)resolve(refreshRet);
            });
        });

        return Promise<Ret>::dummy_result();
    });
}

async::Promise<Ret> DbScoreAuthorizationService::updateProfile(const QVariantMap& data)
{
    return make_promise<Ret>([this, data](auto resolve, auto /*reject*/) {
        const QJsonObject body = QJsonObject::fromVariantMap(data);
        doRequest(QUrl(PLATFORM_API_BASE_URL + "/api/customer/profile?productCode=" + PRODUCT_CODE),
                  body,
                  authHeaders())
        .onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                if (QString::fromStdString(result.ret.text()).contains(
                        QStringLiteral("请先登录"))) {
                    logout();
                }
                (void)resolve(result.ret);
                return;
            }

            // Apply the profile returned by the server immediately. Refreshing
            // afterwards keeps membership data current, but the avatar must not
            // wait for (or depend on) that second network request.
            const QJsonObject user = result.val.value("user").toObject();
            if (!user.isEmpty()) {
                m_phoneUser["id"] = user.value("id");
                m_phoneUser["phone"] = user.value("phone").toString();
                m_phoneUser["displayName"] = user.value("displayName").toString();
                m_phoneUser["createdAt"] = user.value("createdAt").toString();
                m_phoneUser["avatarDataUrl"] = user.value("avatarDataUrl").toString();
                m_phoneLoggedIn = true;
                savePhoneSession();
                notifyStateChanged();
            }

            refreshPhoneUser().onResolve(this, [resolve](const Ret& ret) {
                (void)resolve(ret);
            });
        });
        return Promise<Ret>::dummy_result();
    });
}

async::Promise<Ret> DbScoreAuthorizationService::refresh()
{
    if (!isPhoneLoggedIn()) {
        return make_promise<Ret>([](auto resolve, auto /*reject*/) {
            return resolve(make_ok());
        });
    }
    return refreshPhoneUser();
}

async::Promise<RetVal<QVariantMap> > DbScoreAuthorizationService::accountGet(const QString& path)
{
    return make_promise<RetVal<QVariantMap> >([this, path](auto resolve, auto /*reject*/) {
        doRequest(QUrl(PLATFORM_API_BASE_URL + path), QJsonObject(), authHeaders())
        .onResolve(this, [resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                (void)resolve(RetVal<QVariantMap>::make_ret(result.ret));
                return;
            }
            (void)resolve(RetVal<QVariantMap>::make_ok(result.val.toVariantMap()));
        });
        return Promise<RetVal<QVariantMap> >::dummy_result();
    });
}

async::Promise<RetVal<QVariantMap> > DbScoreAuthorizationService::accountPost(
    const QString& path,
    const QVariantMap& body)
{
    return make_promise<RetVal<QVariantMap> >([this, path, body](auto resolve, auto /*reject*/) {
        doRequest(QUrl(PLATFORM_API_BASE_URL + path),
                  QJsonObject::fromVariantMap(body),
                  authHeaders())
        .onResolve(this, [resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                (void)resolve(RetVal<QVariantMap>::make_ret(result.ret));
                return;
            }
            (void)resolve(RetVal<QVariantMap>::make_ok(result.val.toVariantMap()));
        });
        return Promise<RetVal<QVariantMap> >::dummy_result();
    });
}

void DbScoreAuthorizationService::logout()
{
    if (!m_phoneToken.isEmpty()) {
        QJsonObject body;
        body["revoke"] = true;
        (void)doRequest(
            QUrl(PLATFORM_API_BASE_URL + "/api/customer/logout"),
            body,
            authHeaders());
    }

    m_phoneToken.clear();
    m_phoneUser = QJsonObject();
    m_phoneLoggedIn = false;
    settings()->setLocalValue(PHONE_TOKEN_KEY, Val(std::string()));
    settings()->setLocalValue(PHONE_USER_KEY, Val(std::string()));
    settings()->sync();
    notifyStateChanged();
}

QString DbScoreAuthorizationService::authTokenHeader() const
{
    return m_phoneToken.isEmpty() ? QString() : QStringLiteral("Bearer ") + m_phoneToken;
}

RequestHeaders DbScoreAuthorizationService::authHeaders() const
{
    RequestHeaders headers;
    if (!m_phoneToken.isEmpty()) {
        headers.rawHeaders["Authorization"] = authTokenHeader().toUtf8();
    }
    return headers;
}

async::Promise<Ret> DbScoreAuthorizationService::refreshPhoneUser()
{
    return make_promise<Ret>([this](auto resolve, auto /*reject*/) {
        doRequest(QUrl(PLATFORM_API_BASE_URL + "/api/customer/profile?productCode=" + PRODUCT_CODE),
                  QJsonObject(),
                  authHeaders())
        .onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                if (QString::fromStdString(result.ret.text()).contains(
                        QStringLiteral("请先登录"))) {
                    logout();
                }
                (void)resolve(result.ret);
                return;
            }

            const QJsonObject user = result.val.value("user").toObject();
            const QJsonObject membership = result.val.value("membership").toObject();
            const QJsonObject renewalPlan = result.val.value("renewalPlan").toObject();
            if (user.value("id").isUndefined() || user.value("id").isNull()) {
                logout();
                (void)resolve(make_ret(
                    Ret::Code::CloudFirst,
                    std::string("登录已过期，请重新登录")));
                return;
            }
            QJsonObject next;
            next["id"] = user.value("id");
            next["phone"] = user.value("phone").toString();
            next["displayName"] = user.value("displayName").toString();
            next["createdAt"] = user.value("createdAt").toString();
            next["avatarDataUrl"] = user.value("avatarDataUrl").toString();
            next["membershipPlan"] = membership.value("plan").toString();
            next["membershipKind"] = membership.value("kind").toString();
            next["membershipStartsAt"] = membership.value("startsAt").toString();
            next["membershipExpiresAt"] = membership.value("expiresAt").toString();
            next["membershipSource"] = membership.value("source").toString();
            next["membershipStatus"] = membership.value("status").toString();
            next["isLifetime"] = membership.value("isLifetime").toBool();
            next["product"] = membership.value("product").toString();
            next["renewalPriceFen"] = renewalPlan.value("priceFen").toInt();
            next["renewalPlanName"] = renewalPlan.value("name").toString();
            m_phoneUser = next;
            m_phoneLoggedIn = true;
            savePhoneSession();
            notifyStateChanged();
            (void)resolve(make_ok());
        });
        return Promise<Ret>::dummy_result();
    });
}

async::Notification DbScoreAuthorizationService::stateChanged() const
{
    return m_stateChanged;
}

async::Promise<RetVal<QJsonObject> > DbScoreAuthorizationService::doRequest(
    const QUrl& url,
    const QJsonObject& body,
    const RequestHeaders& extraHeaders)
{
    return make_promise<RetVal<QJsonObject> >([this, url, body, extraHeaders](auto resolve, auto /*reject*/) {
        auto received = std::make_shared<QBuffer>();
        auto timedOut = std::make_shared<bool>(false);

        QTimer* timeoutTimer = new QTimer(this);
        timeoutTimer->setSingleShot(true);
        QObject::connect(timeoutTimer, &QTimer::timeout, this, [resolve, timedOut, timeoutTimer]() {
            if (*timedOut) {
                return;
            }
            *timedOut = true;
            timeoutTimer->deleteLater();
            (void)resolve(RetVal<QJsonObject>::make_ret(
                Ret::Code::InternalError, std::string("请求超时，请检查网络后重试")));
        });
        timeoutTimer->start(15000);

        RequestHeaders headers;
        headers.rawHeaders["Content-Type"] = "application/json";
        headers.rawHeaders["Accept"] = "application/json";
        headers.rawHeaders["User-Agent"] = QString("DBStudio/" + appVersion()).toUtf8();
        for (auto it = extraHeaders.rawHeaders.constBegin(); it != extraHeaders.rawHeaders.constEnd(); ++it) {
            headers.rawHeaders[it.key()] = it.value();
        }

        RetVal<Progress> progress;
        if (body.isEmpty()) {
            progress = networkManager()->get(url, received, headers);
        } else {
            auto outgoing = std::make_shared<QBuffer>();
            outgoing->setData(QJsonDocument(body).toJson(QJsonDocument::Compact));
            progress = networkManager()->post(url, outgoing, received, headers);
        }

        if (!progress.ret) {
            timeoutTimer->stop();
            timeoutTimer->deleteLater();
            (void)resolve(RetVal<QJsonObject>::make_ret(progress.ret));
            return Promise<RetVal<QJsonObject> >::dummy_result();
        }

        progress.val.finished().onReceive(
            this,
            [received, resolve, timeoutTimer, timedOut](const ProgressResult& result) {
                if (*timedOut) {
                    return;
                }
                *timedOut = true;
                timeoutTimer->stop();
                timeoutTimer->deleteLater();

                if (!result.ret) {
                    const QJsonObject error =
                        jsonObjectFromData(received->data());
                    const QString message =
                        error.value("message").toString().isEmpty()
                            ? error.value("error").toString()
                            : error.value("message").toString();
                    (void)resolve(RetVal<QJsonObject>::make_ret(
                        result.ret, message.toStdString()));
                    return;
                }

                const QJsonObject object = jsonObjectFromData(received->data());
                if (object.isEmpty()) {
                    (void)resolve(RetVal<QJsonObject>::make_ret(
                        Ret::Code::InternalError, std::string("服务器返回异常")));
                    return;
                }

                if (!object.value("error").toString().isEmpty()) {
                    (void)resolve(RetVal<QJsonObject>::make_ret(
                        Ret::Code::InternalError,
                        object.value("error").toString().toStdString()));
                    return;
                }
                (void)resolve(RetVal<QJsonObject>::make_ok(object));
            });

        return Promise<RetVal<QJsonObject> >::dummy_result();
    });
}

network::INetworkManagerPtr DbScoreAuthorizationService::networkManager()
{
    if (!m_networkManager) {
        m_networkManager = networkManagerCreator()->makeNetworkManager();
    }
    return m_networkManager;
}

QString DbScoreAuthorizationService::machineDeviceId() const
{
    if (m_deviceIdLoaded) {
        return m_deviceId;
    }

    QByteArray raw(QSysInfo::machineUniqueId());
    m_deviceId = hashDeviceId(raw);
    if (m_deviceId.isEmpty()) {
        m_deviceId = hashDeviceId(QByteArray::fromStdString(cloudConfiguration()->clientId()));
    }
    m_deviceIdLoaded = true;
    return m_deviceId;
}

QString DbScoreAuthorizationService::defaultDeviceName() const
{
    const QString name = QSysInfo::machineHostName();
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
    const QString version = QString::fromStdString(application()->version().toStdString());
    return version.isEmpty() ? QStringLiteral("5.0.1") : version;
}

QString DbScoreAuthorizationService::platformCode() const
{
#ifdef Q_OS_MACOS
    return QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("other");
#endif
}

void DbScoreAuthorizationService::scheduleStartupRefresh()
{
    QTimer::singleShot(1500, this, [this]() {
        if (isPhoneLoggedIn()) {
            (void)refresh();
        }
    });
}

QJsonObject DbScoreAuthorizationService::loadPhoneUser() const
{
    const QString json = settings()->value(PHONE_USER_KEY).toQString();
    if (json.isEmpty()) {
        return QJsonObject();
    }
    return jsonObjectFromData(json.toUtf8());
}

void DbScoreAuthorizationService::savePhoneSession()
{
    settings()->setLocalValue(PHONE_TOKEN_KEY, Val(m_phoneToken.toStdString()));
    const QByteArray json = QJsonDocument(m_phoneUser).toJson(QJsonDocument::Compact);
    settings()->setLocalValue(
        PHONE_USER_KEY,
        Val(std::string(json.constData(), json.size())));
    settings()->sync();
}

bool DbScoreAuthorizationService::isTrialActive() const
{
    return membershipKind() == QStringLiteral("trial")
           && dateIsActive(m_phoneUser.value("membershipExpiresAt").toString());
}

IDbScoreAuthorizationService::LicenseState DbScoreAuthorizationService::computeState() const
{
    if (!isPhoneLoggedIn()) {
        return LicenseState::TrialExpired;
    }

    const QString status = m_phoneUser.value("membershipStatus").toString();
    if (status == QStringLiteral("revoked")) {
        return LicenseState::Revoked;
    }
    if (status == QStringLiteral("expired")) {
        return LicenseState::Expired;
    }

    if (isPermanentMember()) {
        return status == QStringLiteral("active") ? LicenseState::Active
                                                  : LicenseState::Expired;
    }

    const QString kind = membershipKind();
    if (kind == QStringLiteral("annual")) {
        return status == QStringLiteral("active")
               && dateIsActive(m_phoneUser.value("membershipExpiresAt").toString())
            ? LicenseState::Active
            : LicenseState::Expired;
    }
    if (kind == QStringLiteral("trial")) {
        return isTrialActive() ? LicenseState::Trial : LicenseState::TrialExpired;
    }

    return LicenseState::TrialExpired;
}

void DbScoreAuthorizationService::notifyStateChanged()
{
    m_stateChanged.notify();
    emit stateChanged();
}
