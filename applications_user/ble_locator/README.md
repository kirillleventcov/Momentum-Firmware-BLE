# BLE Locator

Groups and finds Bluetooth LE devices by what they broadcast. Walk up to a
handful of devices that belong together, capture them, and the app keeps only
the traits they all share. From then on it recognises devices of that kind it
has never seen before, and homes in on them by signal strength.

Nothing about this is specific to one product. Anything that advertises over
BLE can be grouped: rental scooters and their IoT modules, asset tags, a fleet
of identical headsets, or every Apple device in a room.

**No groups ship with the app.** It starts empty and knows nothing until
someone walks up to real hardware and teaches it. A guessed fingerprint written
by someone who has never seen your devices is worse than no fingerprint — it
looks like it works until the day it quietly does not.

Built as a proof of concept for field crews — locating hardware that has no
working cellular link, GPS or backend to ask.

## What it can and cannot do

- **Can**: tell you a member of a group is within roughly 10–30 m, and get you
  to it by signal strength. Works with no cellular, no GPS and no backend.
- **Cannot**: tell you which *direction* it is in. BLE gives distance-ish, never
  bearing. Finding something means walking and watching the trend, the way a
  metal detector works.
- **Cannot**: find a device that is not transmitting. If its radio is dead,
  nothing can hear it.

## Hardware requirement — read this first

The stock Flipper ships the **light** BLE co-processor stack
(`stm32wb5x_BLE_Stack_light_fw.bin`), which ST describes as "Slave Only": it can
advertise but its link layer has **no scanner at all**. No app can work around
that.

This branch therefore builds against the **full** stack. `fbt_options.py`:

```python
COPRO_STACK_BIN = "stm32wb5x_BLE_Stack_full_fw.bin"
COPRO_STACK_TYPE = "ble_full"
```

Build and install a firmware package the normal way:

```sh
./fbt updater_package          # -> dist/f7-C/f7-update-.../
```

Install it with qFlipper or by copying the update folder to the SD card and
running it from **Settings → Firmware Update**. The updater swaps the radio
stack automatically; expect an extra reboot or two.

### Flash budget — worth knowing before you add features

The full stack is installed 36 KiB lower in flash than the light one
(`0x080CE000` vs `0x080D7000`), so it takes that much away from the firmware
image. On this branch the firmware ends up **~4 KB short of the radio region**.
That fits, and `./fbt updater_package` refuses to build a package whose firmware
would overlap the radio, so you cannot ship a broken one by accident — but there
is not much room left. If a future change pushes it over, either drop a built-in
application or keep `COMPACT = 1` / `DEBUG = 0` (a debug build will not fit).

To confirm afterwards: **Settings → System → About → Firmware** shows the radio
stack type, or run `bt info` over the CLI. If the light stack is still
installed, the app opens on an explanatory screen instead of the menu.

The app itself is an ordinary external FAP:

```sh
./fbt fap_ble_locator      # -> build/f7-firmware-C/.extapps/ble_locator.fap
./fbt launch APPSRC=applications_user/ble_locator   # build, upload and run
```

## Verified on hardware

Bench-tested on a Flipper Zero running this branch with the full radio stack
(`radio_stack_type: 1`). What was confirmed end to end, on live radio:

- Advertising reports arrive and are parsed byte-accurately. A real Apple
  advert (`02 01 1A 02 0A 0C 0B FF 4C 00 10 06 …`) was decoded to exactly
  Flags / TX Power 12 dBm / manufacturer 0x004C with an 8-byte payload.
- Active scanning works: scan responses come back and merge into the same
  device record (`RSP (21)` alongside `ADV (18)` for one device).
- Learning from 3 captures produced a group keyed on manufacturer id plus
  payload shape, which then matched **13 of 16** devices in range and correctly
  rejected the other 3 — the exact group-detection behaviour, on real traffic.
- That same over-broad group is now caught at build time: the review screen
  reported **TOO BROAD, 7 of 10 other devices also match**, rather than saving
  a fingerprint that would list half the street.
- Group filtering verified both ways: pointed at a group with no members
  present the list stayed **empty** while the radio kept seeing 13 devices;
  pointed at the learned group it listed **10**.
