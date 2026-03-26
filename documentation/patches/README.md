# Creating Patches

Patches are a definition of offsets and values to write to game memory for creating simple mods or even large, extensive hacks. Each patch can either belong to a game directly or be "shared" between games, you can see a few of the 60hz patches are shared between multiple games in the patch.json file.

Basic format outline:
```json
"unique_id": {
  "name": "Display Name",
  "notes": "Technical notes, not read by config or dll.",
  "description": "Brief description shown to end users.",
  "type": "toggle",
  "apply": "early",
  "scan": "8b 45 ?? 89 46 ??",
  "writes": [
    { "offset": "0x4", "bytes": "6a 3c 90 90" }
  ]
}
```

| Field         | Type     | Required    | Description                                                                                                                                    |
| ------------- | -------- | ----------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `name`        | string   | yes         | Display name shown in the config UI                                                                                                            |
| `notes`       | string   | no          | Technical notes for patch authors, ignored by config and dll                                                                                   |
| `description` | string   | no          | Brief description shown to end users                                                                                                           |
| `games`       | string[] | shared only | Game IDs this patch applies to, only used under `"shared"`                                                                                     |
| `type`        | string   | yes         | `"toggle"` or `"value"`                                                                                                                        |
| `apply`       | string   | no          | `"super_early"` (before app executes), `"early"` (~10ms delay), or omit for default (~2s delay, good for patches that need initial game setup) |
| `scan`        | string   | no          | Space-separated hex bytes for pattern scanning, `??` for wildcard                                                                              |
| `writes`      | array    | toggle only | Array of `{ "offset", "bytes" }` objects. Offsets are RVA unless `scan` is present, then relative to match                                     |
| `offset`      | string   | value only  | Single hex offset to write the selected value to                                                                                               |
| `options`     | string[] | value only  | Dropdown options shown in UI, selected index is written to `offset`                                                                            |
| `default`     | int      | no          | Default selected index for value patches                                                                                                       |
| `children`    | object   | no          | Nested child patches, only applied when parent is enabled                                                                                      |

### User Patches

Users can create custom patches by placing a `user-patches.json` file in the game directory (same folder as the game executable). The format is the same as `patches.json` but without the `"ver"` or `"shared"` keys.

If a user patch has the same id as a bundled patch, the user patch replaces it. Otherwise it's added as a new patch.

```json
{
  "ez2ac_ev": {
    "my_custom_patch": {
      "name": "My Custom Patch",
      "description": "Does something cool",
      "type": "toggle",
      "writes": [
        { "offset": "0x1234", "bytes": "90 90" }
      ]
    }
  }
}
```

### Types
Patches come in 2 formats, RVA offsets or Patterns.

#### RVA Offset: 

Relative Virtual address to where the game is loaded in memory. If a game EXE loads at 0x00400000 and a function is at virtual address 0x00401234, its RVA is 0x1234.

```json
"force_60hz": {
  "name": "Force 60Hz",
  "description": "Forces DirectDraw SetDisplayMode to request 60Hz.",
  "type": "toggle",
  "apply": "early",
  "writes": [
    { "offset": "0x149CA", "bytes": "6a 3c" }
  ]
}
```

#### Pattern Scan:

Offsets in a pattern patch are relative to the matched location.
```json
"force_60hz_v2": {
  "name": "Force 60Hz",
  "notes": "<notes describing technicalities of the patch, not read by either config or dll>",
  "description": "Forces DirectDraw SetDisplayMode to request 60Hz",
  "type": "toggle",
  "apply": "early",
  "scan": "52 8b 45 08 8b 48 ?? 51 8b 55 08 8b 42 ?? 50 8b 8d 58 ff ff ff 8b 51 ?? 52",
  "writes": [
    { "offset": "0x4", "bytes": "6a 3c 90 90" }
  ]
},
```

#### Value Type

Value patches write a single byte (the index) selected from a list of options. The options are presented as a dropdown in the config UI. Uses a single `offset` field instead of the `writes` array. The index of the selected option is written to the offset.

