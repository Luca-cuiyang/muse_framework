/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * DB Score - 账户中心模型（激活码授权）
 */

#ifndef MUSE_CLOUD_DBSCOREACCOUNTMODEL_H
#define MUSE_CLOUD_DBSCOREACCOUNTMODEL_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"

#include "cloud/dbscoreauthorization/idbscoreauthorizationservice.h"

namespace muse::cloud {
class DbScoreAccountModel : public QObject, public async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool hasProAccess READ hasProAccess NOTIFY modelChanged)
    Q_PROPERTY(int licenseState READ licenseState NOTIFY modelChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY modelChanged)
    Q_PROPERTY(QString licenseCode READ licenseCode NOTIFY modelChanged)
    Q_PROPERTY(QString memberType READ memberType NOTIFY modelChanged)
    Q_PROPERTY(QString expireAtText READ expireAtText NOTIFY modelChanged)
    Q_PROPERTY(QString trialHoursText READ trialHoursText NOTIFY modelChanged)
    Q_PROPERTY(int daysUntilExpiry READ daysUntilExpiry NOTIFY modelChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY modelChanged)
    Q_PROPERTY(bool phoneLoggedIn READ phoneLoggedIn NOTIFY modelChanged)
    Q_PROPERTY(QString phoneMasked READ phoneMasked NOTIFY modelChanged)
    Q_PROPERTY(QString phoneNickname READ phoneNickname NOTIFY modelChanged)
    Q_PROPERTY(QString apiBaseUrl READ apiBaseUrl CONSTANT)
    Q_PROPERTY(QString purchasePageUrl READ purchasePageUrl CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString sendCodeMessage READ sendCodeMessage NOTIFY sendCodeMessageChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateInfoChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateInfoChanged)
    Q_PROPERTY(QString updateDownloadUrl READ updateDownloadUrl NOTIFY updateInfoChanged)
    Q_PROPERTY(QString updateNotes READ updateNotes NOTIFY updateInfoChanged)
    Q_PROPERTY(int pointsBalance READ pointsBalance NOTIFY pointsChanged)
    Q_PROPERTY(bool todayCheckedIn READ todayCheckedIn NOTIFY pointsChanged)
    Q_PROPERTY(QString inviteCode READ inviteCode NOTIFY inviteChanged)
    Q_PROPERTY(QString inviteUrl READ inviteUrl NOTIFY inviteChanged)
    Q_PROPERTY(int commissionBalanceCents READ commissionBalanceCents NOTIFY inviteChanged)
    Q_PROPERTY(int totalEarnedCents READ totalEarnedCents NOTIFY inviteChanged)
    Q_PROPERTY(int inviteeCount READ inviteeCount NOTIFY inviteChanged)
    Q_PROPERTY(QVariantList ordersList READ ordersList NOTIFY ordersChanged)
    Q_PROPERTY(QVariantList scoresList READ scoresList NOTIFY scoresChanged)
    Q_PROPERTY(QVariantList projectsList READ projectsList NOTIFY projectsChanged)
    Q_PROPERTY(QVariantList notificationsList READ notificationsList NOTIFY notificationsChanged)

    QML_ELEMENT

    muse::GlobalInject<IDbScoreAuthorizationService> authorization;

public:
    explicit DbScoreAccountModel(QObject* parent = nullptr);

    bool hasProAccess() const;
    int licenseState() const;
    QString statusText() const;
    QString licenseCode() const;
    QString memberType() const;
    QString expireAtText() const;
    QString trialHoursText() const;
    int daysUntilExpiry() const;
    QString deviceId() const;
    bool phoneLoggedIn() const;
    QString phoneMasked() const;
    QString phoneNickname() const;
    QString apiBaseUrl() const;
    QString purchasePageUrl() const;
    bool busy() const;
    QString lastError() const;
    QString sendCodeMessage() const;
    QString currentVersion() const;
    QString latestVersion() const;
    bool updateAvailable() const;
    QString updateDownloadUrl() const;
    QString updateNotes() const;
    int pointsBalance() const;
    bool todayCheckedIn() const;
    QString inviteCode() const;
    QString inviteUrl() const;
    int commissionBalanceCents() const;
    int totalEarnedCents() const;
    int inviteeCount() const;
    QVariantList ordersList() const;
    QVariantList scoresList() const;
    QVariantList projectsList() const;
    QVariantList notificationsList() const;

    Q_INVOKABLE void activate(const QString& code);
    Q_INVOKABLE void sendCode(const QString& phone);
    Q_INVOKABLE void loginByPhone(const QString& phone, const QString& code);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearLicense();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void openPurchasePage();
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE void checkForUpdate();
    Q_INVOKABLE void openUpdateDownload();
    Q_INVOKABLE void loadPoints();
    Q_INVOKABLE void checkIn();
    Q_INVOKABLE void loadInvite();
    Q_INVOKABLE void loadOrders();
    Q_INVOKABLE void loadScores();
    Q_INVOKABLE void submitFeedback(const QString& content, const QString& contact);
    Q_INVOKABLE void loadProjects();
    Q_INVOKABLE void loadNotifications();

signals:
    void modelChanged();
    void busyChanged();
    void lastErrorChanged();
    void sendCodeMessageChanged();
    void updateInfoChanged();
    void pointsChanged();
    void inviteChanged();
    void ordersChanged();
    void scoresChanged();
    void feedbackSent();
    void projectsChanged();
    void notificationsChanged();
    void activated();
    void refreshFinished();

private:
    void setBusy(bool busy);
    void setLastError(const QString& error);
    void setSendCodeMessage(const QString& message);

    bool m_busy = false;
    QString m_lastError;
    QString m_sendCodeMessage;
    QString m_latestVersion;
    QString m_updateDownloadUrl;
    QString m_updateNotes;
    bool m_updateAvailable = false;
    int m_pointsBalance = 0;
    bool m_todayCheckedIn = false;
    QString m_inviteCode;
    QString m_inviteUrl;
    int m_commissionBalanceCents = 0;
    int m_totalEarnedCents = 0;
    int m_inviteeCount = 0;
    QVariantList m_ordersList;
    QVariantList m_scoresList;
    QVariantList m_projectsList;
    QVariantList m_notificationsList;
};
}

#endif // MUSE_CLOUD_DBSCOREACCOUNTMODEL_H
