# EZ2Config V2 ~ it rules once again ~
A Complete Rewrite of the orginal [2EZConfig](https://github.com/ben-rnd/2EZConfig).

This tool is designed to enable USB controllers to work with all versions of EZ2DJ/AC, as well as offering various patches that make the home experience more enjoyable. 

## Features
- Supports all EZ2DJ/AC and EZ2Dancer games compatible with Windows XP or newer.
- Full IO Emualtion. Feels just like the real thing.
- JSON defined patch library, supporting basic RVA offset or pattern scan patches. Users can create their own patches in a user-patches.json file located in the game directory which will load the next time you open 2EZConfig. Please see the readme under the patches documentation folder for details about the patching system and how to define your own patches.
- 60hz patches for (almost)all games. Now you never have to manually switch your refresh rate again. The game will request a 60hz refresh rate on boot, reverting once the game closes. 60hz is essential for accurate game timing.
- Arcade hardware compatible. Did your IO die on you? 2EZconfig V2 is fully compatible with Windows XP SP3 and the NVIDIA TNT2. IO emulation can be fully disabled if youre only after the patches.
- HID input and output, supporting lighting output from EZ2DJ. EZ2Dancer light support is pending port mappings being supplied or reverse engineered.
- Supports Button Bound TT+ and TT- and Mouse X/Y axis turntables for maximum compatibilty with DIY cons.
- On first boot, 2EZConfig will automatically detect which game youre running by scanning for exe's.
- "Skip UI" option to boot directly into game once settings are set. Great for arcade like setups.
- Multi DLL injection. If youve got your own EZ2 mods, 2EZconfig can inject them all in one place.
- Support for 6th Trax "Remember 1st" mode. This mode is accesed by the game closing itself and opening an entirely different executable. 2EZConfig handles this without a hitch.

## Setup 

1) Simply copy 2EZConfig.exe and 2EZ.dll into the root of your game directory. On first boot 2EZConfig will try and detect the game automatically, if this fails please select the game manually.
2) Setup your bindings. Bindings are a global setting, all other instances of 2EZConfig will share the same input and output bindings.
3) Press "Play EZ2" to boot the game. 

Enjoy!

If you're playing on Windows Vista or later I highly reccomend using [DDrawCompat](https://github.com/narzoul/DDrawCompat/releases). This fixes various DDraw related crashes as well as improving the games visuals dramatically, by fixing various transparancy issues that occur on newer GPU's. 

#### Notes
- Do not put any "." characters in any folders containing your EZ2 data, this breaks how to game reads its .ini configurations files and may prevent the game from working at all. When the game launches, it will scan the entire path and break on the first "." detected, if this is NOT the .ini file it will load defualt settings which often make the game unplayable.

## Get Involved

I will accept PR's for updates to the patches.json file if they seem appropriate. 

## Building
All builds are done on windows with cmake and niXman mingw32 gcc 12.2.0 toolchain

Download toolchain and Dependancies
```cmake --preset release```

Build
```cmake --build --preset release```

## Target Platform
- **Windows XP SP3** and later

## Raising Bugs and Feature Requests
Please use this repo to raise feature and bug requests. When reaising a bug or feature request please use the provided templates. Any issues raised with too little detail will be deleted and ignored.

## Support
I would prefer code and patch contributions but I often get asked anyway.
So if you cant write code but want to help support me? [buy me a coffee](https://buymeacoffee.com/kasaski)
