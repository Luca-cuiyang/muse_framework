/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - 账号模型
 */

#include "dbscoreaccountmodel.h"

#include <QBuffer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QImageReader>
#include <QImageWriter>
#include <QUrl>

#include "log.h"

using namespace muse::cloud;

static QString displayDate(const QString& isoDate)
{
    if (isoDate.isEmpty()) {
        return QStringLiteral("—");
    }

    QDateTime date = QDateTime::fromString(isoDate, Qt::ISODate);
    return date.isValid() ? date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"))
                          : QStringLiteral("—");
}

DbScoreAccountModel::DbScoreAccountModel(QObject* parent)
    : QObject(parent)
{
    m_paymentPollTimer.setInterval(3000);
    m_paymentPollTimer.setSingleShot(false);
    QObject::connect(&m_paymentPollTimer, &QTimer::timeout, this, [this]() {
        if (!authorization()->isPhoneLoggedIn() || m_pendingOrderNumber.isEmpty()) {
            stopPaymentPolling();
            return;
        }

        if (m_paymentPollCount <= 0) {
            setPaymentStatus(QStringLiteral("支付二维码已等待较长时间，请确认支付后点击“检查支付状态”。"));
            stopPaymentPolling();
            return;
        }

        --m_paymentPollCount;
        checkPaymentNow();
    });

    authorization()->stateChanged().onNotify(this, [this]() {
        emit modelChanged();
    });
}

bool DbScoreAccountModel::hasProAccess() const
{
    return authorization()->hasProAccess();
}

bool DbScoreAccountModel::paidMembershipActive() const
{
    const QString kind = authorization()->membershipKind();
    return authorization()->hasProAccess()
           && (kind == QStringLiteral("annual")
               || kind == QStringLiteral("lifetime"));
}

