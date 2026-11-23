/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
import QtQuick

import Muse.Ui
import Muse.UiComponents

// Keep the DB绘谱 sizing while rendering the original icon-font glyphs.
// Custom Canvas substitutes changed established Play/Pause, Metronome,
// Percussion and Audio icons.
Item {
    id: root

    property int iconCode: IconCode.NONE
    property font font: ui.theme.iconsFont
    property color color: ui.theme.fontPrimaryColor

    readonly property bool isEmpty: iconCode === IconCode.NONE

    implicitWidth: iconLabel.implicitWidth
    implicitHeight: iconLabel.implicitHeight

    StyledIconLabel {
        id: iconLabel

        anchors.fill: parent
        visible: !root.isEmpty
        iconCode: root.iconCode
        font: root.font
        color: root.color
    }
}
