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
#include <QNetworkProxyFactory>
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
static const Settings::Key LAST_SYNC_MS_KEY(dbscore_authorization_module_name, "dbscoreAuthorization/lastSyncMs");
static const Settings::Key PHONE_TOKEN_KEY(dbscore_authorization_module_name, "dbscoreAuthorization/phoneToken");
static const Settings::Key PHONE_USER_KEY(dbscore_authorization_module_name, "dbscoreAuthorization/phoneUser");

static const QString API_BASE_URL("https://drumbearai.com");
static const QString ACTIVATE_URL(API_BASE_URL + "/api/dbscore/activate");
static const QString LICENSE_URL(API_BASE_URL + "/api/dbscore/license");
static const QString HEARTBEAT_URL(API_BASE_URL + "/api/dbscore/heartbeat");
static const QString PURCHASE_PAGE_URL(API_BASE_URL + "/membership?plan=yearly");

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
    m_heartbeatTimer.setInterval(24 * 60 * 60 * 1000); // 每 24 小时心跳一次
    m_heartbeatTimer.setSingleShot(false);

    m_trialWatchTimer.setInterval(5 * 60 * 1000); // 每 5 分钟检查一次试用是否到期
    m_trialWatchTimer.setSingleShot(false);
}

void DbScoreAuthorizationService::init()
{
    // 系统可能残留已关闭的 VPN 代理（如 127.0.0.1:1082），
    // 会导致授权请求一直挂起超时。这里统一走直连，由系统路由（TUN）转发。
    QNetworkProxyFactory::setUseSystemConfiguration(false);

    settings()->setDefaultValue(TRIAL_START_MS_KEY, Val(int64_t(0)));
    settings()->setDefaultValue(LICENSE_CACHE_KEY, Val(std::string()));
    settings()->setDefaultValue(LAST_SYNC_MS_KEY, Val(int64_t(0)));
    settings()->setDefaultValue(PHONE_TOKEN_KEY, Val(std::string()));
    settings()->setDefaultValue(PHONE_USER_KEY, Val(std::string()));

    (void)machineDeviceId();
    applyOfflineGraceWindow();
    m_phoneToken = settings()->value(PHONE_TOKEN_KEY).toQString();
    m_phoneUser = loadPhoneUser();
    m_phoneLoggedIn = !m_phoneToken.isEmpty();
    notifyStateChanged();

    scheduleStartupRefresh();
    scheduleHeartbeat();
    scheduleTrialWatch();
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
    if (!isPhoneLoggedIn()) {
        return QString();
    }
    return loadLicense().value("code").toString();
}

QString DbScoreAuthorizationService::memberType() const
{
    if (!isPhoneLoggedIn()) {
        return QString();
    }

    QString mt = m_phoneUser.value("memberType").toString();
    if (!mt.isEmpty() && mt != "free") {
        return mt;
    }

    // 免费账号但激活码生效中：视为正式会员（等 /api/user 同步）
    if (isLicenseActive()) {
        return loadLicense().value("memberType").toString();
    }
    return QStringLiteral("free");
}

QDateTime DbScoreAuthorizationService::expireAt() const
{
    if (!isPhoneLoggedIn()) {
        return QDateTime();
    }

    QString mt = m_phoneUser.value("memberType").toString();
    if (!mt.isEmpty() && mt != "free") {
        QString value = m_phoneUser.value("expireAt").toString();
        if (value.isEmpty()) {
            value = m_phoneUser.value("memberExpireAt").toString();
        }
        if (value.isEmpty()) {
            return QDateTime();
        }
        return QDateTime::fromString(value, Qt::ISODate);
    }

    // 免费账号但激活码生效中：返回激活码到期时间
    if (isLicenseActive()) {
        QString value = loadLicense().value("expireAt").toString();
        if (!value.isEmpty()) {
            return QDateTime::fromString(value, Qt::ISODate);
        }
    }

    return QDateTime();
}

QString DbScoreAuthorizationService::deviceId() const
{
    return machineDeviceId();
}