- Non-matching devices score 0% against every group, confirming the
  "absence is not contradiction" and "shape alone is never enough" rules.
- Groups survive a round trip through `groups.txt`: after a cold restart of
  the app the learned group reloaded from SD and produced the same 13 hits.
- The homing screen tracks a live target (signal, distance band, trend, peak,
  packets/s).
- Bluetooth is healthy before, during and after a scan session, and on the
  light stack the app falls back to its explanatory screen instead of failing.

- After the rename to BLE Locator, re-verified on device: the new menu, `ALL 12
  seen 10 hit` in scan-all mode, `Fleet 4  9` in group mode, an empty
  `Nordic UART module  0` screen with "No group members in range", the learn
  header, and migration of an existing `scooter_locator/profiles.txt` into
  `ble_locator/groups.txt`.

Not yet exercised in the field: real rental scooters. The groups used above
were consumer BLE devices standing in for them; a real device group usually
shares both a manufacturer id *and* a protocol header, which yields a tighter
fingerprint than the one above.

## The three modes

The main menu is the whole model of the app:

| Menu entry | What it does |
| --- | --- |
| **Scan all devices** | Everything on air, strongest first, with anything matching a known group labelled and scored. This is the analysis mode — use it to see what is actually around you. |
| **Find a group** | Pick one group (or **Any group**) and the screen stays empty until a member comes into range. This is the walk-past mode. |
| **Learn a group** | Capture devices that belong together and build a group from what they share. |
| **Groups** | Add devices to a group, remove members, inspect, rename, enable/disable and delete. |

Which mode you are in is always an explicit choice made at the menu, and the
list header says so: `ALL 12 seen 10 hit`, `Fleet 4  9`, `LEARN captured 2/8`.

## The list holds still

Sorting live by signal strength makes a list you cannot aim at: rows trade
places between ticks and whatever you were reaching for has moved. So they
don't.

Every scan opens with a **settle window** — a few seconds of `Scanning
candidates` with the UI blocked. Stand where you want to look from and let it
run. When it closes, whatever it heard is laid out closest-first and **those
rows keep their positions**. From then on only the numbers on them change: walk
away and the top row's signal falls, but it stays the top row.

Devices heard for the first time *after* the window are added rather than
ignored — walking into range of something new has to show it. A newcomer is
placed immediately above the first row weaker than it, which shifts what is
below by one. That is the only movement left, and it is the movement you want.

A held order is not a sorted one: walk far enough and the numbers will no longer
descend down the screen. That is the trade, and it is the right way round —
`Hold Left` re-runs the settle and re-sorts from wherever you are now, without
leaving the screen or losing captures.

Two things still move rows on their own, both deliberate:

- A device silent for 30 s drops off, shifting what is below it up. The age
  counter on each row tells you when that is about to happen.
- Coming back from the homing or details screen does **not** re-settle; the
  order you were looking at is still there.

`Settings > Settle time` sets the window, 3–10 s, default 5.

## Field workflow

### Learn a group (once per kind of device)

1. **Learn a group** from the menu.
2. Stand next to a device you know is yours — within a metre or so. It should
   be at or near the top of the list, which is sorted purely by signal strength
   in this mode.
3. **OK** captures it (buzz + green blink). A `*` marks captured rows.
4. Repeat on **3–5 different devices of the same group**. This matters: the
   group keeps only the traits every capture shares, so more captures means a
   sharper fingerprint and fewer false hits. One capture works but is blunter.
5. **Right** builds the group. You do not need all of them today — one is
   enough to start, and `Groups > Add device` takes more later. Before you name
   it, the app grades it against
   every other device currently on air (see *Selectivity* below) and tells you
   whether it will actually show only your group.
6. **OK** to name and save. The app drops straight into **Find a group**
   pointed at the group you just made, so you can sanity check it on the spot.

**Left** removes the last capture. **Hold Left** re-runs the settle so the list
re-sorts around where you are standing now — useful once you have walked from
one unit to the next, since the frozen order will not float the nearest one back
to the top on its own. **Hold OK** on any row shows the raw advertisement and
how every group scores it — the fastest way to see why something is or is not
matching.

If the app refuses to build, the captures had nothing distinctive in common.
Capture more devices, or different ones.

### Building a group over time

