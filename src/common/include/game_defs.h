#pragma once

#include <string>

// ============================================================================
// Game families — determines which IO handler the DLL uses
// ============================================================================
enum class GameFamily { EZ2DJ, EZ2Dancer, SabinSS };

struct Game {
    const char* name;
    const char* defaultExeName;
    const char* id;
    GameFamily family;
};

static Game games[] = {
    // EZ2DJ / EZ2AC
    {"The 1st Tracks",                          "EZ2DJ.exe",          "ez2dj_1st",    GameFamily::EZ2DJ},
    {"The 1st Tracks Special Edition",          "EZ2DJ.exe",          "ez2dj_1st_se", GameFamily::EZ2DJ},
    {"2nd Trax ~It rules once again~",          "EZ2DJ.exe",          "ez2dj_2nd",    GameFamily::EZ2DJ},
    {"3rd Trax ~Absolute Pitch~",               "EZ2DJ.exe",          "ez2dj_3rd",    GameFamily::EZ2DJ},
    {"4th Trax ~OVER MIND~",                    "EZ2DJ.exe",          "ez2dj_4th",    GameFamily::EZ2DJ},
    {"Platinum",                                "EZ2DJ.exe",          "ez2dj_pt",     GameFamily::EZ2DJ},
    {"6th Trax ~Self Evolution~",               "EZ2DJ-Launcher.exe", "ez2dj_6th",    GameFamily::EZ2DJ},
    {"7th Trax ~Resistance~",                   "EZ2DJ.exe",          "ez2dj_7th",    GameFamily::EZ2DJ},
    {"7th Trax Ver 1.5",                        "EZ2DJ.exe",          "ez2dj_7th_15", GameFamily::EZ2DJ},
    {"7th Trax Ver 2.0",                        "EZ2DJ.exe",          "ez2dj_7th_20", GameFamily::EZ2DJ},
    {"Codename: Violet",                        "EZ2DJ.exe",          "ez2dj_cv",     GameFamily::EZ2DJ},
    {"Bonus Edition",                           "EZ2DJBe.exe",        "ez2dj_be",     GameFamily::EZ2DJ},
    {"Bonus Edition revision A",                "EZ2DJBe.exe",        "ez2dj_be_a",   GameFamily::EZ2DJ},
    {"Azure ExpressioN",                        "EZ2DJ.exe",          "ez2dj_ae",     GameFamily::EZ2DJ},
    {"Azure ExpressioN Integral Composition",   "EZ2DJ.exe",          "ez2dj_ae_ic",  GameFamily::EZ2DJ},
    {"Endless Circulation",                     "EZ2AC.exe",          "ez2ac_ec",     GameFamily::EZ2DJ},
    {"Evolve",                                  "EZ2AC.exe",          "ez2ac_ev",     GameFamily::EZ2DJ},
    {"Night Traveller",                         "EZ2AC.exe",          "ez2ac_nt",     GameFamily::EZ2DJ},
    {"Time Traveller",                          "EZ2AC.exe",          "ez2ac_tt",     GameFamily::EZ2DJ},
    {"Final",                                   "EZ2AC.exe",          "ez2ac_fn",     GameFamily::EZ2DJ},
    {"Final:EX",                                "EZ2AC.exe",          "ez2ac_fn_ex",  GameFamily::EZ2DJ},
    // EZ2Dancer
    {"1st Move",    "EZ2Dancer.exe", "ez2dancer_1st",    GameFamily::EZ2Dancer},
    {"2nd Move",    "EZ2Dancer.exe", "ez2dancer_2nd",    GameFamily::EZ2Dancer},
    {"UK Move",     "EZ2Dancer.exe", "ez2dancer_uk",     GameFamily::EZ2Dancer},
    {"UK Move SE",  "EZ2Dancer.exe", "ez2dancer_uk_se",  GameFamily::EZ2Dancer},
    {"Super China", "EZ2Dancer.exe", "ez2dancer_sc",     GameFamily::EZ2Dancer},
    // Sabin Sound Star
    {"Sabin Sound Star: Renascence Burst", "3s.exe", "3s_rb", GameFamily::SabinSS},
};