double DbScoreAuthorizationService::trialHoursRemaining() const
{
    // 登录后优先用服务端试用计时
    if (isPhoneLoggedIn()) {
        QString endAt = m_phoneUser.value("trialEndAt").toString();
        if (!endAt.isEmpty()) {
            QDateTime end = QDateTime::fromString(endAt, Qt::ISODate);
            if (end.isValid()) {
                qint64 remainingMs = end.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch();
                if (remainingMs <= 0) {
                    return 0.0;
                }
                return double(remainingMs) / (60.0 * 60.0 * 1000.0);
            }
        }

        double daysLeft = m_phoneUser.value("trialDaysLeft").toDouble();
        if (daysLeft > 0) {
            return daysLeft * 24.0;
        }
        return 0.0;
    }

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
    if (isPhoneLoggedIn()) {
        return m_phoneUser.value("trialActive").toBool();
    }
    return trialHoursRemaining() > 0.0;
}

bool DbScoreAuthorizationService::isPhoneLoggedIn() const
{
    return m_phoneLoggedIn && !m_phoneToken.isEmpty();
}

QString DbScoreAuthorizationService::phoneMasked() const
{
    return m_phoneUser.value("phone").toString();
}

QString DbScoreAuthorizationService::phoneNickname() const
{
    return m_phoneUser.value("nickname").toString();
}

QString DbScoreAuthorizationService::avatarUrl() const
{
    QString url = m_phoneUser.value("avatarUrl").toString();
    if (url.isEmpty()) {
        url = m_phoneUser.value("avatar").toString();
    }
    return url;
}

QString DbScoreAuthorizationService::apiBaseUrl() const
{
    return API_BASE_URL;
}

QString DbScoreAuthorizationService::purchasePageUrl() const
{
    return PURCHASE_PAGE_URL;
}

QString DbScoreAuthorizationService::currentVersion() const
{
    return appVersion();
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

        // 带登录态，让后端把激活码绑定到当前账号
        doRequest(QUrl(ACTIVATE_URL), body, authHeaders()).onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                resolve(result.ret);
                return;
            }

            QJsonObject license = result.val.value("data").toObject().value("license").toObject();
            if (!license.isEmpty()) {
                saveLicense(license);
                notifyStateChanged();
            }

            settings()->setLocalValue(LAST_SYNC_MS_KEY, Val(QDateTime::currentMSecsSinceEpoch()));
            // 激活后刷新账号会员状态（后端将激活码绑定到账号后，/api/user 会返回 standard）
            refreshPhoneUser().onResolve(this, [this, resolve](const Ret&) {
                notifyStateChanged();
                resolve(make_ok());
            });
        });

        return Promise<Ret>::dummy_result();
    });
}

async::Promise<Ret> DbScoreAuthorizationService::sendCode(const QString& phone)
{
    return make_promise<Ret>([this, phone](auto resolve, auto /*reject*/) {
        LOGD() << "DbScoreAuthorizationService::sendCode phone=" << phone;

        QJsonObject body;
        body["phone"] = phone;

        doRequest(QUrl(API_BASE_URL + "/api/auth/send-code"), body).onResolve(this, [resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                LOGE() << "sendCode failed: " << result.ret.text();
                resolve(result.ret);
                return;
            }
            LOGD() << "sendCode ok";
            resolve(make_ok());
        });

        return Promise<Ret>::dummy_result();
    });
}

async::Promise<Ret> DbScoreAuthorizationService::loginByPhone(const QString& phone, const QString& code)
{
    return make_promise<Ret>([this, phone, code](auto resolve, auto /*reject*/) {
        QJsonObject body;
        body["phone"] = phone;
        body["code"] = code;

        doRequest(QUrl(API_BASE_URL + "/api/auth/login"), body).onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                resolve(result.ret);
                return;
            }

            m_phoneToken = result.val.value("token").toString();
            if (m_phoneToken.isEmpty()) {
                resolve(make_ret(Ret::Code::InternalError, std::string("登录返回异常")));
                return;
            }

            m_phoneUser = result.val.value("user").toObject();
            m_phoneLoggedIn = true;
            savePhoneSession();
            setTrialStartNowIfNeeded();

            LOGI() << "loginByPhone: token=" << m_phoneToken.left(8)
                   << " loginUserHasMemberType=" << m_phoneUser.contains("memberType")
                   << " loginUserHasTrial=" << m_phoneUser.contains("trialActive");

            refreshPhoneUser().onResolve(this, [this, resolve](const Ret&) {
                notifyStateChanged();
                // 只要拿到 token 即视为登录成功，用户信息拉取失败不影响登录
                resolve(make_ok());
            });
        });

        return Promise<Ret>::dummy_result();
    });
}

