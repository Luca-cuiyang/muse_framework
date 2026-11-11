/*
 * SPDX-License-Identifier: GPL-3.0-only
 * DB Score - 微信支付二维码
 */
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

Dialog {
    id: root

    required property string qrSource
    required property string planName
    required property string amountText
    required property string orderNumber
    required property string statusText
    required property bool checking
    required property bool creating

    signal checkRequested()

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: 420
    height: 620
    modal: true
    padding: 0
    closePolicy: Popup.NoAutoClose

    background: Rectangle {
        radius: 20
        color: ui.theme.backgroundPrimaryColor
        border.width: 1
        border.color: ui.theme.strokeColor
    }

    contentItem: ColumnLayout {
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.bottomMargin: 0

            StyledTextLabel {
                Layout.fillWidth: true
                text: qsTrc("appshell", "微信扫码开通会员")
                font: ui.theme.titleBoldFont
            }

            FlatButton {
                icon: IconCode.CLOSE_X_ROUNDED
                transparent: true
                toolTipTitle: qsTrc("appshell", "关闭")
                onClicked: root.close()
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            text: root.planName + "  " + root.amountText
            font: ui.theme.largeBodyBoldFont
            horizontalAlignment: Text.AlignHCenter
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 320
            Layout.preferredHeight: 320
            radius: 16
            color: "#FFFFFF"
            border.width: 1
            border.color: ui.theme.strokeColor

            Image {
                anchors.fill: parent
                anchors.margins: 16
                source: root.qrSource
                visible: !root.creating && root.qrSource.length > 0
                fillMode: Image.PreserveAspectFit
                asynchronous: false
                cache: false
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: root.creating || root.qrSource.length === 0
                spacing: 10

                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    running: root.creating
                    visible: root.creating
                }

                StyledTextLabel {
                    text: root.creating
                          ? qsTrc("appshell", "正在生成支付码…")
                          : qsTrc("appshell", "支付码暂不可用")
                    font: ui.theme.bodyFont
                    color: ui.theme.fontPrimaryColor
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            text: root.statusText
            color: root.statusText.indexOf(qsTrc("appshell", "支付成功")) >= 0
                   ? "#2ECC71"
                   : ui.theme.fontPrimaryColor
            font: ui.theme.bodyFont
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        StyledTextLabel {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            text: qsTrc("appshell", "订单号：%1").arg(root.orderNumber)
            visible: root.orderNumber.length > 0
            font: ui.theme.bodyFont
            opacity: 0.55
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.topMargin: 0
            spacing: 12

            FlatButton {
                Layout.fillWidth: true
                text: root.checking
                      ? qsTrc("appshell", "正在检查…")
                      : root.creating
                        ? qsTrc("appshell", "正在生成支付码…")
                      : qsTrc("appshell", "检查支付状态")
                enabled: !root.checking && !root.creating && root.orderNumber.length > 0
                onClicked: root.checkRequested()
            }

            FlatButton {
                Layout.fillWidth: true
                text: qsTrc("appshell", "关闭")
                accentButton: true
                onClicked: root.close()
            }
        }
    }
}
