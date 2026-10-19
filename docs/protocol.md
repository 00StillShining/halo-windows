# EP-series sample protocol (EP-40 / EP-133 / EP-1320)

This is Halo's working spec for Teenage Engineering's private USB-MIDI SysEx protocol. It's compiled from public sources: decoded captures, TE's own web Sample Tool bundle (`garrettjwilke/ep_133_sample_tool`), and hardware-tested community libraries (notably `kmorrill/ep-series-sysex`, tested on an EP-40 running OS 2.5.1). No code was copied; Halo implements this independently.

Labels: **C** = confirmed by TE's code or decoded captures; **D** = documented by a third party; **I** = inferred; **H** = verified by Halo on the owner's EP-40.

## Device identity (H, 2026-10-19)

```
TX F0 7E 7F 06 01 F7
RX F0 7E 3C 06 02 00 20 76 20 00 06 00 00 00 00 00 F7
```

- Manufacturer `00 20 76` = Teenage Engineering.
- Family 0x20, member 6 gives SKU **TE032AS006** (EP-40). The EP-133 is member 1, the EP-1320 member 5.
- **devId = 0x3C.** It goes in byte 4 of every TE frame (the EP-133's is 0x33); always take it from the identity reply.
- Version bytes are zero on OS 2.5.x. The OS version comes from GREET.

## Verified by Halo on the owner's EP-40 (OS 2.5.0, 2026-10-19)

- **GREET:** `product:EP-40;mode:normal;sku:TE032AS006;os_version:2.5.0;sw_version:2.5.0;bl_version:1000.0.0;serial:…`
- **FILE INIT:** chunkSize 512.
- **META GET 1000:** `max_capacity` 127801344, `free_space_in_bytes`, `formats` (s16, 1-2 channels, `samplerate.range` [1, 65535], native 46875), and `tabs` with the device's own categories: KICKS 1-99, SNARES 100-199, CYMBALS 200-299, PERC 300-399, BASS 400-499, LEADS 500-599, SKANKS 600-699, VOX & FX 700-799, LOOPS 800-899, USER 900-999.
- **LIST 1000:** 477 sounds over 24 pages, each named `NNN.pcm` with flags 0x1D. Slot metadata uses `samplerate` (OS 2.x).
- **META GET 2000:** `{"active": <project fid>}` (project 6 here).
- **Pad metadata:** `{"sym":747,"sound.playmode":"oneshot","sample.start":0,"sample.end":…,"envelope.attack":0,"envelope.release":255,"sound.pitch":0.00,"sound.amplitude":88,"sound.pan":0,"sound.mutegroup":false,"time.mode":"off","midi.channel":0,"sound.rootnote":60}`
- **GET:** pages of 324 data bytes. Slot 54 is 13372 bytes, mono 44.1 kHz s16 LE, 42 pages in about 170 ms.
- **GET on a project fid** returns the project as a TAR (55-75 KB): `live`, `pads/a/p01…`, and so on.
- **Throughput:** about 80 KB/s for reads, so a full backup takes around 20-25 minutes.

## Framing (C)

```
request : F0 00 20 76 <devId> 40 <0x40|0x20|(rid>>7 & 0x1F)> <rid & 0x7F> <cmd> <packed payload> F7
response: F0 00 20 76 <devId> 40 <0x20|(rid>>7)>             <rid & 0x7F> <cmd> <STATUS> <packed body> F7
```

- Byte 5 is always 0x40. Byte 5 = 0x33 means a firmware debug frame: stop all FILE traffic.
- The request id is 12 bits and is echoed back. A frame without bit 0x20 in byte 6 is an unsolicited event.
- **The status byte is raw (not packed).** 0 = OK, 1 = ERROR, 2 = unknown command, 3 = bad request, 16-63 = command-specific error (the body is ASCII text), 64 = interim (keep waiting for the final 0).
- Packing: groups of 1 flag byte + up to 7 data bytes. Flag bit k is the MSB of data byte k.
- Only one FILE request may be in flight at a time. Timeout 2 s (15 s for project writes).

## Commands

| cmd | name | notes |
|---|---|---|
| 0x01 | GREET | no payload; reply is ASCII `key:value;…` (product, mode, sku, os_version, serial, …) |
| 0x02 | ECHO | returns its payload |
| 0x03 | DFU | **never send** (firmware) |
| 0x05 | FILE | sub-commands below |

FILE sub-commands (big-endian integers):

| sub | payload | reply body |
|---|---|---|
| INIT `01` | `01 <flags> <maxResp u32>` (Halo: `01 01 00 40 00 00`) | `<u8> <chunkSize u32>` (512) |
| PUT INIT `02 00` | `<flags=05> <fid> <parent=03E8> <size u32> <name>\0 {json}` | `<fid>` |
| PUT DATA `02 01` | `<page u16> <≤433 bytes>`; an empty page ends it | — |
| GET INIT `03 00` | `<fid> <offset u32=0>` | `<fid><flags u8><size u32><name>\0` |
| GET DATA `03 01` | `<page u16>` | `<page u16><data>` |
| LIST `04` | `<page u16> <node u16>` | `<page>` + `<fid u16><flags u8><size u32><name>\0`… |
| DELETE `06` | `<fid>` | — |
| META SET `07 01` | `<fid> <json>\0` | — |
| META GET `07 02` | `<fid> <page u16>` | `<page><json chunk>` (ends on a trailing 0 or a body ≤ 2) |
| STAT `0B` | `<fid>` | `<fid><parent><flags><size u32><name>\0` |

## File tree

| Node | fid |
|---|---|
| `/sounds` | 1000; slot N has fid N ("NNN.pcm"). META GET 1000 gives `free_space_in_bytes`, `max_capacity` and `formats`. |
| `/projects` | 2000; project P is 3000 + (P−1)·1000 |
| groups | +200 (A) … +500 (D) |
| pads | group + 1…12 |

- SysEx pad numbers: 1-3 = 7 8 9, 4-6 = 4 5 6, 7-9 = 1 2 3, 10 = ".", 11 = "0", 12 = ENTER. That is the same row-major grid Halo uses.
- Sound audio is headerless **s16 little-endian** PCM, interleaved, at the slot's `samplerate` (max 46875).
- Pad → sound is pad metadata `{"sym": slot}`. Reading it uses META GET on the pad fid; assigning uses META SET `{"sym": N}`.

## Safety rules Halo follows

- Read-only first: identity → GREET (mode must be `normal`) → INIT → META GET 1000 → LIST → META GET.
- Never send DFU (0x03), 0x06 at the top level, or 0x7F. Never PUT a project TAR that Halo built itself.
- Writes go one at a time, waiting for each status, and finish with INIT. Back up before the first write.
- Upload is: PUT INIT → PUT DATA pages → **empty final page** → META SET → INIT → verify with STAT.