async::Promise<Ret> DbScoreAuthorizationService::registerByPhone(const QString& phone, const QString& code, const QString& nickname)
{
    return make_promise<Ret>([this, phone, code, nickname](auto resolve, auto /*reject*/) {
        QJsonObject body;
        body["phone"] = phone;
        body["code"] = code;
        if (!nickname.isEmpty()) {
            body["nickname"] = nickname;
        }

        doRequest(QUrl(API_BASE_URL + "/api/auth/register"), body).onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                resolve(result.ret);
                return;
            }

            m_phoneToken = result.val.value("token").toString();
            if (m_phoneToken.isEmpty()) {
                resolve(make_ret(Ret::Code::InternalError, std::string("注册返回异常")));
                return;
            }

            m_phoneUser = result.val.value("user").toObject();
            m_phoneLoggedIn = true;
            savePhoneSession();
            setTrialStartNowIfNeeded();

            refreshPhoneUser().onResolve(this, [this, resolve](const Ret&) {
                notifyStateChanged();
                resolve(make_ok());
            });
        });

        return Promise<Ret>::dummy_result();
    });
}

async::Promise<Ret> DbScoreAuthorizationService::refresh()
{
    return make_promise<Ret>([this](auto resolve, auto /*reject*/) {
        LOGI() << "refresh() phoneLoggedIn=" << isPhoneLoggedIn();

        QUrl url(LICENSE_URL);
        QUrlQuery query;
        query.addQueryItem("deviceId", machineDeviceId());
        url.setQuery(query);

        if (isPhoneLoggedIn()) {
            // 先带登录态同步激活码（触发后端把历史激活码补绑到账号），再刷新账号会员状态
            doRequest(url, QJsonObject(), authHeaders()).onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
                if (result.ret) {
                    QJsonObject dataObj = result.val.value("data").toObject();
                    if (dataObj.contains("license")) {
                        QJsonObject license = dataObj.value("license").toObject();
                        if (license.isEmpty()) {
                            clearStoredLicense();
                        } else {
                            saveLicense(license);
                        }
                    }
                }

                refreshPhoneUser().onResolve(this, [this, resolve](const Ret& ret) {
                    notifyStateChanged();
                    resolve(ret);
                });
            });
            return Promise<Ret>::dummy_result();
        }

        doRequest(url, QJsonObject()).onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                resolve(result.ret);
                return;
            }

            QJsonObject dataObj = result.val.value("data").toObject();
            if (dataObj.contains("license")) {
                QJsonObject license = dataObj.value("license").toObject();
                if (license.isEmpty()) {
                    // 服务端明确表示无授权（可能被解绑/停用/删除）
                    clearStoredLicense();
                } else {
                    saveLicense(license);
                }
                notifyStateChanged();
            }

            settings()->setLocalValue(LAST_SYNC_MS_KEY, Val(QDateTime::currentMSecsSinceEpoch()));
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

async::Promise<RetVal<QVariantMap> > DbScoreAuthorizationService::checkForUpdate()
{
    return make_promise<RetVal<QVariantMap> >([this](auto resolve, auto /*reject*/) {
        doRequest(QUrl(API_BASE_URL + "/api/software"), QJsonObject()).onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                resolve(RetVal<QVariantMap>::make_ret(result.ret));
                return;
            }

            QVariantMap info;
            QString key = updatePlatformKey();
            QJsonObject pkg = result.val.value(key).toObject();
            QString latest = pkg.value("version").toString();
            QString url = pkg.value("url").toString();
            QString notes = pkg.value("notes").toString();

            if (!url.isEmpty() && url.startsWith('/')) {
                url = API_BASE_URL + url;
            }

            info["latestVersion"] = latest;
            info["downloadUrl"] = url;
            info["notes"] = notes;

            bool hasUpdate = false;
            if (!latest.isEmpty()) {
                Version current = application()->version();
                Version remote(latest.toStdString());
                hasUpdate = current < remote;
            }
            info["hasUpdate"] = hasUpdate;

            resolve(RetVal<QVariantMap>::make_ok(info));
        });

        return Promise<RetVal<QVariantMap> >::dummy_result();
    });
}