```json
"keep_note_skin": {
  "name": "Keep Note Skin",
  "description": "Persist the selected note skin across sessions.",
  "type": "value",
  "offset": "0x33D0EC4",
  "options": ["Default", "2nd", "1st SE", "Simple", "Steel", "3S", "3S RB", "Circle", "Disc", "Star", "Turtle", "Gem"],
  "default": 0
}
```

### Shared Patches

Patches that apply to multiple games can be defined under the top-level `"shared"` key. Add a `"games"` array listing the game IDs the patch should apply to.

```json
{
  "shared": {
    "force_60hz_v1": {
      "name": "Force 60Hz",
      "games": ["ez2dj_6th", "ez2dj_7th", "ez2dj_cv"],
      "description": "Forces 60Hz refresh rate",
      "type": "toggle",
      "apply": "early",
      "scan": "8b 56 28 53 8b 5f 18 ...",
      "writes": [
        { "offset": "0x4", "bytes": "6a 3c 5b" }
      ]
    }
  }
}
```

### Children

Patches can contain nested child patches. Children are toggled independently in the UI but only applied when the parent patch is also enabled.

```json
"parent_patch": {
  "name": "Parent",
  "type": "toggle",
  "writes": [
    { "offset": "0x100", "bytes": "90" }
  ],
  "children": {
    "child_a": {
      "name": "Option A",
      "type": "toggle",
      "writes": [
        { "offset": "0x200", "bytes": "01" }
      ]
    }
  }
}
```

### Apply Timing

The `"apply"` field controls when the patch is written to memory:

| Value           | Timing                          | Use case                                                         |
| --------------- | ------------------------------- | ---------------------------------------------------------------- |
| *(omitted)*     | ~2 seconds after boot           | Patches that need the game to finish initial setup first         |
| `"early"`       | ~10ms after boot                | Patches that need to be applied before the game starts rendering |
| `"super_early"` | Before the application executes | Patches that must be in place before any game code runs          |

### Game IDs

| ID                | Game                  |
| ----------------- | --------------------- |
| `ez2dj_1st`       | EZ2DJ 1st             |
| `rmbr_1st`        | EZ2DJ Remember 1st    |
| `ez2dj_1st_se`    | EZ2DJ 1st SE          |
| `ez2dj_2nd`       | EZ2DJ 2nd             |
| `ez2dj_3rd`       | EZ2DJ 3rd             |
| `ez2dj_4th`       | EZ2DJ 4th             |
| `ez2dj_5th`       | EZ2DJ 5th             |
| `ez2dj_6th`       | EZ2DJ 6th             |
| `ez2dj_7th`       | EZ2DJ 7th             |
| `ez2dj_7th_15`    | EZ2DJ 7th 1.5         |
| `ez2dj_7th_20`    | EZ2DJ 7th 2.0         |
| `ez2dj_cv`        | EZ2DJ CV              |
| `ez2dj_be`        | EZ2DJ BE              |
| `ez2dj_be_a`      | EZ2DJ BE-A            |
| `ez2dj_ae`        | EZ2DJ AE              |
| `ez2dj_ae_ic`     | EZ2DJ AE IC           |
| `ez2ac_ec`        | EZ2AC EC              |
| `ez2ac_ev`        | EZ2AC EV              |
| `ez2ac_nt`        | EZ2AC NT              |
| `ez2ac_tt`        | EZ2AC TT              |
| `ez2ac_fn`        | EZ2AC FN              |
| `ez2ac_fn_ex`     | EZ2AC FN EX           |
| `ez2dancer_1st`   | EZ2Dancer 1st Move    |
| `ez2dancer_2nd`   | EZ2Dancer 2nd Move    |
| `ez2dancer_uk`    | EZ2Dancer UK Move     |
| `ez2dancer_uk_se` | EZ2Dancer UK Move SE  |
| `ez2dancer_sc`    | EZ2Dancer Super China |
