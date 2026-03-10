#pragma once

struct DJGame {
    const char* name;
    const char* defaultExeName;
    const char* id;
};

static DJGame djGames[] = {
    {"The 1st Tracks",                          "EZ2DJ.exe",          "ez2dj_1st"},
    {"The 1st Tracks Special Edition",          "EZ2DJ.exe",          "ez2dj_1st_se"},
    {"2nd Trax ~It rules once again~",          "EZ2DJ.exe",          "ez2dj_2nd"},
    {"3rd Trax ~Absolute Pitch~",               "EZ2DJ.exe",          "ez2dj_3rd"},
    {"4th Trax ~OVER MIND~",                    "EZ2DJ.exe",          "ez2dj_4th"},
    {"Platinum",                                "EZ2DJ.exe",          "ez2dj_pt"},
    {"6th Trax ~Self Evolution~",               "EZ2DJ-Launcher.exe", "ez2dj_6th"},
    {"7th Trax ~Resistance~",                   "EZ2DJ.exe",          "ez2dj_7th"},
    {"7th Trax Ver 1.5",                        "EZ2DJ.exe",          "ez2dj_7th_15"},
    {"7th Trax Ver 2.0",                        "EZ2DJ.exe",          "ez2dj_7th_20"},
    {"Codename: Violet",                        "EZ2DJ.exe",          "ez2dj_cv"},
    {"Bonus Edition",                           "EZ2DJBe.exe",        "ez2dj_be"},
    {"Bonus Edition revision A",                "EZ2DJBe.exe",        "ez2dj_be_a"},
    {"Azure ExpressioN",                        "EZ2DJ.exe",          "ez2dj_ae"},
    {"Azure ExpressioN Integral Composition",   "EZ2DJ.exe",          "ez2dj_ae_ic"},
    {"Endless Circulation",                     "EZ2AC.exe",          "ez2ac_ec"},
    {"Evolve",                                  "EZ2AC.exe",          "ez2ac_ev"},
    {"Night Traveller",                         "EZ2AC.exe",          "ez2ac_nt"},
    {"Time Traveller",                          "EZ2AC.exe",          "ez2ac_tt"},
    {"Final",                                   "EZ2AC.exe",          "ez2ac_fn"},
    {"Final:EX",                                "EZ2AC.exe",          "ez2ac_fn_ex"},
};

struct DancerGame {
    const char* name;
    const char* id;
};

static DancerGame dancerGames[] = {
    {"1st Move",    "ez2dancer_1st"},
    {"2nd Move",    "ez2dancer_2nd"},
    {"UK Move",     "ez2dancer_uk"},
    {"UK Move SE",  "ez2dancer_uk_se"},
    {"Super China", "ez2dancer_sc"},
};

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

static const char* dancerLightNames[] = {
    "Stage 1", "Stage 2", "Stage 3", "Stage 4",
};