async::Promise<RetVal<QVariantMap> > DbScoreAuthorizationService::apiGet(const QString& path)
{
    return make_promise<RetVal<QVariantMap> >([this, path](auto resolve, auto /*reject*/) {
        doRequest(QUrl(API_BASE_URL + path), QJsonObject(), authHeaders()).onResolve(this, [resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                resolve(RetVal<QVariantMap>::make_ret(result.ret));
                return;
            }
            resolve(RetVal<QVariantMap>::make_ok(result.val.toVariantMap()));
        });
        return Promise<RetVal<QVariantMap> >::dummy_result();
    });
}

async::Promise<RetVal<QVariantMap> > DbScoreAuthorizationService::apiPost(const QString& path, const QVariantMap& body)
{
    return make_promise<RetVal<QVariantMap> >([this, path, body](auto resolve, auto /*reject*/) {
        QJsonObject json = QJsonObject::fromVariantMap(body);
        doRequest(QUrl(API_BASE_URL + path), json, authHeaders()).onResolve(this, [resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                resolve(RetVal<QVariantMap>::make_ret(result.ret));
                return;
            }
            resolve(RetVal<QVariantMap>::make_ok(result.val.toVariantMap()));
        });
        return Promise<RetVal<QVariantMap> >::dummy_result();
    });
}

void DbScoreAuthorizationService::logout()
{
    m_phoneToken.clear();
    m_phoneUser = QJsonObject();
    m_phoneLoggedIn = false;
    settings()->setLocalValue(PHONE_TOKEN_KEY, Val(std::string()));
    settings()->setLocalValue(PHONE_USER_KEY, Val(std::string()));
    // 退出登录后，激活码会员状态一并清除，避免残留显示
    clearStoredLicense();
    notifyStateChanged();
}

QString DbScoreAuthorizationService::authTokenHeader() const
{
    if (m_phoneToken.isEmpty()) {
        return QString();
    }
    return QStringLiteral("Bearer ") + m_phoneToken;
}

network::RequestHeaders DbScoreAuthorizationService::authHeaders() const
{
    network::RequestHeaders headers;
    if (!m_phoneToken.isEmpty()) {
        headers.rawHeaders["Authorization"] = authTokenHeader().toUtf8();
    }
    return headers;
}

async::Promise<Ret> DbScoreAuthorizationService::refreshPhoneUser()
{
    return make_promise<Ret>([this](auto resolve, auto /*reject*/) {
        LOGI() << "refreshPhoneUser() called, token=" << m_phoneToken.left(8);

        network::RequestHeaders headers;
        headers.rawHeaders["Authorization"] = authTokenHeader().toUtf8();

        doRequest(QUrl(API_BASE_URL + "/api/user"), QJsonObject(), headers).onResolve(this, [this, resolve](const RetVal<QJsonObject>& result) {
            if (!result.ret) {
                resolve(result.ret);
                return;
            }

            QJsonObject user = result.val.value("user").toObject();
            if (!user.isEmpty()) {
                m_phoneUser = user;
                savePhoneSession();
                notifyStateChanged();
            }

            LOGI() << "refreshPhoneUser: memberType=" << m_phoneUser.value("memberType").toString()
                   << " trialActive=" << m_phoneUser.value("trialActive").toBool()
                   << " trialDaysLeft=" << m_phoneUser.value("trialDaysLeft").toInt();

            resolve(make_ok());
        });

        return Promise<Ret>::dummy_result();
    });
}

async::Notification DbScoreAuthorizationService::stateChanged() const
{
    return m_stateChanged;
}

