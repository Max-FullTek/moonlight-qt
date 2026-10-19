import QtQuick 2.15
import QtTest 1.3
import "../../app/gui" as Gui

Item {
    width: 640
    height: 480

    Gui.HostEmptyState {
        id: emptyState
        width: 560
    }

    SignalSpy { id: addSpy; target: emptyState; signalName: "addComputerRequested" }
    SignalSpy { id: settingsSpy; target: emptyState; signalName: "settingsRequested" }

    TestCase {
        name: "LiteManualConnectionUI"
        when: windowShown

        function init() {
            emptyState.width = 560
            emptyState.discoveryEnabled = false
            addSpy.clear()
            settingsSpy.clear()
        }

        function test_keyboard_add_and_settings() {
            var add = findChild(emptyState, "addComputerButton")
            var settings = findChild(emptyState, "openSettingsButton")
            emptyState.focusPrimaryAction()
            tryCompare(add, "activeFocus", true)
            keyClick(Qt.Key_Space)
            compare(addSpy.count, 1)
            settings.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            compare(settingsSpy.count, 1)
        }

        function test_narrow_actions_wrap_without_overlap() {
            var add = findChild(emptyState, "addComputerButton")
            var settings = findChild(emptyState, "openSettingsButton")
            emptyState.width = Math.max(add.width, settings.width) + 1
            tryVerify(function() { return settings.y >= add.y + add.height })
            verify(emptyState.height > settings.y + settings.height)
        }

        function test_discovery_state_is_explicit() {
            var status = findChild(emptyState, "discoveryStatus")
            var disabledText = status.text
            verify(disabledText.length > 0)
            emptyState.discoveryEnabled = true
            verify(status.text.length > 0)
            verify(status.text !== disabledText)
        }
    }
}
