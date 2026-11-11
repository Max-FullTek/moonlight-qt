#pragma once

enum GamepadUiStyle
{
    GamepadUiStyleXbox = 0,
    GamepadUiStylePlayStation = 1,
    GamepadUiStyleNintendo = 2,
};

// Values are also used by the persisted preferences and Qt frontend.
struct InputOptions
{
    enum CaptureSysKeysMode {
        CSK_OFF = 0,
        CSK_FULLSCREEN = 1,
        CSK_ALWAYS = 2,
    };
    enum GamepadQuitCombo {
        GQC_DEFAULT = 0,
        GQC_SELECT_L1_R1_X = 1,
        GQC_SELECT_L1_R1_Y = 2,
        GQC_START_L1_R1_A = 3,
        GQC_START_L1_R1_B = 4,
        GQC_L1_R1_X_Y = 5,
        GQC_L1_R1_A_B = 6,
    };

    bool multiController;
    bool gamepadMouse;
    bool swapMouseButtons;
    bool swapWinAltKeys;
    bool reverseScrollDirection;
    bool swapFaceButtons;
    GamepadQuitCombo gamepadQuitCombo;
    int gamepadDeadzone;
    CaptureSysKeysMode captureSysKeysMode;
    bool showLocalCursor;
    bool absoluteMouseMode;
    bool absoluteTouchMode;
    bool backgroundGamepad;
};