async::Promise<RetVal<QJsonObject> > DbScoreAuthorizationService::doRequest(const QUrl& url, const QJsonObject& body,
                                                                             const network::RequestHeaders& extraHeaders)
{
    return make_promise<RetVal<QJsonObject> >([this, url, body, extraHeaders](auto resolve, auto /*reject*/) {
        LOGI() << "doRequest url=" << url.toString();

        auto received = std::make_shared<QBuffer>();
        auto timedOut = std::make_shared<bool>(false);

        // 防止请求挂起导致 UI 一直 busy
        QTimer* timeoutTimer = new QTimer(this);
        timeoutTimer->setSingleShot(true);
        QObject::connect(timeoutTimer, &QTimer::timeout, this, [resolve, timedOut]() {
            if (*timedOut) {
                return;
            }
            *timedOut = true;
            resolve(RetVal<QJsonObject>::make_ret(Ret::Code::InternalError, std::string("请求超时，请检查网络后重试")));
        });
        timeoutTimer->start(15000);

        RequestHeaders headers;
        headers.rawHeaders["Content-Type"] = "application/json";
        headers.rawHeaders["Accept"] = "application/json";
        headers.rawHeaders["User-Agent"] = QString("DBScore/" + appVersion()).toUtf8();
        for (auto it = extraHeaders.rawHeaders.constBegin(); it != extraHeaders.rawHeaders.constEnd(); ++it) {
            headers.rawHeaders[it.key()] = it.value();
        }

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
            timeoutTimer->stop();
            timeoutTimer->deleteLater();
            resolve(RetVal<QJsonObject>::make_ret(progress.ret));
            return Promise<RetVal<QJsonObject> >::dummy_result();
        }

        progress.val.finished().onReceive(this, [this, url, received, resolve, timeoutTimer, timedOut](const ProgressResult& res) {
            if (*timedOut) {
                return;
            }
            *timedOut = true;
            timeoutTimer->stop();
            timeoutTimer->deleteLater();

            LOGI() << "doRequest finished url=" << url.toString() << " ret=" << res.ret.success();

            if (!res.ret) {
                // 后端返回 4xx 时，尝试从响应体里取更友好的错误提示
                QJsonParseError err;
                QJsonObject errObj = jsonObjectFromData(received->data(), &err);
                QString errMsg;
                if (err.error == QJsonParseError::NoError && !errObj.isEmpty()) {
                    errMsg = errObj.value("message").toString();
                    if (errMsg.isEmpty()) {
                        errMsg = errObj.value("error").toString();
                    }
                }
                if (errMsg.isEmpty()) {
                    resolve(RetVal<QJsonObject>::make_ret(res.ret));
                } else {
                    resolve(RetVal<QJsonObject>::make_ret(Ret::Code::InternalError, errMsg.toStdString()));
                }
                return;
            }

            QJsonParseError err;
            QJsonObject obj = jsonObjectFromData(received->data(), &err);
            if (err.error != QJsonParseError::NoError || obj.isEmpty()) {
                resolve(RetVal<QJsonObject>::make_ret(Ret::Code::InternalError, std::string("服务器返回异常")));
                return;
            }

            // 后端各接口返回格式不统一：
            // - 部分接口带 success:true/false（如 /api/dbscore/license、/api/software、/api/dbscore/activate）
            // - /api/user 成功时没有 success 字段，失败时才带 error/code
            // 因此：有 success 字段时以它为准；没有 success 字段时，仅当存在 error/message 才判定失败。
            bool hasSuccessField = obj.contains("success");
            bool success = obj.value("success").toBool(false);
            QString code = obj.value("code").toString();
            QString message = obj.value("message").toString();
            QString error = obj.value("error").toString();

            bool failed = false;
            if (hasSuccessField) {
                failed = !success;
            } else if (!error.isEmpty() || !message.isEmpty()) {
                failed = true;
            }

            if (failed) {
                QString errMsg = message;
                if (errMsg.isEmpty()) {
                    errMsg = error;
                }
                if (errMsg.isEmpty()) {
                    errMsg = code;
                }
                resolve(RetVal<QJsonObject>::make_ret(Ret::Code::InternalError, errMsg.toStdString()));
                return;
            }

            resolve(RetVal<QJsonObject>::make_ok(obj));
        });

        return Promise<RetVal<QJsonObject> >::dummy_result();
    });
}

network::INetworkManagerPtr DbScoreAuthorizationService::networkManager()
{
    // 必须保存为成员：若作为临时对象，请求发出后对象被销毁，网络请求会被中断，
    // 导致 finished 永远不触发、界面一直等待超时。
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

QString DbScoreAuthorizationService::updatePlatformKey() const
{
#ifdef Q_OS_MACOS
    return QStringLiteral("mac");
#elif defined(Q_OS_WIN)
    return QStringLiteral("win");
#else
    return QStringLiteral("linux");
#endif
}

void DbScoreAuthorizationService::setTrialStartNowIfNeeded()
{
    qint64 start = trialStartMs();
    if (start <= 0) {
        settings()->setLocalValue(TRIAL_START_MS_KEY, Val(QDateTime::currentMSecsSinceEpoch()));
    }
}

void DbScoreAuthorizationService::scheduleStartupRefresh()
{
    LOGI() << "scheduleStartupRefresh scheduled";

    // 等主事件循环启动后再发起首次授权同步
    QTimer::singleShot(3000, this, [this]() {
        LOGI() << "scheduleStartupRefresh fired";
        (void)refresh();
    });
}

void DbScoreAuthorizationService::scheduleHeartbeat()
{
    QObject::connect(&m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        (void)refresh();
    });
    m_heartbeatTimer.start();
}