QString DbScoreAccountModel::statusText() const
{
    if (!authorization()->isPhoneLoggedIn()) {
        return QStringLiteral("未登录，登录后赠送3天免费试用");
    }

    switch (authorization()->state()) {
    case IDbScoreAuthorizationService::LicenseState::Active:
        if (authorization()->isPermanentMember()) {
            return QStringLiteral("DB Score 永久会员");
        }
        return QStringLiteral("DB Score 年度会员，到期时间：") + expireAtText();
    case IDbScoreAuthorizationService::LicenseState::Trial:
        return QStringLiteral("3 天试用中，剩余：") + trialHoursText();
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

QString DbScoreAccountModel::shortStatusText() const
{
    if (!authorization()->isPhoneLoggedIn()) {
        return QStringLiteral("点击登录");
    }

    switch (authorization()->state()) {
    case IDbScoreAuthorizationService::LicenseState::Active:
        return authorization()->isPermanentMember() ? QStringLiteral("永久会员")
                                                    : QStringLiteral("年度会员");
    case IDbScoreAuthorizationService::LicenseState::Trial:
        return QStringLiteral("免费试用中");
    case IDbScoreAuthorizationService::LicenseState::TrialExpired:
        return QStringLiteral("试用已结束");
    case IDbScoreAuthorizationService::LicenseState::Expired:
        return QStringLiteral("会员已过期");
    case IDbScoreAuthorizationService::LicenseState::Revoked:
        return QStringLiteral("会员已停用");
    default:
        break;
    }

    return QString();
}

QString DbScoreAccountModel::expireAtText() const
{
    if (authorization()->isPermanentMember()) {
        return QStringLiteral("永久");
    }

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

bool DbScoreAccountModel::phoneLoggedIn() const
{
    return authorization()->isPhoneLoggedIn();
}

QString DbScoreAccountModel::phoneNumber() const
{
    return authorization()->phoneNumber();
}

QString DbScoreAccountModel::phoneMasked() const
{
    return authorization()->phoneMasked();
}

QString DbScoreAccountModel::phoneNickname() const
{
    return authorization()->phoneNickname();
}

QString DbScoreAccountModel::avatarUrl() const
{
    QString url = authorization()->avatarUrl();
    if (url.startsWith(QLatin1Char('/'))) {
        return authorization()->apiBaseUrl() + url;
    }

    const QUrl parsed(url);
    const QString scheme = parsed.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("file") || scheme == QStringLiteral("data")) {
        return url;
    }

    return QString();
}

QString DbScoreAccountModel::avatarLetter() const
{
    const QString avatar = authorization()->avatarUrl();
    if (!avatar.isEmpty() && !avatar.contains(QLatin1Char('/'))
            && !avatar.contains(QLatin1Char('.'))) {
        if (avatar.size() > 0 && avatar.at(0).isHighSurrogate() && avatar.size() >= 2) {
            return avatar.left(2);
        }
        return avatar.left(1);
    }

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

QString DbScoreAccountModel::accountCreatedAtText() const
{
    return displayDate(authorization()->accountCreatedAt());
}

QString DbScoreAccountModel::membershipPlan() const
{
    return authorization()->membershipPlan();
}

QString DbScoreAccountModel::membershipKindText() const
{
    const QString kind = authorization()->membershipKind();
    if (kind == QStringLiteral("trial")) {
        return QStringLiteral("3 天试用");
    }
    if (kind == QStringLiteral("lifetime")) {
        return QStringLiteral("永久会员");
    }
    if (kind == QStringLiteral("annual")) {
        return QStringLiteral("年度会员");
    }
    return QStringLiteral("未开通");
}

QString DbScoreAccountModel::membershipStartedAtText() const
{
    return displayDate(authorization()->membershipStartsAt());
}

QString DbScoreAccountModel::membershipExpiresAtText() const
{
    return authorization()->isPermanentMember() ? QStringLiteral("永久") : expireAtText();
}

QString DbScoreAccountModel::membershipSourceText() const
{
    const QString source = authorization()->membershipSource();
    if (source == QStringLiteral("admin")) {
        return QStringLiteral("后台开通");
    }
    if (source == QStringLiteral("purchase")) {
        return QStringLiteral("用户付款开通");
    }
    if (source == QStringLiteral("migration")) {
        return QStringLiteral("历史数据迁移");
    }
    return QStringLiteral("—");
}

QString DbScoreAccountModel::renewalPriceText() const
{
    const int priceFen = authorization()->renewalPriceFen();
    return priceFen > 0 ? QStringLiteral("¥%1/年").arg(priceFen / 100)
                        : QStringLiteral("价格加载中");
}

QString DbScoreAccountModel::renewalPlanName() const
{
    return authorization()->renewalPlanName();
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
        const QString debugCode = authorization()->lastOtpDebugCode();
        setSendCodeMessage(
            debugCode.isEmpty()
                ? QStringLiteral("验证码已发送，请注意查收短信")
                : QStringLiteral("开发环境验证码：") + debugCode);
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

        setLastError(QString());
        setSendCodeMessage(QString());
        emit modelChanged();
    });
}

void DbScoreAccountModel::updateAvatar(const QString& fileUrl)
{
    if (m_busy) {
        return;
    }

    QUrl url(fileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : fileUrl;
    const QFileInfo fileInfo(path);
    constexpr qint64 MAX_SOURCE_BYTES = 30 * 1024 * 1024;
    if (!fileInfo.isFile() || fileInfo.size() <= 0 || fileInfo.size() > MAX_SOURCE_BYTES) {
        setLastError(QStringLiteral("头像图片无效或超过 30MB"));
        return;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()
            && qint64(sourceSize.width()) * sourceSize.height() > 100 * 1000 * 1000) {
        setLastError(QStringLiteral("头像图片尺寸过大"));
        return;
    }

    QImage image = reader.read();
    if (image.isNull()) {
        setLastError(QStringLiteral("无法读取头像图片，请使用 JPG、PNG 或 WebP 格式"));
        return;
    }

    constexpr int MAX_AVATAR_DIMENSION = 512;
    if (image.width() > MAX_AVATAR_DIMENSION || image.height() > MAX_AVATAR_DIMENSION) {
        image = image.scaled(
            MAX_AVATAR_DIMENSION,
            MAX_AVATAR_DIMENSION,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }

    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly)) {
        setLastError(QStringLiteral("头像图片处理失败"));
        return;
    }

    const bool keepTransparency = image.hasAlphaChannel();
    QImageWriter writer(&buffer, keepTransparency ? "PNG" : "JPEG");
    if (!keepTransparency) {
        writer.setQuality(88);
    }
    if (!writer.write(image)) {
        setLastError(QStringLiteral("头像图片编码失败"));
        return;
    }
    buffer.close();

    QVariantMap data;
    data["avatarDataUrl"] = QStringLiteral("data:image/")
                            + (keepTransparency ? QStringLiteral("png") : QStringLiteral("jpeg"))
                            + QStringLiteral(";base64,")
                            + QString::fromLatin1(encoded.toBase64());

    setBusy(true);
    setLastError(QString());
    authorization()->updateProfile(data).onResolve(this, [this](const muse::Ret& ret) {
        setBusy(false);
        if (!ret) {
            setLastError(QString::fromStdString(ret.text()));
            return;
        }
        emit modelChanged();
    });
}

void DbScoreAccountModel::updateDisplayName(const QString& displayName)
{
    const QString normalized = displayName.trimmed();
    if (m_busy || normalized.isEmpty()) {
        return;
    }

    QVariantMap data;
    data["displayName"] = normalized;
    setBusy(true);
    setLastError(QString());
    authorization()->updateProfile(data).onResolve(this, [this](const muse::Ret& ret) {
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

void DbScoreAccountModel::logout()
{
    authorization()->logout();
    setLastError(QString());
    setSendCodeMessage(QString());
    m_ordersList.clear();
    closePaymentDialog();
    emit ordersChanged();
    emit modelChanged();
}

void DbScoreAccountModel::openPurchasePage()
{
    if (m_paymentCreating || !authorization()->isPhoneLoggedIn()) {
        return;
    }

    const quint64 requestGeneration = ++m_paymentRequestGeneration;
    m_paymentCreating = true;
    m_paymentVisible = true;
    m_paymentChecking = false;
    m_pendingOrderNumber.clear();
    m_paymentQrSource.clear();
    m_paymentPlanName = authorization()->renewalPlanName();
    if (m_paymentPlanName.isEmpty()) {
        m_paymentPlanName = QStringLiteral("DB Score 年度会员");
    }
    const int previewPriceFen = authorization()->renewalPriceFen();
    m_paymentAmountText = previewPriceFen > 0
                          ? QStringLiteral("¥%1").arg(previewPriceFen / 100.0, 0, 'f', 2)
                          : QString();
    m_paymentStatus = QStringLiteral("正在生成微信支付二维码…");
    emit paymentChanged();

    setLastError(QString());
    QVariantMap body;
    body["productCode"] = QStringLiteral("db-score");
    authorization()->accountPost("/api/customer/orders", body)
    .onResolve(this, [this, requestGeneration](const muse::RetVal<QVariantMap>& result) {
        if (requestGeneration != m_paymentRequestGeneration) {
            return;
        }
        m_paymentCreating = false;
        if (!result.ret) {
            setLastError(QString::fromStdString(result.ret.text()));
            m_paymentStatus = QStringLiteral("微信支付二维码生成失败，请关闭后重试");
            emit paymentChanged();
            return;
        }

        const QVariantMap order = result.val.value("order").toMap();
        m_pendingOrderNumber = order.value("orderNumber").toString();
        m_paymentQrSource = order.value("qrDataUrl").toString();
        m_paymentPlanName = order.value("plan").toString();
        const int amountFen = order.value("amountFen").toInt();
        m_paymentAmountText = QStringLiteral("¥%1").arg(amountFen / 100.0, 0, 'f', 2);
        m_paymentStatus = QStringLiteral("请使用微信扫描二维码支付");
        m_paymentVisible = !m_paymentQrSource.isEmpty() && !m_pendingOrderNumber.isEmpty();
        if (!m_paymentVisible) {
            m_paymentStatus = QStringLiteral("微信支付二维码生成失败，请关闭后重试");
            setLastError(m_paymentStatus);
        }
        emit paymentChanged();

        if (!m_paymentVisible) {
            return;
        }

        startPaymentPolling();
    });
}

void DbScoreAccountModel::startPaymentPolling()
{
    m_paymentPollCount = 40;
    m_paymentPollTimer.start();
}

void DbScoreAccountModel::stopPaymentPolling()
{
    m_paymentPollTimer.stop();
    m_paymentPollCount = 0;
}

void DbScoreAccountModel::checkPaymentNow()
{
    if (m_pendingOrderNumber.isEmpty() || m_paymentChecking || !authorization()->isPhoneLoggedIn()) {
        return;
    }

    m_paymentChecking = true;
    emit paymentChanged();
    authorization()->accountGet(
        QStringLiteral("/api/customer/orders/") + m_pendingOrderNumber)
    .onResolve(this, [this](const muse::RetVal<QVariantMap>& result) {
        m_paymentChecking = false;
        emit paymentChanged();
        if (!result.ret) {
            setPaymentStatus(QStringLiteral("支付状态查询失败，请稍后重试"));
            return;
        }

        const QString status = result.val.value("status").toString();
        if (status == QStringLiteral("paid")) {
            setPaymentStatus(QStringLiteral("支付成功，会员已开通"));
            stopPaymentPolling();
            authorization()->refresh().onResolve(this, [this](const muse::Ret& ret) {
                if (ret) {
                    emit modelChanged();
                }
            });
        } else if (status == QStringLiteral("cancelled")
                   || status == QStringLiteral("expired")) {
            setPaymentStatus(QStringLiteral("订单已关闭，请重新发起支付"));
            stopPaymentPolling();
        }
    });
}

void DbScoreAccountModel::closePaymentDialog()
{
    ++m_paymentRequestGeneration;
    stopPaymentPolling();
    m_pendingOrderNumber.clear();
    m_paymentQrSource.clear();
    m_paymentPlanName.clear();
    m_paymentAmountText.clear();
    m_paymentStatus.clear();
    m_paymentVisible = false;
    m_paymentChecking = false;
    m_paymentCreating = false;
    emit paymentChanged();
}

bool DbScoreAccountModel::paymentVisible() const
{
    return m_paymentVisible;
}

QString DbScoreAccountModel::paymentQrSource() const
{
    return m_paymentQrSource;
}

QString DbScoreAccountModel::paymentPlanName() const
{
    return m_paymentPlanName;
}

QString DbScoreAccountModel::paymentAmountText() const
{
    return m_paymentAmountText;
}

QString DbScoreAccountModel::paymentOrderNumber() const
{
    return m_pendingOrderNumber;
}

QString DbScoreAccountModel::paymentStatusText() const
{
    return m_paymentStatus;
}

bool DbScoreAccountModel::paymentChecking() const
{
    return m_paymentChecking;
}

bool DbScoreAccountModel::paymentCreating() const
{
    return m_paymentCreating;
}

void DbScoreAccountModel::setPaymentStatus(const QString& status)
{
    if (m_paymentStatus == status) {
        return;
    }
    m_paymentStatus = status;
    emit paymentChanged();
}

void DbScoreAccountModel::copyText(const QString& text)
{
    if (text.isEmpty()) {
        return;
    }

    QGuiApplication::clipboard()->setText(text);
}

void DbScoreAccountModel::clearTransientMessages()
{
    setLastError(QString());
    setSendCodeMessage(QString());
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

void DbScoreAccountModel::loadOrders()
{
    if (m_busy) {
        return;
    }

    setBusy(true);
    setLastError(QString());

    authorization()->accountGet("/api/customer/orders?productCode=db-score").onResolve(this, [this](const muse::RetVal<QVariantMap>& result) {
        setBusy(false);
        if (!result.ret) {
            setLastError(QString::fromStdString(result.ret.text()));
            return;
        }

        QVariantMap data = dataMap(result.val);
        m_ordersList.clear();
        for (const QVariant& value : data.value("orders").toList()) {
            const QVariantMap order = value.toMap();
            if (order.value("product").toString().trimmed() == QStringLiteral("DB Score")) {
                m_ordersList.append(order);
            }
        }
        emit ordersChanged();
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

