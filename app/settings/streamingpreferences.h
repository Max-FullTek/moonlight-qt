#pragma once

#include <QRect>
#ifndef LITE_GPUI_WORKER
#include <QObject>
class QQmlEngine;
#endif
#include <QString>
#include "streaming/video/videodecoderoptions.h"
#include "streaming/input/inputoptions.h"

class StreamingPreferences
#ifndef LITE_GPUI_WORKER
    : public QObject
#endif
{
#ifndef LITE_GPUI_WORKER
    Q_OBJECT
#endif

public:
#ifdef LITE_GPUI_WORKER
    static StreamingPreferences* get();
    static int getDefaultBitrate(int width, int height, int fps, bool yuv444);
    void save();
#else
    static StreamingPreferences* get(QQmlEngine *qmlEngine = nullptr);
    Q_INVOKABLE static int
    getDefaultBitrate(int width, int height, int fps, bool yuv444);

    Q_INVOKABLE void save();
#endif

    void reload();

    enum AudioConfig
    {
        AC_STEREO,
        AC_51_SURROUND,
        AC_71_SURROUND,
        AC_714_SURROUND
    };

    enum VideoCodecConfig
    {
        VCC_AUTO,
        VCC_FORCE_H264,
        VCC_FORCE_HEVC,
        VCC_FORCE_HEVC_HDR_DEPRECATED, // Kept for backwards compatibility
        VCC_FORCE_AV1
    };

    enum VideoDecoderSelection
    {
        VDS_AUTO = VideoDecoderOptions::Auto,
        VDS_FORCE_HARDWARE = VideoDecoderOptions::Hardware,
        VDS_FORCE_SOFTWARE = VideoDecoderOptions::Software
    };

    // Mac only (for now)
    enum RendererSelection
    {
        RS_PROBE_ONLY = VideoDecoderOptions::ProbeOnly, // Only valid for probing decoder properties
        RS_AUTO = VideoDecoderOptions::Automatic,
        RS_VULKAN = VideoDecoderOptions::Vulkan,
        RS_METAL = VideoDecoderOptions::Metal,
        RS_AVSBDL = VideoDecoderOptions::AvSampleBuffer
    };

    enum WindowMode
    {
        WM_FULLSCREEN,
        WM_FULLSCREEN_DESKTOP,
        WM_WINDOWED
    };

    enum UIDisplayMode
    {
        UI_WINDOWED,
        UI_MAXIMIZED,
        UI_FULLSCREEN
    };

    enum BackgroundSource
    {
        BGS_PHOTOGRAPHY,
        BGS_ANIME,
        BGS_API,
        BGS_LOCAL,
        BGS_NONE,
    };

    // New entries must go at the end of the enum
    // to avoid renumbering existing entries (which
    // would affect existing user preferences).
    enum Language
    {
        LANG_AUTO,
        LANG_EN,
        LANG_FR,
        LANG_ZH_CN,
        LANG_DE,
        LANG_NB_NO,
        LANG_RU,
        LANG_ES,
        LANG_JA,
        LANG_VI,
        LANG_TH,
        LANG_KO,
        LANG_HU,
        LANG_NL,
        LANG_SV,
        LANG_TR,
        LANG_UK,
        LANG_ZH_TW,
        LANG_PT,
        LANG_PT_BR,
        LANG_EL,
        LANG_IT,
        LANG_HI,
        LANG_PL,
        LANG_CS,
        LANG_HE,
        LANG_CKB,
        LANG_LT,
        LANG_ET,
        LANG_BG,
        LANG_EO,
        LANG_TA,
    };

    enum CaptureSysKeysMode
    {
        CSK_OFF = InputOptions::CSK_OFF,
        CSK_FULLSCREEN = InputOptions::CSK_FULLSCREEN,
        CSK_ALWAYS = InputOptions::CSK_ALWAYS,
    };

    enum OverlayMenuPosition
    {
        OMP_DISABLED = 0,   // Do not show the overlay menu
        OMP_BUTTON = 1,     // Show a floating button on the streaming window
        OMP_TOP_EDGE = 2,   // Show from the top edge of the streaming window
        OMP_RIGHT_EDGE = 3, // Show on right edge
        OMP_LEFT_EDGE = 4,  // Show on left edge
    };

    void setOverlayMenuPosition(OverlayMenuPosition position);

#ifndef LITE_GPUI_WORKER
    BackgroundSource backgroundSource() const;
    void setBackgroundSource(BackgroundSource source);
    QString backgroundImageApi() const;
    void setBackgroundImageApi(const QString &apiUrl);
    QString backgroundImageLocalPath() const;
    void setBackgroundImageLocalPath(const QString &path);
    int backgroundOverlayOpacity() const;
    void setBackgroundOverlayOpacity(int opacity);
    bool backgroundSetupCompleted() const;
    void setBackgroundSetupCompleted(bool completed);
    Q_INVOKABLE void resetBackgroundConfiguration();
#endif

    enum HdrMode
    {
        HDR_PQ  = 1,   // HDR10/PQ (SMPTE ST 2084) - default
        HDR_HLG = 2,   // HLG (Hybrid Log-Gamma, ARIB STD-B67)
    };

    enum HdrBrightnessMode
    {
        HBM_HOST_DEFAULT = 0,
        HBM_AUTO = 1,
        HBM_MANUAL = 2,
    };

    enum DualSenseHapticsMode
    {
        DSHM_PHYSICAL = 0,
        DSHM_EMULATED = 1,
    };

    enum GamepadQuitCombo
    {
        GQC_DEFAULT = InputOptions::GQC_DEFAULT,        // Start + Select + L1 + R1 (original)
        GQC_SELECT_L1_R1_X = InputOptions::GQC_SELECT_L1_R1_X, // Deprecated: same combo as the stats overlay toggle; migrated to
                                // GQC_SELECT_L1_R1_Y on load
        GQC_SELECT_L1_R1_Y = InputOptions::GQC_SELECT_L1_R1_Y, // Select + L1 + R1 + Y
        GQC_START_L1_R1_A = InputOptions::GQC_START_L1_R1_A,  // Start + L1 + R1 + A (avoids Select conflict)
        GQC_START_L1_R1_B = InputOptions::GQC_START_L1_R1_B,  // Start + L1 + R1 + B (avoids Select conflict)
        GQC_L1_R1_X_Y = InputOptions::GQC_L1_R1_X_Y,      // L1 + R1 + X + Y (no Select/Start at all)
        GQC_L1_R1_A_B = InputOptions::GQC_L1_R1_A_B,      // L1 + R1 + A + B (no Select/Start at all)
    };

    enum ScreenCombinationMode
    {
        SCM_FOLLOW_HOST = -1,
        SCM_NO_OPERATION = 0,
        SCM_ENSURE_ACTIVE = 1,
        SCM_ENSURE_PRIMARY = 2,
        SCM_ENSURE_ONLY_DISPLAY = 3,
        SCM_ENSURE_SECONDARY = 4,
    };

#ifdef LITE_GPUI_WORKER
    bool retranslate();
#else
    Q_ENUM(AudioConfig)
    Q_ENUM(VideoCodecConfig)
    Q_ENUM(VideoDecoderSelection)
    Q_ENUM(RendererSelection)
    Q_ENUM(WindowMode)
    Q_ENUM(UIDisplayMode)
    Q_ENUM(BackgroundSource)
    Q_ENUM(Language);
    Q_ENUM(CaptureSysKeysMode);
    Q_ENUM(OverlayMenuPosition);
    Q_ENUM(HdrMode);
    Q_ENUM(HdrBrightnessMode);
    Q_ENUM(DualSenseHapticsMode);
    Q_ENUM(GamepadQuitCombo);
    Q_ENUM(ScreenCombinationMode);

    Q_PROPERTY(int width MEMBER width NOTIFY displayModeChanged)
    Q_PROPERTY(int height MEMBER height NOTIFY displayModeChanged)
    Q_PROPERTY(int fps MEMBER fps NOTIFY displayModeChanged)
    Q_PROPERTY(int bitrateKbps MEMBER bitrateKbps NOTIFY bitrateChanged)
    Q_PROPERTY(int fecPercentage MEMBER fecPercentage NOTIFY fecChanged)
    Q_PROPERTY(int fixedFecPercentage MEMBER fixedFecPercentage NOTIFY fixedFecChanged)
    Q_PROPERTY(bool autoAdjustBitrate MEMBER autoAdjustBitrate NOTIFY autoAdjustBitrateChanged)
    Q_PROPERTY(bool enableSunshineAbr MEMBER enableSunshineAbr NOTIFY enableSunshineAbrChanged)
    Q_PROPERTY(bool ignoreAspectRatio MEMBER ignoreAspectRatio NOTIFY ignoreAspectRatioChanged)
    Q_PROPERTY(bool enableVsync MEMBER enableVsync NOTIFY enableVsyncChanged)
    Q_PROPERTY(bool gameOptimizations MEMBER gameOptimizations NOTIFY gameOptimizationsChanged)
    Q_PROPERTY(bool playAudioOnHost MEMBER playAudioOnHost NOTIFY playAudioOnHostChanged)
    Q_PROPERTY(bool multiController MEMBER multiController NOTIFY multiControllerChanged)
    Q_PROPERTY(bool enableMdns MEMBER enableMdns NOTIFY enableMdnsChanged)
    Q_PROPERTY(bool quitAppAfter MEMBER quitAppAfter NOTIFY quitAppAfterChanged)
    Q_PROPERTY(bool absoluteMouseMode MEMBER absoluteMouseMode NOTIFY absoluteMouseModeChanged)
    Q_PROPERTY(bool showLocalCursor MEMBER showLocalCursor NOTIFY showLocalCursorChanged)
    Q_PROPERTY(bool absoluteTouchMode MEMBER absoluteTouchMode NOTIFY absoluteTouchModeChanged)
    Q_PROPERTY(bool enableNativeTouchpad MEMBER enableNativeTouchpad NOTIFY enableNativeTouchpadChanged)
    Q_PROPERTY(DualSenseHapticsMode dualSenseHapticsMode MEMBER dualSenseHapticsMode NOTIFY dualSenseHapticsModeChanged)
    Q_PROPERTY(bool framePacing MEMBER framePacing NOTIFY framePacingChanged)
    Q_PROPERTY(bool videoEnhancement MEMBER videoEnhancement NOTIFY videoEnhancementChanged)
    Q_PROPERTY(bool streamResolutionScale MEMBER streamResolutionScale NOTIFY streamResolutionScaleChanged)
    Q_PROPERTY(int streamResolutionScaleRatio MEMBER streamResolutionScaleRatio NOTIFY streamResolutionScaleRatioChanged)
    Q_PROPERTY(bool remoteResolution MEMBER remoteResolution NOTIFY remoteResolutionChanged)
    Q_PROPERTY(int remoteResolutionWidth MEMBER remoteResolutionWidth NOTIFY remoteResolutionWidthChanged)
    Q_PROPERTY(int remoteResolutionHeight MEMBER remoteResolutionHeight NOTIFY remoteResolutionHeightChanged)
    Q_PROPERTY(bool remoteFps MEMBER remoteFps NOTIFY remoteFpsChanged)
    Q_PROPERTY(int remoteFpsRate MEMBER remoteFpsRate NOTIFY remoteFpsRateChanged)
    Q_PROPERTY(bool connectionWarnings MEMBER connectionWarnings NOTIFY connectionWarningsChanged)
    Q_PROPERTY(bool configurationWarnings MEMBER configurationWarnings NOTIFY configurationWarningsChanged)
    Q_PROPERTY(bool richPresence MEMBER richPresence NOTIFY richPresenceChanged)
    Q_PROPERTY(bool gamepadMouse MEMBER gamepadMouse NOTIFY gamepadMouseChanged)
    Q_PROPERTY(int gamepadDeadzone MEMBER gamepadDeadzone NOTIFY gamepadDeadzoneChanged)
    Q_PROPERTY(bool detectNetworkBlocking MEMBER detectNetworkBlocking NOTIFY detectNetworkBlockingChanged)
    Q_PROPERTY(bool showPerformanceOverlay MEMBER showPerformanceOverlay NOTIFY showPerformanceOverlayChanged)
    Q_PROPERTY(AudioConfig audioConfig MEMBER audioConfig NOTIFY audioConfigChanged)
    Q_PROPERTY(VideoCodecConfig videoCodecConfig MEMBER videoCodecConfig NOTIFY videoCodecConfigChanged)
    Q_PROPERTY(bool enableHdr MEMBER enableHdr NOTIFY enableHdrChanged)
    Q_PROPERTY(HdrMode hdrMode MEMBER hdrMode NOTIFY hdrModeChanged)
    Q_PROPERTY(HdrBrightnessMode hdrBrightnessMode MEMBER hdrBrightnessMode NOTIFY hdrBrightnessModeChanged)
    Q_PROPERTY(double hdrMaxBrightness MEMBER hdrMaxBrightness NOTIFY hdrBrightnessValuesChanged)
    Q_PROPERTY(double hdrMinBrightness MEMBER hdrMinBrightness NOTIFY hdrBrightnessValuesChanged)
    Q_PROPERTY(double hdrMaxAverageBrightness MEMBER hdrMaxAverageBrightness NOTIFY hdrBrightnessValuesChanged)
    Q_PROPERTY(bool enableYUV444 MEMBER enableYUV444 NOTIFY enableYUV444Changed)
    Q_PROPERTY(VideoDecoderSelection videoDecoderSelection MEMBER videoDecoderSelection NOTIFY videoDecoderSelectionChanged)
    Q_PROPERTY(RendererSelection rendererSelection MEMBER rendererSelection NOTIFY rendererSelectionChanged)
    Q_PROPERTY(WindowMode windowMode MEMBER windowMode NOTIFY windowModeChanged)
    Q_PROPERTY(WindowMode recommendedFullScreenMode MEMBER recommendedFullScreenMode CONSTANT)
    Q_PROPERTY(UIDisplayMode uiDisplayMode MEMBER uiDisplayMode NOTIFY uiDisplayModeChanged)
    Q_PROPERTY(bool rememberWindowPosition MEMBER rememberWindowPosition NOTIFY rememberWindowPositionChanged)
    Q_PROPERTY(BackgroundSource backgroundSource READ backgroundSource WRITE setBackgroundSource NOTIFY backgroundConfigurationChanged)
    Q_PROPERTY(QString backgroundImageApi READ backgroundImageApi WRITE setBackgroundImageApi NOTIFY backgroundConfigurationChanged)
    Q_PROPERTY(QString backgroundImageLocalPath READ backgroundImageLocalPath WRITE setBackgroundImageLocalPath NOTIFY backgroundConfigurationChanged)
    Q_PROPERTY(int backgroundOverlayOpacity READ backgroundOverlayOpacity WRITE setBackgroundOverlayOpacity NOTIFY backgroundOverlayOpacityChanged)
    Q_PROPERTY(bool backgroundSetupCompleted READ backgroundSetupCompleted WRITE setBackgroundSetupCompleted NOTIFY backgroundSetupCompletedChanged)
    Q_PROPERTY(bool swapMouseButtons MEMBER swapMouseButtons NOTIFY mouseButtonsChanged)
    Q_PROPERTY(bool swapWinAltKeys MEMBER swapWinAltKeys NOTIFY swapWinAltKeysChanged)
    Q_PROPERTY(bool muteOnFocusLoss MEMBER muteOnFocusLoss NOTIFY muteOnFocusLossChanged)
    Q_PROPERTY(bool backgroundGamepad MEMBER backgroundGamepad NOTIFY backgroundGamepadChanged)
    Q_PROPERTY(GamepadQuitCombo gamepadQuitCombo MEMBER gamepadQuitCombo NOTIFY gamepadQuitComboChanged)
    Q_PROPERTY(bool reverseScrollDirection MEMBER reverseScrollDirection NOTIFY reverseScrollDirectionChanged)
    Q_PROPERTY(bool swapFaceButtons MEMBER swapFaceButtons NOTIFY swapFaceButtonsChanged)
    Q_PROPERTY(bool keepAwake MEMBER keepAwake NOTIFY keepAwakeChanged)
    Q_PROPERTY(CaptureSysKeysMode captureSysKeysMode MEMBER captureSysKeysMode NOTIFY captureSysKeysModeChanged)
    Q_PROPERTY(Language language MEMBER language NOTIFY languageChanged)
    Q_PROPERTY(ScreenCombinationMode screenCombinationMode MEMBER screenCombinationMode NOTIFY screenCombinationModeChanged)
    Q_PROPERTY(bool enableMicrophone MEMBER enableMicrophone NOTIFY enableMicrophoneChanged)
    Q_PROPERTY(OverlayMenuPosition overlayMenuPosition MEMBER overlayMenuPosition NOTIFY overlayMenuPositionChanged)
    Q_PROPERTY(bool autoUpdateCheck MEMBER autoUpdateCheck NOTIFY autoUpdateCheckChanged)

    Q_INVOKABLE bool retranslate();
#endif

    // Directly accessible members for preferences
    int width;
    int height;
    int fps;
    int bitrateKbps;
    int fecPercentage;
    int fixedFecPercentage;
    bool autoAdjustBitrate;
    bool enableSunshineAbr;
    bool ignoreAspectRatio;
    bool enableVsync;
    bool gameOptimizations;
    bool playAudioOnHost;
    bool multiController;
    bool enableMdns;
    bool quitAppAfter;
    bool absoluteMouseMode;
    bool showLocalCursor;
    bool absoluteTouchMode;
    bool enableNativeTouchpad;
    DualSenseHapticsMode dualSenseHapticsMode;
    bool framePacing;
    bool videoEnhancement;
    bool streamResolutionScale;
    int streamResolutionScaleRatio;
    bool remoteResolution;
    int remoteResolutionWidth;
    int remoteResolutionHeight;
    bool remoteFps;
    int remoteFpsRate;
    bool connectionWarnings;
    bool configurationWarnings;
    bool richPresence;
    bool gamepadMouse;
    bool detectNetworkBlocking;
    bool showPerformanceOverlay;
    bool swapMouseButtons;
    bool swapWinAltKeys;
    bool muteOnFocusLoss;
    bool backgroundGamepad;
    GamepadQuitCombo gamepadQuitCombo;

    // Stick deadzone in percent (0-30); 0 passes stick input through unmodified
    int gamepadDeadzone;
    bool reverseScrollDirection;
    bool swapFaceButtons;
    bool keepAwake;
    int packetSize;
    AudioConfig audioConfig;
    VideoCodecConfig videoCodecConfig;
    bool enableHdr;
    HdrMode hdrMode;
    HdrBrightnessMode hdrBrightnessMode;
    double hdrMaxBrightness;
    double hdrMinBrightness;
    double hdrMaxAverageBrightness;
    bool enableYUV444;
    VideoDecoderSelection videoDecoderSelection;
    WindowMode windowMode;
    WindowMode recommendedFullScreenMode;
    UIDisplayMode uiDisplayMode;
    bool rememberWindowPosition;
    Language language;
    CaptureSysKeysMode captureSysKeysMode;
    ScreenCombinationMode screenCombinationMode;
    bool enableMicrophone;
    OverlayMenuPosition overlayMenuPosition;
    bool autoUpdateCheck;
    RendererSelection rendererSelection;

#ifndef LITE_GPUI_WORKER
signals:
    void displayModeChanged();
    void bitrateChanged();
    void fecChanged();
    void fixedFecChanged();
    void autoAdjustBitrateChanged();
    void enableSunshineAbrChanged();
    void ignoreAspectRatioChanged();
    void enableVsyncChanged();
    void gameOptimizationsChanged();
    void playAudioOnHostChanged();
    void multiControllerChanged();
    void unsupportedFpsChanged();
    void enableMdnsChanged();
    void quitAppAfterChanged();
    void absoluteMouseModeChanged();
    void showLocalCursorChanged();
    void absoluteTouchModeChanged();
    void enableNativeTouchpadChanged();
    void dualSenseHapticsModeChanged();
    void audioConfigChanged();
    void videoCodecConfigChanged();
    void enableHdrChanged();
    void hdrModeChanged();
    void hdrBrightnessModeChanged();
    void hdrBrightnessValuesChanged();
    void enableYUV444Changed();
    void videoDecoderSelectionChanged();
    void uiDisplayModeChanged();
    void rememberWindowPositionChanged();
    void backgroundConfigurationChanged();
    void backgroundOverlayOpacityChanged();
    void backgroundSetupCompletedChanged();
    void windowModeChanged();
    void framePacingChanged();
    void videoEnhancementChanged();
    void streamResolutionScaleChanged();
    void streamResolutionScaleRatioChanged();
    void remoteResolutionChanged();
    void remoteResolutionWidthChanged();
    void remoteResolutionHeightChanged();
    void remoteFpsChanged();
    void remoteFpsRateChanged();
    void connectionWarningsChanged();
    void configurationWarningsChanged();
    void richPresenceChanged();
    void gamepadMouseChanged();
    void detectNetworkBlockingChanged();
    void showPerformanceOverlayChanged();
    void mouseButtonsChanged();
    void swapWinAltKeysChanged();
    void muteOnFocusLossChanged();
    void backgroundGamepadChanged();
    void gamepadQuitComboChanged();
    void gamepadDeadzoneChanged();
    void reverseScrollDirectionChanged();
    void swapFaceButtonsChanged();
    void captureSysKeysModeChanged();
    void keepAwakeChanged();
    void languageChanged();
    void screenCombinationModeChanged();
    void enableMicrophoneChanged();
    void overlayMenuPositionChanged();
    void autoUpdateCheckChanged();
    void rendererSelectionChanged();
#endif

private:
#ifdef LITE_GPUI_WORKER
    Q_DISABLE_COPY(StreamingPreferences)
    StreamingPreferences();
#else
    explicit StreamingPreferences(QQmlEngine *qmlEngine);
#endif

    QString getSuffixFromLanguage(Language lang);

    BackgroundSource m_BackgroundSource;
    QString m_BackgroundImageApi;
    QString m_BackgroundImageLocalPath;
    int m_BackgroundOverlayOpacity;
    bool m_BackgroundSetupCompleted;

#ifndef LITE_GPUI_WORKER
    QQmlEngine* m_QmlEngine;
#endif
};
