#pragma once

#include <QPoint>
#include <QJsonArray>
#include <QQuickWindow>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include <Limelight.h>
#include <opus_multistream.h>
#include "settings/streamingpreferences.h"
#include "backend/nvcomputer.h"
#include "supportedvideoformats.h"
#include "input/input.h"
#include "video/decoder.h"
#include "video/decoderfactory.h"
#include "audio/audiostream.h"
#include "video/overlaymanager.h"
#include "video/overlaymenupanel.h"
#include "video/overlaymenubutton.h"
#include "video/overlaytoast.h"
#ifndef STEAM_LINK
#include "micstream.h"
#endif

class DualSenseHapticsRenderer;
class QScreen;
#ifdef Q_OS_DARWIN
class MacQtEventPumpInputGuard;
#endif
#ifdef MOONLIGHT_ENABLE_FUNCTION_TESTS
class StylusReplayTest;
#endif

class Session : public QObject, public IInputHost
{
    Q_OBJECT

    friend class SdlInputHandler;

public:
    explicit Session(NvComputer* computer,
                     NvApp& app,
                     StreamingPreferences *preferences = nullptr,
                     QString launchDisplayName = QString(),
                     std::optional<bool> launchUseVdd = std::nullopt);
    virtual ~Session();

    // Set only by the Windows GPUI bridge before initialize(). Qt callers retain -1.
    void setClientDisplay(int display) { m_GpuiClientDisplay = display; }
    void setGpuiControlsClient(unsigned long processId) { m_GpuiControlsClient = processId; }
    bool usesGpuiControls() const { return m_GpuiControlsClient != 0; }
    void openGpuiControls();
    bool handleGpuiControl(const QString& action, int value = 0, int requestId = 0);
    static void wakeForCoreCommands();
    void setCoreCommandHandler(std::function<void()> handler) { m_CoreCommandHandler = std::move(handler); }
    void setLifecycleDispatcher(std::function<void(std::function<void()>)> dispatcher)
    {
        m_LifecycleDispatcher = std::move(dispatcher);
    }
    Q_INVOKABLE bool initialize(QQuickWindow* qtWindow);
    Q_INVOKABLE void start();
    Q_INVOKABLE void interrupt();
    Q_PROPERTY(QStringList launchWarnings MEMBER m_LaunchWarnings NOTIFY launchWarningsChanged);

    static
    void getDecoderInfo(SDL_Window* window,
                        bool& isHardwareAccelerated, bool& isFullScreenOnly,
                        bool& isHdrSupported, QSize& maxResolution);

    static Session* get()
    {
        return s_ActiveSession;
    }

    Overlay::OverlayManager& getOverlayManager()
    {
        return m_OverlayManager;
    }

    void flushWindowEvents();

    void setShouldExit(bool quitHostApp = false);

    // These route window-state operations through Qt only for the native
    // Wayland stream window. All other platforms retain SDL's behavior.
    bool isStreamingWindowFullscreen() const override;
    bool usesCompositorWindow() const override { return usesQtWaylandStreamWindow(); }
    bool shouldConfinePointerForCompositor() const override;
    bool usesQtWaylandStreamWindow() const;
    void minimizeStreamingWindow() override;

signals:
    void gpuiControlsChanged(bool active, bool gamepadMouse, bool hasGamepad);
    void gpuiControlsNavigate(QString direction);
    void gpuiBitrateResult(int requestId, int requestedKbps, bool success, QString error);
    void hostQuitFailed(QString message);
    void stageStarting(QString stage);

    void stageFailed(QString stage, int errorCode, QString failingPorts);

    void connectionStarted();

    void displayLaunchError(QString text);

    void quitStarting();

    void sessionFinished(int portTestResult);

    // Emitted after sessionFinished() when the session is ready to be destroyed
    void readyForDeletion();

    void launchWarningsChanged();

private:
    int m_GpuiClientDisplay = -1;
    unsigned long m_GpuiControlsClient = 0;
    std::function<void()> m_CoreCommandHandler;
    std::function<void(std::function<void()>)> m_LifecycleDispatcher;
    void dispatchLifecycleCompletion(std::function<void()> completion);
    bool m_GpuiControlsActive = false;
    bool m_GpuiRestoreCapture = false;
    struct WaylandWindowSnapshot
    {
        QSize size;
        Qt::WindowStates states = Qt::WindowNoState;
        bool visible = false;
        QScreen* screen = nullptr;
        qreal devicePixelRatio = 0.0;
        qreal refreshRate = 0.0;
        bool valid = false;
    };

    void exec();
    void dispatchConnectionCleanup();
    void finishConnectionCleanup();
    static void releaseActiveSession();

    bool startConnectionAsync();

    // Attempt to silently re-establish a streaming session after an
    // unexpected network interruption. Returns true if streaming resumed.
    bool tryReconnect();

