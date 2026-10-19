pragma ComponentBehavior: Bound
import QtQuick 2.9
import QtQuick.Controls
import "."
import "../theme"
import SystemProperties 1.0

Column {
    id: aboutPage
    signal scrollToEndRequested()
    width: parent ? parent.width : 0
    spacing: Theme.spaceLg
    readonly property string licenseUrl: "https://github.com/AlkaidLab/foundation-sunshine/blob/master/LICENSE"
    readonly property string noticeUrl: "https://github.com/AlkaidLab/foundation-sunshine/blob/master/NOTICE"
    function openExternal(url) {
        if (!Qt.openUrlExternally(url)) {
            ToolTip.show(qsTr("No external browser is available."), 3500)
        }
    }
    SettingsCard {
        title: qsTr("About")
        Text {
            text: qsTr("Version %1").arg(SystemProperties.versionString)
            color: Theme.text
            font.family: Theme.fontSans
            font.pointSize: Theme.fontBody
        }
    }
    SettingsCard {
        title: qsTr("Legal notice")
        subtitle: qsTr("This software is released under GNU GPLv3. The full license and third-party component information are provided below.")

        SettingsRow {
            id: legalInfoRow

            title: "GNU GPL v3.0"

            Flow {
                width: Math.min(300, Math.max(0, legalInfoRow.width - Theme.spaceMd * 2))
                spacing: Theme.spaceSm

                HardLink {
                    text: qsTr("License agreement")
                    onClicked: aboutPage.openExternal(aboutPage.licenseUrl)
                }

                HardLink {
                    text: qsTr("Third-party notice")
                    onClicked: aboutPage.openExternal(aboutPage.noticeUrl)
                }
            }
        }

        CommunityLicenseNotice {
            width: parent.width
        }
    }

}