Learning in one sitting only works if every device you want is in front of you
at once. Usually it isn't — they're spread across a site, or you meet one more
next Tuesday. So a group **keeps the devices it was built from** and can be
added to later.

`Groups` → pick one:

| | |
| --- | --- |
| **Add device** | Runs a normal scan showing everything (the device you are adding is by definition one the group does not match yet). Pick it with OK. |
| **Members (N)** | The devices the group was built from. Open one to see its advertisement and **Remove** it. |
| **Details** | What the group keys on and its threshold. **Left/Right** move the threshold by 5 % for this group alone, unlike Match mode which shifts every group. |
| **Retest here** | Re-intersects the stored members and tightens the group against whatever is on air where you stand now, then shows the same review screen as a build. Use it to check a group learned somewhere quiet once you are somewhere busy. Back discards, Save keeps the retuned threshold. |
| **Rename** | Give the group a new name. Names must be unique — the keyboard refuses a name another group already has, or a blank one. Members and the group lock follow the rename. |

Adding or removing re-intersects the whole member set into a fresh fingerprint
and puts it through the same review screen as a first-time build, so you see
what the change did — a wrong device widening the group shows up as **TOO
BROAD** and Back discards it. Nothing is written until you press Save.

A group needs at least one member, and rebuilds that leave nothing identifying
are refused with the member put back.

Members live in `members/<id>.txt`, one file per group, as the advertisements
themselves rather than parsed fields — loading them runs the same parser as live
radio, so a stored member and a heard one cannot drift apart. Copying a group
to another Flipper means copying its member file alongside `groups.txt`.

Groups learned before this existed have no stored members: their `Members` list
reads `(none stored)` and starts from the first device you add. Their existing
fingerprint keeps working in the meantime.

### Selectivity: only your group, nothing else

The intersection alone can produce a fingerprint that is technically correct
and practically useless — if the only thing your captures share is "made by
vendor X", every other gadget from vendor X matches too.

So the build does not stop at the intersection. Everything on air that you did
*not* capture is treated as a negative set, and the group's match threshold
is raised to sit just above the best-scoring impostor, as far as that is
possible without shutting out the captures themselves. You then get a verdict:

```
Keys on: mfr+silent+shape
From 3 captures

SELECTIVE
0 of 42 other devices
around you match.
```

or, when no such gap exists:

```
TOO BROAD
7 of 10 other devices
around you also match.
```

A **TOO BROAD** group is not a bug — it is the honest answer that these units
advertise nothing distinguishing them from their surroundings. Back out and
capture a different mix, or learn somewhere with less around. Real groups
normally share both an OEM manufacturer id *and* a protocol header, which
separates cleanly.

Note the negative set is only what was on air *where you learned*. A group
built in an empty car park is untested; the review says so.

### Find a group

**Menu > Find a group** asks which group first, then scans. Nothing outside
that group is ever listed, so the screen stays empty as you walk and a row
appears only when a member comes into range. The header names the group and
counts the hits:

```
Nordic UART module  0     <- devices on air, none of them yours
Fleet 4  9                <- nine of them are
```

A member coming into range **buzzes and blinks green** (the Beeper and
Vibration settings apply), so the Flipper can stay in a pocket while you walk.
A device that dropped off the list and comes back announces itself again.

**Any group** is still a filter: it hides everything the app has never been
taught, it just does not care which group a hit belongs to. Use it when you
have several groups and want all of them at once.

If a group is deleted while it is the active one, the lock releases itself
rather than leaving you with a permanently blank screen.

1. The list shows matches strongest first, with the match confidence and how
   many seconds since the last packet.
2. **OK** opens the homing screen: a large signal reading, a coarse distance
   band, a CLOSER/FURTHER trend, a peak marker and a Geiger-counter beep that
   speeds up as you close in and adds a buzz within a metre or two. The screen
   stays lit for as long as you are on it.
3. Walk. Sweep around parked cars, bike racks and building corners — the body of
   a van will cost you 10–20 dB.

## Settings

Everything here is a global tuning knob. *Which* devices you are looking at is
not a setting — that is the menu.