void DbScoreAuthorizationService::scheduleTrialWatch()
{
    m_trialWasActive = isTrialActive();

    QObject::connect(&m_trialWatchTimer, &QTimer::timeout, this, [this]() {
        bool active = isTrialActive();
        if (m_trialWasActive && !active) {
            // 试用从有效变为过期：立即锁定并通知界面
            notifyStateChanged();
        }
        m_trialWasActive = active;
    });
    m_trialWatchTimer.start();
}

void DbScoreAuthorizationService::applyOfflineGraceWindow()
{
    constexpr qint64 GRACE_MS = 7 * 24 * 60 * 60 * 1000LL;

    qint64 lastSync = settings()->value(LAST_SYNC_MS_KEY).toInt64();
    if (lastSync <= 0) {
        return;
    }

    QJsonObject license = loadLicense();
    if (license.isEmpty()) {
        return;
    }

    if (QDateTime::currentMSecsSinceEpoch() - lastSync > GRACE_MS) {
        // 超过离线宽限期仍未联网校验，暂停会员权益
        clearStoredLicense();
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

QJsonObject DbScoreAuthorizationService::loadPhoneUser() const
{
    QString json = settings()->value(PHONE_USER_KEY).toQString();
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

void DbScoreAuthorizationService::savePhoneSession()
{
    settings()->setLocalValue(PHONE_TOKEN_KEY, Val(m_phoneToken.toStdString()));

    QByteArray json = QJsonDocument(m_phoneUser).toJson(QJsonDocument::Compact);
    settings()->setLocalValue(PHONE_USER_KEY, Val(std::string(json.constData(), json.size())));
}

bool DbScoreAuthorizationService::isLicenseActive() const
{
    QJsonObject license = loadLicense();
    if (license.isEmpty() || license.value("status").toString() != "active") {
        return false;
    }

    // 本产品为年度会员（365 天），不存在永久授权；没有到期时间不视为有效会员
    QString expire = license.value("expireAt").toString();
    if (expire.isEmpty()) {
        return false;
    }

    QDateTime expireAt = QDateTime::fromString(expire, Qt::ISODate);
    return expireAt.isValid() && expireAt > QDateTime::currentDateTimeUtc();
}

IDbScoreAuthorizationService::LicenseState DbScoreAuthorizationService::computeState() const
{
    // 未登录：不判定会员，界面按“未登录”处理
    if (!isPhoneLoggedIn()) {
        return LicenseState::TrialExpired;
    }

    // 已登录：账号为正式会员
    QString memberType = m_phoneUser.value("memberType").toString();
    if (!memberType.isEmpty() && memberType != "free") {
        QString expire = m_phoneUser.value("expireAt").toString();
        if (expire.isEmpty()) {
            expire = m_phoneUser.value("memberExpireAt").toString();
        }
        if (expire.isEmpty()) {
            // 年卡必须有到期时间，没有则视为过期
            return LicenseState::Expired;
        }
        QDateTime expireAt = QDateTime::fromString(expire, Qt::ISODate);
        if (expireAt.isValid() && expireAt > QDateTime::currentDateTimeUtc()) {
            return LicenseState::Active;
        }
        return LicenseState::Expired;
    }

    // 已登录免费账号：激活码刚激活但 /api/user 尚未同步时，用本地 license 兜底
    if (isLicenseActive()) {
        return LicenseState::Active;
    }

    // 免费账号：3 天试用
    return isTrialActive() ? LicenseState::Trial : LicenseState::TrialExpired;
}

void DbScoreAuthorizationService::notifyStateChanged()
{
    m_stateChanged.notify();
    emit stateChanged();
}