    void handleSdlUserEvent(const SDL_UserEvent& event);

    void updateDualSenseHapticsControllerTarget();

    // Emit the appropriate error dialog for a connection termination code.
    void displayTerminationError(int errorCode);

    bool validateLaunch(SDL_Window* testWindow);

    void emitLaunchWarning(QString text);

    bool populateDecoderProperties(SDL_Window* window);

    bool testAudio(int audioConfiguration);

    int getAudioRendererCapabilities(int audioConfiguration);

    void getWindowDimensions(int& x, int& y,
                             int& width, int& height);

    void toggleFullscreen() override;
    void toggleStatisticsOverlay() override;
    void quitHostAndExit() override { setShouldExit(true); }
    void showInputControls() override { showQtOverlayMenu(); }
    bool queueTouchpadFlush() override { return queueTouchpadFrameFlush(); }
    void applyControllerMappings() override;
    std::optional<uint16_t> sensorReportRateLimit() const override;
    bool queueCursorFlush() override { return queueCursorVisibilityFlush(); }

    // Qt-based overlay menu
    void showQtOverlayMenu(std::optional<QPoint> pointerGlobalPosition = std::nullopt,
                           bool closeWhenPointerOutside = true);
    void hideQtOverlayMenu();
    void toggleQtOverlayMenu();
    bool isStreamingWindowVisible() const;
    bool isStreamingWindowMinimized() const;
    void syncQtOverlayWindowsWithSdlWindowState();
    QRect qtOverlayParentGeometry() const;
    QPoint qtOverlayPositionForSdlPoint(int x, int y) const;
#ifdef HAS_QT_SDL_WAYLAND_BRIDGE
    void syncWaylandSdlWindowState();
#endif
    void dispatchQtMenuAction(OverlayMenuPanel::MenuAction action);
    void requestRuntimeBitrateChange(int bitrateKbps, int requestId = 0);
    void startRuntimeBitrateWorker();
    void finishRuntimeBitrateChange();
    void showStreamingToast(const QString& message, int durationMs = 2000);
    void processQtOverlayEvents();
#ifdef MOONLIGHT_ENABLE_FUNCTION_TESTS
    void restoreCaptureAfterStylusReplayPanel();
#endif
    void notifyMouseEmulationMode(bool enabled) override;

    static
    bool queueTouchpadFrameFlush();

    static
    bool queueCursorVisibilityFlush();

    void updateOptimalWindowDisplayMode();

    using DecoderAvailability = VideoDecoderFactory::Availability;

    static
    DecoderAvailability getDecoderAvailability(SDL_Window* window,
                                               StreamingPreferences::VideoDecoderSelection vds,
                                               int videoFormat, int width, int height, int frameRate);

    static
    bool chooseDecoder(StreamingPreferences::VideoDecoderSelection vds,
                       StreamingPreferences::RendererSelection renderer,
                       SDL_Window* window, int videoFormat, int width, int height,
                       int frameRate, bool enableVsync, bool enableFramePacing,
                       bool enableVideoEnhancement, bool ignoreAspectRatio, bool testOnly,
                       IVideoDecoder*& chosenDecoder);

    static
    void clStageStarting(int stage);

    static
    void clStageFailed(int stage, int errorCode);

    static
    void clConnectionTerminated(int errorCode);

    static
    void clLogMessage(const char* format, ...);

