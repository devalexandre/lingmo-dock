import QtQuick 2.12
import QtQuick.Window 2.3
import QtQuick.Controls 2.12
import Lingmo.Dock 1.0
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Thumbnails of an application's windows, shown above (or beside) its dock icon
Window {
    id: control
    visible: false
    color: "transparent"
    flags: Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.ToolTip

    // The dock item the popup points at
    property Item target: null
    property string iconName: ""

    // Where the dock is, to keep the popup on its screen
    property rect screenRect: Qt.rect(0, 0, Screen.width, Screen.height)

    readonly property bool horizontal: Settings.direction === DockSettings.Bottom
    readonly property int count: windowPreviews.count
    readonly property int padding: LingmoUI.Units.smallSpacing
    readonly property int cardSpacing: LingmoUI.Units.smallSpacing / 2
    readonly property int cardPadding: LingmoUI.Units.smallSpacing
    readonly property int titleHeight: fontMetrics.height + LingmoUI.Units.smallSpacing

    // Thumbnails shrink when the windows don't fit the screen side by side
    readonly property int thumbWidth: {
        var n = Math.max(control.count, 1)
        var margins = control.padding * 2 + LingmoUI.Units.largeSpacing * 2
        if (control.horizontal) {
            var perCard = (control.screenRect.width - margins - (n - 1) * control.cardSpacing) / n
            return Math.max(80, Math.min(200, Math.floor(perCard - control.cardPadding * 2)))
        }

        var perCardHeight = (control.screenRect.height - margins - (n - 1) * control.cardSpacing) / n
        var thumbHeight = perCardHeight - control.cardPadding * 2 - control.titleHeight
        return Math.max(80, Math.min(200, Math.floor(thumbHeight / 0.6)))
    }
    readonly property int thumbHeight: Math.round(thumbWidth * 0.6)
    readonly property int cardWidth: thumbWidth + cardPadding * 2
    readonly property int cardHeight: thumbHeight + titleHeight + cardPadding * 2

    // The layout goes by these rather than by width and height: a window resized
    // while hidden keeps reporting (and laying out its content at) its old size
    // until it shows, and the popup gets its size from the model before that
    readonly property int popupWidth: horizontal ? count * cardWidth + Math.max(count - 1, 0) * cardSpacing + padding * 2
                                                 : cardWidth + padding * 2
    readonly property int popupHeight: horizontal ? cardHeight + padding * 2
                                                  : count * cardHeight + Math.max(count - 1, 0) * cardSpacing + padding * 2

    width: popupWidth
    height: popupHeight

    onPopupWidthChanged: updatePosition()
    onPopupHeightChanged: updatePosition()
    onCountChanged: if (control.visible && control.count === 0) control.hideNow()

    onVisibleChanged: {
        windowPreviews.capturing = control.visible
        mainWindow.setPreviewVisible(control.visible)
    }

    Binding {
        target: windowPreviews
        property: "thumbnailSize"
        value: Qt.size(control.thumbWidth * LingmoUI.Units.devicePixelRatio,
                       control.thumbHeight * LingmoUI.Units.devicePixelRatio)
    }

    FontMetrics {
        id: fontMetrics
    }

    Timer {
        id: showTimer
        interval: 400
        property Item pendingTarget: null
        property string pendingAppId: ""
        property string pendingIconName: ""
        onTriggered: control.open(pendingTarget, pendingAppId, pendingIconName)
    }

    Timer {
        id: hideTimer
        interval: 300
        onTriggered: {
            // Back on the popup or on its icon meanwhile
            var onTarget = control.target && control.target.mouseArea.containsMouse
            if (!popupHover.hovered && !onTarget)
                control.hideNow()
        }
    }

    // The pointer is on a dock icon with windows
    function requestShow(item, appId, iconName) {
        hideTimer.stop()

        // Already open: follow the pointer to the new icon right away
        if (control.visible) {
            control.open(item, appId, iconName)
            return
        }

        showTimer.pendingTarget = item
        showTimer.pendingAppId = appId
        showTimer.pendingIconName = iconName
        showTimer.restart()
    }

    // The pointer left the icon (or the popup): give it time to reach the popup.
    // The icon the pointer moves to may have been entered first, so a leave only
    // counts from the icon the popup belongs to.
    function requestHide(item) {
        if (item && item !== control.target && item !== showTimer.pendingTarget)
            return

        showTimer.stop()
        showTimer.pendingTarget = null

        if (control.visible)
            hideTimer.restart()
    }

    function hideNow() {
        showTimer.stop()
        showTimer.pendingTarget = null
        hideTimer.stop()
        control.visible = false
        control.target = null
        // The windows stay in the model: back on the same icon, the last
        // pictures show right away while new ones are taken
    }

    function open(item, appId, iconName) {
        if (!item)
            return

        var switching = control.visible && windowPreviews.appId !== appId

        control.target = item
        control.iconName = iconName
        windowPreviews.appId = appId

        if (control.count === 0) {
            control.hideNow()
            return
        }

        updatePosition()

        if (switching)
            switchAnimation.restart()

        control.visible = true
    }

    function updatePosition() {
        if (!control.target)
            return

        var pos = control.target.mapToGlobal(0, 0)
        var posX, posY

        if (Settings.direction === DockSettings.Left) {
            posX = mainWindow.x + mainWindow.width + LingmoUI.Units.largeSpacing
            posY = pos.y + control.target.height / 2 - control.popupHeight / 2
        } else if (Settings.direction === DockSettings.Right) {
            posX = mainWindow.x - control.popupWidth - LingmoUI.Units.largeSpacing
            posY = pos.y + control.target.height / 2 - control.popupHeight / 2
        } else {
            posX = pos.x + control.target.width / 2 - control.popupWidth / 2
            posY = mainWindow.y - control.popupHeight - LingmoUI.Units.smallSpacing
        }

        var rect = control.screenRect
        posX = Math.max(rect.x + LingmoUI.Units.smallSpacing,
                        Math.min(posX, rect.x + rect.width - control.popupWidth - LingmoUI.Units.smallSpacing))
        posY = Math.max(rect.y + LingmoUI.Units.smallSpacing,
                        Math.min(posY, rect.y + rect.height - control.popupHeight - LingmoUI.Units.smallSpacing))

        control.x = Math.round(posX)
        control.y = Math.round(posY)
    }

    Behavior on x {
        enabled: control.visible
        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }
    }

    Behavior on y {
        enabled: control.visible
        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }
    }

    LingmoUI.WindowHelper {
        id: windowHelper
    }

    LingmoUI.WindowShadow {
        view: control
        geometry: Qt.rect(0, 0, control.popupWidth, control.popupHeight)
        radius: _background.radius
    }

    LingmoUI.WindowBlur {
        view: control
        enabled: windowHelper.compositing
        windowRadius: _background.radius
        geometry: Qt.rect(0, 0, control.popupWidth, control.popupHeight)
    }

    Rectangle {
        id: _background
        width: control.popupWidth
        height: control.popupHeight
        radius: windowHelper.compositing ? LingmoUI.Theme.mediumRadius : 0
        color: LingmoUI.Theme.secondBackgroundColor
        opacity: windowHelper.compositing ? 0.9 : 1
        border.width: 1 / LingmoUI.Units.devicePixelRatio
        border.pixelAligned: LingmoUI.Units.devicePixelRatio > 1 ? false : true
        border.color: LingmoUI.Theme.darkMode ? Qt.rgba(255, 255, 255, 0.15)
                                              : Qt.rgba(0, 0, 0, 0.15)
    }

    Item {
        id: content
        width: control.popupWidth
        height: control.popupHeight

        HoverHandler {
            id: popupHover
            onHoveredChanged: {
                if (hovered)
                    hideTimer.stop()
                else
                    control.requestHide()
            }
        }

        NumberAnimation {
            id: switchAnimation
            target: content
            property: "opacity"
            from: 0.3
            to: 1
            duration: 150
            easing.type: Easing.OutCubic
        }

        Grid {
            x: control.padding
            y: control.padding
            columns: control.horizontal ? Math.max(control.count, 1) : 1
            spacing: control.cardSpacing

            Repeater {
                model: windowPreviews

                delegate: Item {
                    id: card
                    width: control.cardWidth
                    height: control.cardHeight

                    property bool hovered: cardArea.containsMouse || closeArea.containsMouse

                    Rectangle {
                        anchors.fill: parent
                        radius: LingmoUI.Theme.smallRadius
                        color: LingmoUI.Theme.textColor
                        opacity: card.hovered ? 0.12 : model.active ? 0.06 : 0

                        Behavior on opacity {
                            NumberAnimation {
                                duration: 150
                            }
                        }
                    }

                    Item {
                        id: thumbArea
                        x: control.cardPadding
                        y: control.cardPadding
                        width: control.thumbWidth
                        height: control.thumbHeight

                        Image {
                            id: thumbnail
                            anchors.fill: parent
                            source: model.thumbnail
                            fillMode: Image.PreserveAspectFit
                            asynchronous: false
                            cache: false
                            smooth: true
                            mipmap: true
                            visible: model.thumbnail !== "" && status === Image.Ready
                            // Minimized or on another desktop: an older picture
                            opacity: model.minimized || !model.onCurrentDesktop ? 0.6 : 1
                        }

                        LingmoUI.IconItem {
                            anchors.centerIn: parent
                            width: Math.round(Math.min(parent.width, parent.height) * 0.6)
                            height: width
                            source: control.iconName
                            visible: !thumbnail.visible
                        }
                    }

                    Label {
                        anchors.top: thumbArea.bottom
                        anchors.left: thumbArea.left
                        anchors.right: thumbArea.right
                        height: control.titleHeight
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: model.title
                        color: LingmoUI.Theme.textColor
                    }

                    MouseArea {
                        id: cardArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton

                        onClicked: function(mouse) {
                            if (mouse.button === Qt.MiddleButton) {
                                windowPreviews.close(model.windowId)
                            } else {
                                windowPreviews.activate(model.windowId)
                                control.hideNow()
                            }
                        }
                    }

                    Rectangle {
                        id: closeButton
                        width: 20
                        height: 20
                        radius: width / 2
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: control.cardPadding / 2
                        visible: card.hovered
                        color: closeArea.containsMouse ? LingmoUI.Theme.redColor
                                                       : LingmoUI.Theme.darkMode ? "#4C4C4D" : "#E4E4E6"

                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Close window")

                        Repeater {
                            model: [45, -45]

                            Rectangle {
                                anchors.centerIn: parent
                                width: 9
                                height: 1.5
                                rotation: modelData
                                antialiasing: true
                                color: closeArea.containsMouse ? "#FFFFFF" : LingmoUI.Theme.textColor
                            }
                        }

                        MouseArea {
                            id: closeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: windowPreviews.close(model.windowId)
                        }
                    }
                }
            }
        }
    }
}
