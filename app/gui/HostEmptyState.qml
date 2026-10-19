import QtQuick 2.9
import QtQuick.Controls
import "theme"

Column {
    id: emptyState
    property bool discoveryEnabled: false
    signal addComputerRequested()
    signal settingsRequested()
    spacing: Theme.spaceLg

    function focusPrimaryAction() {
        addButton.forceActiveFocus(Qt.TabFocusReason)
    }

    Text {
        width: parent.width
        text: qsTr("Add your PC")
        color: Theme.text
        font.family: Theme.fontSans
        font.pointSize: 24
        font.weight: Font.Bold
        wrapMode: Text.Wrap
    }

    Text {
        width: parent.width
        text: qsTr("Enter your PC's LAN or Tailscale IP address to pair and start streaming.")
        color: Theme.textSettingsSubtitle
        font.family: Theme.fontSans
        font.pointSize: Theme.fontBody
        wrapMode: Text.Wrap
    }

    Flow {
        width: parent.width
        spacing: Theme.spaceMd

        HardButton {
            id: addButton
            objectName: "addComputerButton"
            text: qsTr("Add PC manually")
            primary: true
            onClicked: emptyState.addComputerRequested()
        }

        HardButton {
            objectName: "openSettingsButton"
            text: qsTr("Open settings")
            onClicked: emptyState.settingsRequested()
        }
    }

    Text {
        objectName: "discoveryStatus"
        width: parent.width
        text: emptyState.discoveryEnabled
              ? qsTr("Local discovery is running. You can also add a PC manually.")
              : qsTr("Local discovery is off. You can enable it in settings.")
        color: Theme.textDim
        font.family: Theme.fontSans
        font.pointSize: Theme.fontCaption
        wrapMode: Text.Wrap
    }
}
