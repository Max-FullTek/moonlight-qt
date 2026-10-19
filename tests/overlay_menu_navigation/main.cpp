#include "../../app/streaming/video/overlaymenupanel.h"
#include <QGuiApplication>
#include <QFontDatabase>
#include <QCursor>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QTest>

// Native preview of the production panel, for retained stream controls.
class MenuPreview : public QRasterWindow {
public:
    explicit MenuPreview(OverlayMenuPanel &panel) : menu(panel) {
        setTitle(QStringLiteral("Stream Menu Interaction Check"));
        setGeometry(150, 100, 900, 700);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(QRect(QPoint(), size()), QColor(35, 38, 45));
        painter.setPen(Qt::white);
        painter.drawText(QRect(30, 30, 800, 100), Qt::AlignLeft,
            QStringLiteral("Stream menu preview\nClick outside to close; click the background to reopen.\nEscape: back, then close."));
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        if (menu.isMenuVisible()) menu.dismissOnOutsideClick(QCursor::pos());
        else menu.showAtCursor(x(), y(), width(), height(), position() + QPoint(250, 150), false);
    }
    void keyReleaseEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) menu.gamepadBack();
    }
private:
    OverlayMenuPanel &menu;
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Manrope-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Manrope-SemiBold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMMono-Regular.ttf"));
    OverlayMenuPanel panel;
    if (app.arguments().contains(QStringLiteral("--interactive"))) {
        MenuPreview preview(panel);
        preview.show();
        panel.setTransientParent(&preview);
        panel.showAtCursor(0, 0, 1600, 1200, QPoint(400, 250), false);
        return app.exec();
    }
    // Entering and leaving a retained submenu must preserve active interaction.
    panel.showAtCursor(0, 0, 1600, 1200, QPoint(400, 400), true);
    QTest::qWait(20);
    const int initialHeight = panel.height();
    QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier,
                      QPoint(140, 8 + 32 + 4 + 1 * 38 + 19));
    QTest::qWait(700);
    if (!panel.isVisible()) qFatal("menu position submenu dismissed during interaction");
    panel.gamepadBack();
    QTest::qWait(700);
    if (!panel.isVisible() || panel.height() != initialHeight) qFatal("back lost active interaction");
    panel.dismissOnOutsideClick(panel.geometry().center());
    if (!panel.isMenuVisible()) qFatal("inside click dismissed menu");
    panel.dismissOnOutsideClick(QPoint(-100, -100));
    // The close animation needs a beat; tolerate slow CI timers.
    for (int i = 0; i < 40 && panel.isVisible(); i++) QTest::qWait(50);
    if (panel.isVisible()) qFatal("outside click failed to dismiss menu");
    // Reopening restores transient pointer-triggered behavior.
    panel.showAtCursor(0, 0, 1600, 1200, QPoint(400, 400), true);
    QTest::qWait(700);
    if (panel.isVisible()) qFatal("fresh pointer-triggered menu lost auto-dismiss");
    panel.showAtCursor(0, 0, 1600, 1200, QPoint(400, 400), false);
    QTest::qWait(240);
    panel.gamepadBack();
    QTest::qWait(220);
    if (panel.isVisible()) qFatal("back at top level failed to close menu");

    // --- Bitrate scrubber: preset tap, debounced scrub, wheel, flush ---
    int bitrateCommits = 0;
    int lastBitrate = 0;
    panel.setBitrateChangeCallback([&](int kbps) {
        lastBitrate = kbps;
        ++bitrateCommits;
    });
    panel.updateBitrateState(10000);
    panel.showAtCursor(0, 0, 1600, 1200, QPoint(400, 400), false);
    QTest::qWait(240);
    // Enter the Bitrate submenu (row 2), then tap the 5 Mbps preset (row 3).
    QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier,
                      QPoint(140, 8 + 32 + 4 + 2 * 38 + 19));
    QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier,
                      QPoint(140, 8 + 32 + 4 + 3 * 38 + 19));
    if (lastBitrate != 5000 || bitrateCommits != 1) qFatal("preset tap did not commit");
    if (!panel.isMenuVisible()) qFatal("preset tap closed the menu");
    // D-pad right scrubs upward but must wait out the commit debounce.
    panel.gamepadAdjustSlider(1);
    if (bitrateCommits != 1 || lastBitrate != 5000) qFatal("scrub committed before debounce");
    QTest::qWait(600);
    if (bitrateCommits != 2 || lastBitrate <= 5000) qFatal("scrub never committed");
    const int afterScrub = lastBitrate;
    // One wheel notch on the slider row steps again; A flushes immediately.
    QWheelEvent wheelUp(QPointF(140, 8 + 32 + 4 + 19), QPointF(),
                        QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                        Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&panel, &wheelUp);
    panel.gamepadSelect();
    if (bitrateCommits != 3 || lastBitrate <= afterScrub) qFatal("wheel step not flushed");
    if (!panel.isMenuVisible()) qFatal("slider interaction closed the menu");
    // Saturating the wheel must clamp at the Sunshine /bitrate cap (800 Mbps).
    for (int i = 0; i < 250; i++) {
        QWheelEvent wheelMax(QPointF(140, 8 + 32 + 4 + 19), QPointF(),
                             QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                             Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&panel, &wheelMax);
    }
    panel.gamepadSelect();
    if (lastBitrate != 800000) qFatal("scrubber did not clamp at 800000 Kbps");
    // Track press scrubs to the click position (window → content coords):
    // near the left end lands near the minimum, near the right end at the cap.
    QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier,
                      QPoint(8 + 102 + 4, 8 + 32 + 4 + 19));
    panel.gamepadSelect();
    if (bitrateCommits != 5 || lastBitrate > 2000)
        qFatal("track press near minimum failed: commits=%d last=%d", bitrateCommits, lastBitrate);
    QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier,
                      QPoint(8 + 252, 8 + 32 + 4 + 19));
    panel.gamepadSelect();
    if (bitrateCommits != 6 || lastBitrate != 800000)
        qFatal("track press near maximum failed: commits=%d last=%d", bitrateCommits, lastBitrate);
    panel.dismissOnOutsideClick(QPoint(-100, -100));
    for (int i = 0; i < 40 && panel.isVisible(); i++) QTest::qWait(50);
    if (panel.isVisible()) qFatal("outside click after slider use failed to dismiss");
    return 0;
}