| Setting | Effect |
| --- | --- |
| Settle time | 3–10 s (default 5). How long a fresh scan collects before it locks the row order. Longer catches more devices that advertise slowly; shorter gets you looking sooner. |
| Match mode | Strict / Normal / Loose. Shifts every group's threshold by ±15 %. Start Normal; go Loose if you are missing devices you can see, Strict if unrelated devices show up. |
| Scan type | **Active** also requests scan responses, which is where many modules put their name. **Passive** is quieter and slightly faster but can lose name-based matching. |
| Scan rate | Fast (90 ms window / 100 ms interval) or Saver. Use Fast in the field. |
| Min signal | Hides anything weaker than the threshold — cuts clutter in a busy street. |
| Beeper / Vibration | Homing feedback. |
| Survey log | Appends every received advertisement to `survey.csv`. |

## Files on the SD card

`/ext/apps_data/ble_locator/`

- `groups.txt` — the learned groups, as plain editable text. Absent until you
  learn something. A `profiles.txt` left behind by this app's earlier,
  scooter-only version is read once on first run and migrated here, minus the
  starter groups that version shipped — those were not the operator's work and
  are not carried forward. Copy it between
  Flippers to roll a fingerprint out to a whole crew, or hand-edit a threshold.
  Unknown keys are ignored, so it survives future format additions.
- `members/<id>.txt` — the devices each group was built from, one file per
  group, keyed by the group's stable id. Deleting a group deletes its file.
- `settings.bin` — app settings.
- `survey.csv` — `tick_ms,mac,addr_type,event_type,rssi,adv_hex,name,score`, one
  row per received advertisement, when Survey log is on. This is the raw
  material for tuning a fingerprint off-device.

## How matching works

Each advertiser is tracked by address, with its ADV_IND payload and its
SCAN_RSP merged into one feature set: name, manufacturer id and the leading
bytes of manufacturer data, 16- and 128-bit service UUIDs, MAC vendor prefix,
which AD types are present, payload lengths, address type and connectability.

Building a group intersects those features across every capture — a common
name prefix (with trailing id digits trimmed), the longest shared manufacturer
data prefix, the service UUIDs they all carry, a shared MAC block, and so on.
The build is rejected outright if nothing identifying survives the
intersection, because a group keyed only on payload shape would match half
the street.

Scoring is a weighted percentage of the criteria a group asserts, with two
rules that matter in practice:

- **Contradiction vetoes, absence does not.** A different manufacturer id, a
  different 128-bit UUID or a name that does not start with the learned prefix
  rejects the device outright. A *missing* field just scores zero — otherwise a
  device whose name only rides in a scan response would vanish the moment you
  switched to passive scanning.
- **Shape alone is never enough.** At least one identifying criterion has to
  actually hit; matching only on payload lengths and AD types scores nothing.

The manufacturer-data prefix is graded rather than all-or-nothing, and the
number of bytes trusted grows with the number of captures. A prefix learned
from few devices tends to reach into the serial number, and demanding all of
it would reject the rest of the group.

## Tests

The parsing, fingerprinting and matching logic is plain C with no Flipper
dependencies beyond a few stubs, so it runs on a PC:

```sh
./applications_user/ble_locator/test/run_tests.sh
```

Covers AD parsing, ADV/SCAN_RSP merging, malformed and truncated payloads, a
200k-iteration parser fuzz under ASan/UBSan, group building and its
over-generic guard, cross-group matching and rejection, passive-scan (nameless)
matching, case-insensitive names, group file round-trips, and the
discriminative threshold tightening.

## Firmware changes this app depends on

- `targets/f7/ble_glue/ble_scanner.{c,h}` — observer procedure, advertising
  report parsing, delivery to a registered callback.
- `targets/furi_hal_include/furi_hal_bt.h`, `targets/f7/furi_hal/furi_hal_bt.c` —
  `furi_hal_bt_{is_scan_supported,start_scan,stop_scan,is_scanning}`.
- `targets/f7/ble_glue/gap.c` — asks for the GAP observer role alongside
  peripheral when the full stack is installed.
- `fbt_options.py` — full radio stack.
- `scripts/ob.data`, `scripts/flipper/assets/obdata.py` — the flash/SRAM
  boundary option bytes are owned by FUS and follow whichever stack is
  installed, so they are no longer compared against a build-time constant. An
  `i` (ignore) mode was added for that.