static constexpr int GAME_COUNT = sizeof(games) / sizeof(games[0]);

static GameFamily familyFromGameId(const std::string& id) {
    for (int i = 0; i < GAME_COUNT; i++) {
        if (id == games[i].id) return games[i].family;
    }
    return GameFamily::EZ2DJ; // default
}

static int gameIndexFromId(const std::string& id) {
    for (int i = 0; i < GAME_COUNT; i++) {
        if (id == games[i].id) return i;
    }
    return 0;
}

// ============================================================================
// EZ2DJ buttons, analogs, lights
// ============================================================================
enum class DJButton {
    TEST = 0, SERVICE,
    EFFECTOR_1, EFFECTOR_2, EFFECTOR_3, EFFECTOR_4,
    P1_START, P2_START,
    P1_1, P1_2, P1_3, P1_4, P1_5, P1_PEDAL,
    P2_1, P2_2, P2_3, P2_4, P2_5, P2_PEDAL,
    COUNT,
};

static const char* djButtonNames[] = {
    "Test", "Service",
    "Effector 1", "Effector 2", "Effector 3", "Effector 4",
    "P1 Start", "P2 Start",
    "P1 1", "P1 2", "P1 3", "P1 4", "P1 5", "P1 Pedal",
    "P2 1", "P2 2", "P2 3", "P2 4", "P2 5", "P2 Pedal",
};

enum class Analog {
    P1_TURNTABLE = 0,
    P2_TURNTABLE,
    COUNT,
};

static const char* analogNames[] = {
    "P1 Turntable",
    "P2 Turntable",
};

enum class Light {
    EFFECTOR_1, EFFECTOR_2, EFFECTOR_3, EFFECTOR_4,
    P1_START, P2_START,
    P1_TURNTABLE,
    P1_1, P1_2, P1_3, P1_4, P1_5,
    P2_TURNTABLE,
    P2_1, P2_2, P2_3, P2_4, P2_5,
    NEONS,
    RED_LAMP_L, RED_LAMP_R, BLUE_LAMP_L, BLUE_LAMP_R,
    COUNT,
};

static const char* lightNames[] = {
    "Effector 1", "Effector 2", "Effector 3", "Effector 4",
    "P1 Start", "P2 Start",
    "P1 Turntable",
    "P1 1", "P1 2", "P1 3", "P1 4", "P1 5",
    "P2 Turntable",
    "P2 1", "P2 2", "P2 3", "P2 4", "P2 5",
    "Neons",
    "Red Lamp L", "Red Lamp R",
    "Blue Lamp L", "Blue Lamp R",
};

// ============================================================================
// EZ2Dancer buttons, lights
// ============================================================================
enum class DancerButton {
    TEST = 0, SERVICE,
    P1_LEFT, P1_CENTRE, P1_RIGHT,
    P2_LEFT, P2_CENTRE, P2_RIGHT,
    P1_L_SENSOR_TOP, P1_L_SENSOR_BOT,
    P1_R_SENSOR_TOP, P1_R_SENSOR_BOT,
    P2_L_SENSOR_TOP, P2_L_SENSOR_BOT,
    P2_R_SENSOR_TOP, P2_R_SENSOR_BOT,
    COUNT,
};

static const char* dancerButtonNames[] = {
    "Test", "Service",
    "P1 Left", "P1 Centre", "P1 Right",
    "P2 Left", "P2 Centre", "P2 Right",
    "P1 L Sensor Top", "P1 L Sensor Bottom",
    "P1 R Sensor Top", "P1 R Sensor Bottom",
    "P2 L Sensor Top", "P2 L Sensor Bottom",
    "P2 R Sensor Top", "P2 R Sensor Bottom",
};

