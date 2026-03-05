#pragma once

static struct djGame {
    const char* name;
    const char* defaultExeName;
    const char* id;
} djGames[] = {
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
    {"Evolve (Win98)",                          "EZ2AC.exe",          "ez2ac_ev_w98"},
    {"Evolve",                                  "EZ2AC.exe",          "ez2ac_ev"},
    {"Night Traveller",                         "EZ2AC.exe",          "ez2ac_nt"},
    {"Time Traveller (1.83)",                   "EZ2AC.exe",          "ez2ac_tt"},
    {"Final",                                   "EZ2AC.exe",          "ez2ac_fn"},
    {"Final:EX",                                "EZ2AC.exe",          "ez2ac_fn_ex"},
};

static struct dancerGame {
    const char* name;
    const char* id;
} dancerGames[] = {
    {"1st Move",    "ez2dancer_1st"},
    {"2nd Move",    "ez2dancer_2nd"},
    {"UK Move",     "ez2dancer_uk"},
    {"UK Move SE",  "ez2dancer_uk_se"},
    {"Super China", "ez2dancer_sc"},
};

static const char* ioButtons[] = {
    "Test", "Service",
    "Effector 1", "Effector 2", "Effector 3", "Effector 4",
    "P1 Start", "P2 Start",
    "P1 1", "P1 2", "P1 3", "P1 4", "P1 5", "P1 Pedal",
    "P2 1", "P2 2", "P2 3", "P2 4", "P2 5", "P2 Pedal",
};

static const char* analogs[] = {
    "P1 Turntable",
    "P2 Turntable",
};

static const char* lights[] = {
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

static const char* ez2DancerIOButtons[] = {
    "Test", "Service",
    "P1 Left", "P1 Centre", "P1 Right",
    "P2 Left", "P2 Centre", "P2 Right",
    "P1 L Sensor Top", "P1 L Sesor Bottom",
    "P1 R Sensor Top", "P1 R Sesor Bottom",
    "P2 L Sensor Top", "P2 L Sesor Bottom",
    "P2 R Sensor Top", "P2 R Sesor Bottom",
};

static const char* ez2DancerLights[] = {
    "Stage 1", "Stage 2", "Stage 3", "Stage 4",
};

static const char* noteSkins[] = {
    "Default", "2nd", "1st SE", "Simple", "Steel",
    "3S", "3S RB", "Circle", "Disc", "Star", "Turtle", "Gem",
};

static const char* panelSkins[] = {
    "FN", "TT Blue", "TT Green", "NT", "EV", "EC", "AEIC",
    "3S", "CV", "7th", "6th", "5th", "4th",
    "3rd - Silver", "3rd - Gold", "3rd - Green",
    "2nd - Silver", "2nd - Green", "2nd - Black",
    "1st SE", "1st",
};

static const char* VisualSettings[] = {
    "Default", "Black Panel", "BGA Off",
};