    static
    void clRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor);

    static
    void clConnectionStatusUpdate(int connectionStatus);

    static
    void clSetHdrMode(bool enabled, void* hdrMetadata);

    static
    void clRumbleTriggers(uint16_t controllerNumber, uint16_t leftTrigger, uint16_t rightTrigger);

    static
    void clSetMotionEventState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz);

    static
    void clSetControllerLED(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b);

    static
    void clSetAdaptiveTriggers(uint16_t controllerNumber, uint8_t eventFlags, uint8_t typeLeft, uint8_t typeRight, uint8_t *left, uint8_t *right);

    static
    void clClipboardData(const char* data, int length);

    static
    void clCursorUpdate(const LI_CURSOR_UPDATE* update);

    static
    void clDs5HapticsPcm(const LI_DS5_HAPTICS_PCM_FRAME* frame);

    static
    void clDs5HapticsIrV2(const LI_DS5_HAPTICS_IR_FRAME_V2* frame);

    static
    int arInit(int audioConfiguration,
               const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
               void* arContext, int arFlags);

    static
    void arCleanup();

    static
    void arDecodeAndPlaySample(char* sampleData, int sampleLength);

    static
    int drSetup(int videoFormat, int width, int height, int frameRate, void*, int);

    static
    void drCleanup();

    void startMicrophone();
    void stopMicrophone();

    void startSunshineAbr();
    void stopSunshineAbr();
    void sendSunshineAbrFeedback();

    static
    int drSubmitDecodeUnit(PDECODE_UNIT du);

    StreamingPreferences* m_Preferences;
    bool m_IsFullScreen;
    SupportedVideoFormatList m_SupportedVideoFormats; // Sorted in order of descending priority
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacks;
    NvComputer* m_Computer;
    NvApp m_App;
    QString m_LaunchDisplayName;
    std::wstring m_ClientDisplayName;
    std::optional<bool> m_LaunchUseVdd;
    SDL_Window* m_Window;
    IVideoDecoder* m_VideoDecoder;
    SDL_mutex* m_DecoderLock;
    bool m_AudioDisabled;
    Uint32 m_FullScreenFlag;
    QQuickWindow* m_QtWindow;
    QWindow* m_WaylandStreamWindow;
    WaylandWindowSnapshot m_WaylandWindowSnapshot;
    bool m_WaylandOverlayShortcutPressed;
    bool m_UnexpectedTermination;
    SdlInputHandler* m_InputHandler;
    int m_MouseEmulationRefCount;
    int m_FlushingWindowEventsRef;
    QStringList m_LaunchWarnings;
    bool m_ShouldExit;

    // Graceful reconnect state
    bool m_ConnectionInterrupted;        // set by clConnectionTerminated for recoverable errors
    bool m_SuppressConnectionErrorDialog; // suppress error dialogs during reconnect attempts
    bool m_HasReceivedVideo;             // true after the first decode unit of the current connection
    int m_LastTerminationErrorCode;      // stored to show final error if reconnect gives up

    bool m_AsyncConnectionSuccess;
    std::thread m_ConnectionThread;
    float m_LastClientSdrWhiteNits;
    int m_PortTestResults;

    int m_ActiveVideoFormat;
    int m_ActiveVideoWidth;
    int m_ActiveVideoHeight;
    int m_ActiveVideoFrameRate;

    AudioStream m_AudioStream;
    DualSenseHapticsRenderer* m_DualSenseHapticsRenderer;

    Overlay::OverlayManager m_OverlayManager;
    bool m_WasCapturedBeforeMenu;  // 菜单打开前鼠标是否处于捕获状态
    bool m_DeferCaptureRestore;    // 延迟恢复鼠标捕获（全屏切换等）
    bool m_PendingMicToggle;       // 延迟麦克风切换（避免堆损坏）
#ifdef MOONLIGHT_ENABLE_FUNCTION_TESTS
    // Developer-only test harness. All replay/UI behavior lives behind this
    // boundary so production Session code keeps only integration hooks.
    std::unique_ptr<StylusReplayTest> m_StylusReplayTest;
    bool m_WasCapturedBeforeStylusReplayPanel;
#endif
    bool m_SunshineAbrEnabled;
    Uint32 m_LastAbrFeedbackTicks;
    RTP_VIDEO_STATS m_LastAbrVideoStats;
    std::future<void> m_AbrFeedback;
    std::shared_ptr<std::atomic_int> m_AbrCurrentBitrateKbps;
    // Runtime bitrate requests coalesce here; a single background worker
    // applies the newest value so HTTP never blocks the stream loop.
    std::thread m_RuntimeBitrateThread;
    struct RuntimeBitrateResult {
        int requestId;
        int bitrateKbps;
        bool success;
        QString error;
    };
    std::mutex m_RuntimeBitrateResultMutex;
    std::optional<RuntimeBitrateResult> m_RuntimeBitrateResult;
    // Pending request and closing state belong to the session owner thread.
    int m_PendingRuntimeBitrateKbps = 0;
    int m_PendingRuntimeBitrateRequestId = 0;
    bool m_RuntimeBitrateClosing = false;
    OverlayMenuPanel* m_MenuPanel; // Qt-based overlay menu window
    OverlayMenuButton* m_MenuButton = nullptr; // Qt-based floating menu button
    OverlayToast* m_Toast;           // Qt-based toast notification
#ifdef Q_OS_DARWIN
    std::unique_ptr<MacQtEventPumpInputGuard> m_MacQtEventPumpInputGuard;
#endif
    Uint32 m_MenuCloseTicks;       // 菜单关闭时间戳（防抖）
    void stopClipboardHelper();
    std::mutex m_ClipboardHelperMutex;
    class ClipboardHelperClient*
        m_ClipboardHelper; // Protected from receive callbacks by m_ClipboardHelperMutex
    std::mutex m_CursorUpdateMutex;
    std::shared_ptr<RemoteCursorUpdate> m_PendingCursorUpdate;
    bool m_CursorUpdateEventQueued = false;
    static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;
    static Session* s_ActiveSession;
    static std::mutex s_ActiveSessionMutex;
    static std::condition_variable s_ActiveSessionChanged;
#ifndef STEAM_LINK
    MicStream* m_MicStream;
#else
    void* m_MicStream;
#endif
};