enum class DancerLight {
    // 0x30A cabinet lights (7)
    NEON, LIGHT_LEFT_TOP, LIGHT_LEFT_MIDDLE, LIGHT_LEFT_BOTTOM,
    LIGHT_RIGHT_TOP, LIGHT_RIGHT_MIDDLE, LIGHT_RIGHT_BOTTOM,
    // 0x30C (Hand LEDs) and 0x308 (Pads) require further research.
    COUNT,
};

static const char* dancerLightNames[] = {
    "Neon",
    "Spotlight Left Top", "Spotlight Left Middle", "Spotlight Left Bottom",
    "Spotlight Right Top", "Spotlight Right Middle", "Spotlight Right Bottom",
};

// ============================================================================
// Sabin Sound Star buttons, lights, commands
// ============================================================================
enum class SabinButton {
    TEST = 0, SERVICE, COIN, BILL,
    P1_BTN0, P1_BTN1, P1_BTN2, P1_BTN3, P1_BTN4,
    P1_BTN5, P1_BTN6, P1_BTN7, P1_BTN8, P1_PEDAL,
    P2_BTN0, P2_BTN1, P2_BTN2, P2_BTN3, P2_BTN4,
    P2_BTN5, P2_BTN6, P2_BTN7, P2_BTN8, P2_PEDAL,
    COUNT,
};

static const char* sabinButtonNames[] = {
    "Test", "Service", "Coin", "Bill",
    "P1 Start", "P1 B1", "P1 B2", "P1 B3", "P1 B4", "P1 B5",
    "P1 Red", "P1 Green", "P1 Blue", "P1 Pedal",
    "P2 Start", "P2 B1", "P2 B2", "P2 B3", "P2 B4", "P2 B5",
    "P2 Red", "P2 Green", "P2 Blue", "P2 Pedal",
};

static const char* sabinButtonCommands[] = {
    "Tet", "Svce", "Coin", "Bill",
    "S10", "S11", "S12", "S13", "S14",
    "S15", "S16", "S17", "S18", "F10",
    "S20", "S21", "S22", "S23", "S24",
    "S25", "S26", "S27", "S28", "F20",
};

enum class SabinLight {
    // Button LEDs
    P1_LED0 = 0, P1_LED1, P1_LED2, P1_LED3, P1_LED4,
    P1_LED5, P1_LED6, P1_LED7, P1_LED8,
    P2_LED0, P2_LED1, P2_LED2, P2_LED3, P2_LED4,
    P2_LED5, P2_LED6, P2_LED7, P2_LED8,
    // RGB zones (C=Control, M=Middle, B=Bottom, T=Top)
    CTRL_RED, CTRL_GREEN, CTRL_BLUE,
    MID_RED, MID_GREEN, MID_BLUE,
    BOT_RED, BOT_GREEN, BOT_BLUE,
    TOP_RED, TOP_GREEN, TOP_BLUE,
    // Neon/fluorescent
    NEON_1, NEON_2,
    COUNT,
};

static const char* sabinLightNames[] = {
    "P1 LED 0", "P1 LED 1", "P1 LED 2", "P1 LED 3", "P1 LED 4",
    "P1 LED 5", "P1 LED 6", "P1 LED 7", "P1 LED 8",
    "P2 LED 0", "P2 LED 1", "P2 LED 2", "P2 LED 3", "P2 LED 4",
    "P2 LED 5", "P2 LED 6", "P2 LED 7", "P2 LED 8",
    "Control Red", "Control Green", "Control Blue",
    "Middle Red", "Middle Green", "Middle Blue",
    "Bottom Red", "Bottom Green", "Bottom Blue",
    "Top Red", "Top Green", "Top Blue",
    "Neon 1", "Neon 2",
};

static const char* sabinLightCommands[] = {
    "L10", "L11", "L12", "L13", "L14",
    "L15", "L16", "L17", "L18",
    "L20", "L21", "L22", "L23", "L24",
    "L25", "L26", "L27", "L28",
    "LCR", "LCG", "LCB",
    "LMR", "LMG", "LMB",
    "LBR", "LBG", "LBB",
    "LTR", "LTG", "LTB",
    "L30", "L31",
};
