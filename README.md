# M5PaperS3 News Dashboard

Japanese: [README.ja.md](README.ja.md)

This repository contains the M5PaperS3-side implementation of the news dashboard. It displays PNG pages served over HTTP, keeps them on SD as cache, and updates only when `index.version` changes.

For the whole system, see the integration hub [m5papers3-news-system](https://github.com/omiya-bonsai/m5papers3-news-system). For the Raspberry Pi generation and serving side, see [news-png-generator](https://github.com/omiya-bonsai/news-png-generator).

## Quick Links

- Integration hub:
  - [omiya-bonsai/m5papers3-news-system](https://github.com/omiya-bonsai/m5papers3-news-system)
- Raspberry Pi server:
  - [omiya-bonsai/news-png-generator](https://github.com/omiya-bonsai/news-png-generator)

![M5PaperS3 device photo](img/01.jpeg)

## Features

- `index.version`-based diff checks
- fast page display using SD cache
- swipe and tap page navigation
- periodic background `index` refresh while idle
- priority prefetch for detail pages
- `NEW / READ` state based on `index.version`
- refresh policy changes depending on USB power and battery level
- battery-aware update throttling
- background refresh to reduce network waits during interaction

Note: the detailed `docs/` files are currently written in Japanese.

## Related Repositories

- Integration hub:
  - [omiya-bonsai/m5papers3-news-system](https://github.com/omiya-bonsai/m5papers3-news-system)
- Raspberry Pi server:
  - [omiya-bonsai/news-png-generator](https://github.com/omiya-bonsai/news-png-generator)

## Expected System Behavior

Server side:

- generates news PNG pages
- generates `index.version`
- serves files over HTTP

Device side:

- stays awake in always-on mode
- evaluates USB power and battery state
- checks `index.version` at intervals
- updates `index.png` only when needed
- prefetches priority detail pages when appropriate
- prefers cached pages during user interaction

## Delivered Files

The device expects these eight files:

```text
index.png
page1.png
page2.png
page3.png
page4.png
page5.png
page6.png
index.version
```

The current default URL in code is:

```text
http://192.168.3.82:8010/index.png
```

## Update Flow

The device checks `index.version` before it downloads images.

1. Fetch `index.version`
2. Compare it with the previous value
3. Skip `index.png` if unchanged
4. Refresh `index.png` if changed
5. Invalidate detail-page caches

This reduces network traffic, power consumption, and e-paper refreshes.

## Refresh Architecture

The current design is "always on, but refresh in the background."

Basic flow:

1. Display the cached `index` page first
2. Check `index.version` and refresh `index.png` only if needed
3. Invalidate detail caches only when `index.version` changes
4. Prefetch `page1` and the last-viewed detail page on normal battery
5. Re-check `index.version` periodically while idle
6. Prefer cached pages during user interaction
7. Turn Wi-Fi on only for refresh work

Current policy:

- more frequent refresh on USB power
- medium refresh interval on normal battery
- longer interval on low battery
- stop automatic refresh on critical battery
- detail-page prefetch only on normal battery
- no background refresh for 20 seconds right after user interaction

## Controls

From the index page:

- tap a headline to open the matching detail page

On all screens:

- swipe left for next page
- swipe right for previous page
- swipe up from the bottom edge to return to index
- tap the top-right area to force refresh

On detail pages:

- tap near the title to return to index

For touch details, see [`docs/touch-gesture.md`](docs/touch-gesture.md).

## Cache

Files are stored at the root of the SD card:

```text
/index.png
/page1.png
/page2.png
/page3.png
/page4.png
/page5.png
/page6.png
/index.version
```

If a cached page is still valid, it is shown without network access.

On normal battery, the device also prefetches:

- `page1.png`
- the last-viewed detail page

This improves perceived responsiveness without prefetching every page all the time.

## Footer and Header UI

Footer:

- `LAST`: time of the last successful update
- center: current status code and refresh interval
- `Pn/6`: current page number
- `BAT xx%`: battery level
- `USB`: shown when USB power is detected

Read state:

- `NEW` means the current `index.version` has not been acknowledged yet
- `READ` means at least one valid action has occurred for the current `index.version`
- the top-right label is emphasized with inverted colors when unread

For more UI details, see [`docs/display-ui.md`](docs/display-ui.md).

## Wi-Fi and Time Sync

Wi-Fi is enabled only during refresh work and disconnected afterward.

NTP servers:

- `ntp.nict.jp`
- `time.cloudflare.com`
- `pool.ntp.org`

These are used to keep the `LAST` timestamp meaningful.

## Setup

### 1. Create `config.h`

Use [`config.example.h`](config.example.h) as a template and create `config.h` with your own values.

```cpp
#pragma once

static const char* WIFI_SSID     = "YOUR_WIFI_SSID";
static const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

`config.h` is ignored by Git and should not be committed.

### 2. Required board and library

- Arduino IDE or `arduino-cli`
- M5PaperS3 board support
- `M5Unified`

### 3. Build example

```sh
arduino-cli compile -b m5stack:esp32:m5stack_papers3 .
```

## Repository Contents

```text
M5PaperS3_NewsDashboard.ino
config.example.h
README.md
README.ja.md
docs/
```

## Notes

- URL values, thresholds, and intervals are currently code constants
- priority prefetch is enabled only on normal battery
- the device does not use deep sleep in the current implementation
- battery display is based on sampled values, so USB plug/unplug changes can appear with a delay
- power-control details are documented in [`docs/power-policy.md`](docs/power-policy.md)
- real NHK content images should not be committed to this repository

## License

This repository is licensed under the [MIT License](LICENSE).
