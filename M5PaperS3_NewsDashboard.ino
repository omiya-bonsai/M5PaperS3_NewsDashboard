#include <SD.h>   // M5Unified より先に読む
#include <WiFi.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "config.h"

static const char* PAGE_URLS[] = {
  "http://192.168.3.82:8010/index.png",  // 一覧
  "http://192.168.3.82:8010/page1.png",  // 1件目詳細
  "http://192.168.3.82:8010/page2.png",  // 2件目詳細
  "http://192.168.3.82:8010/page3.png",  // 3件目詳細
  "http://192.168.3.82:8010/page4.png",  // 4件目詳細
  "http://192.168.3.82:8010/page5.png",  // 5件目詳細
  "http://192.168.3.82:8010/page6.png"   // 6件目詳細
};

static constexpr int PAGE_COUNT = sizeof(PAGE_URLS) / sizeof(PAGE_URLS[0]);

static const char* INDEX_VERSION_URL  = "http://192.168.3.82:8010/index.version";
static const char* INDEX_VERSION_PATH = "/index.version";

// ページごとの SD キャッシュ
static const char* PAGE_CACHE_PATHS[] = {
  "/index.png",
  "/page1.png",
  "/page2.png",
  "/page3.png",
  "/page4.png",
  "/page5.png",
  "/page6.png"
};

static constexpr uint32_t INDEX_CACHE_TTL_MS  = 5UL * 60UL * 1000UL;
static constexpr uint32_t DETAIL_CACHE_TTL_MS = 15UL * 60UL * 1000UL;

static const char* DL_PNG_PATH = "/download.png";
static const char* DL_TXT_PATH = "/download.txt";

// 省電力運用の基本更新間隔
static constexpr uint32_t REFRESH_MS_HIGH = 15UL * 60UL * 1000UL;
static constexpr uint32_t REFRESH_MS_MID  = 30UL * 60UL * 1000UL;
static constexpr uint32_t REFRESH_MS_LOW  = 60UL * 60UL * 1000UL;
static constexpr uint32_t REFRESH_MS_CRITICAL = 120UL * 60UL * 1000UL;

static constexpr uint32_t IDLE_WAIT_MS = 90UL * 1000UL;
static constexpr uint32_t LOOP_WAIT_MS = 80UL;
static constexpr uint32_t BACKGROUND_REFRESH_IDLE_GRACE_MS = 20UL * 1000UL;

// ジャイロ / 加速度で持ち上げ判定
static constexpr float LIFT_ACCEL_DELTA_THRESHOLD = 0.18f;
static constexpr float LIFT_GYRO_THRESHOLD_DPS = 12.0f;
static constexpr int LIFT_SAMPLE_COUNT = 5;
static constexpr uint32_t LIFT_SAMPLE_DELAY_MS = 35UL;

// スワイプ判定
static constexpr int SWIPE_THRESHOLD_X = 80;
static constexpr int SWIPE_THRESHOLD_Y = 80;

// タップ判定
static constexpr int TAP_THRESHOLD_X = 18;
static constexpr int TAP_THRESHOLD_Y = 18;
static constexpr int BOTTOM_EDGE_ZONE_H = 120;
static constexpr int BOTTOM_EDGE_SWIPE_UP_MIN_Y = 140;
static constexpr int BOTTOM_EDGE_SWIPE_UP_MAX_X = 80;

static constexpr int MANUAL_REFRESH_ZONE_W = 140;
static constexpr int MANUAL_REFRESH_ZONE_H = 100;

// タイトルタップ領域
static constexpr int TITLE_TAP_X_MIN = 0;
static constexpr int TITLE_TAP_X_MAX = 400;
static constexpr int TITLE_TAP_Y_MIN = 0;
static constexpr int TITLE_TAP_Y_MAX = 90;

// index.png のヘッドラインタップ領域
static constexpr int HEADLINE_TAP_X_MIN = 0;
static constexpr int HEADLINE_TAP_X_MAX = 510;

static constexpr int HEADLINE1_Y_MIN =  85;
static constexpr int HEADLINE1_Y_MAX = 200;

static constexpr int HEADLINE2_Y_MIN = 190;
static constexpr int HEADLINE2_Y_MAX = 315;

static constexpr int HEADLINE3_Y_MIN = 300;
static constexpr int HEADLINE3_Y_MAX = 430;

static constexpr int HEADLINE4_Y_MIN = 410;
static constexpr int HEADLINE4_Y_MAX = 545;

static constexpr int HEADLINE5_Y_MIN = 520;
static constexpr int HEADLINE5_Y_MAX = 670;

static constexpr int HEADLINE6_Y_MIN = 635;
static constexpr int HEADLINE6_Y_MAX = 820;

// NTP
static constexpr uint32_t NTP_RESYNC_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;

// バッテリー
static constexpr int LOW_BATTERY_THRESHOLD  = 20;
static constexpr int CRITICAL_BATTERY_LEVEL = 10;
static constexpr uint32_t BATTERY_SAMPLE_INTERVAL_MS = 5UL * 60UL * 1000UL;

enum class WakeReason {
  ColdBoot,
  Timer,
  Button,
  Touch,
  Unknown
};

enum class BatteryProfile {
  Normal,
  Low,
  Critical
};

struct WakeContext {
  WakeReason reason = WakeReason::Unknown;
  bool usbPowered = false;
  bool motionDetected = false;
  BatteryProfile batteryProfile = BatteryProfile::Normal;
};

struct PowerPolicy {
  uint32_t wakeIntervalMs = REFRESH_MS_MID;
  uint32_t idleSleepMs = IDLE_WAIT_MS;
  bool allowAutoUpdate = true;
  bool allowInteractiveNetwork = true;
  bool keepAwake = true;
  const char* statusCode = "INIT";
  const char* reasonText = "boot";
};

RTC_DATA_ATTR uint32_t rtcBootCount = 0;
RTC_DATA_ATTR uint32_t rtcLastSuccessEpoch = 0;
RTC_DATA_ATTR char rtcLastSuccessTextStore[24] = "--:--";
RTC_DATA_ATTR char rtcLastKnownIndexVersionStore[32] = "";
RTC_DATA_ATTR int rtcLastViewedDetailPage = 1;

int currentPage = 0;

// タッチ追跡
bool touchTracking = false;
bool touchStartedAtBottomEdge = false;
int touchStartX = 0;
int touchStartY = 0;

// 状態管理
bool pageLoaded = false;
uint32_t lastRefreshMs = 0;
uint32_t lastBackgroundRefreshCheckMs = 0;
uint32_t lastSuccessfulLoadMs = 0;
uint32_t lastNtpSyncMs = 0;
uint32_t lastBatterySampleMs = 0;
uint32_t lastUserActivityMs = 0;

// ページごとのキャッシュ取得時刻
uint32_t pageCacheFetchedAt[PAGE_COUNT] = {0};

// 失敗カウント
uint32_t refreshFailureCount = 0;
uint32_t pageChangeFailureCount = 0;
uint32_t wifiConnectCount = 0;
uint32_t ntpSyncSuccessCount = 0;
uint32_t cacheHitCount = 0;

// 画面表示用
String lastSuccessTimeText = "--:--";
String lastStatusText = "INIT";   // INIT / LIVE / CACHED / SKIP / NET / HTTP / RENDER ...

// バッテリー表示用
int cachedBatteryLevel = -1;
bool cachedBatteryCharging = false;
float cachedBatteryVoltage = 0.0f;

// index.version
String lastKnownIndexVersion = "";

WakeContext currentWakeContext;
PowerPolicy currentPolicy;
void drawStatus(const String& msg) {
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(20, 40);
  M5.Display.println(msg);
  M5.Display.display();
}

void printCounters() {
  Serial.printf(
    "Counters: refreshFail=%lu pageChangeFail=%lu wifiConnect=%lu ntpSync=%lu cacheHit=%lu status=%s\n",
    static_cast<unsigned long>(refreshFailureCount),
    static_cast<unsigned long>(pageChangeFailureCount),
    static_cast<unsigned long>(wifiConnectCount),
    static_cast<unsigned long>(ntpSyncSuccessCount),
    static_cast<unsigned long>(cacheHitCount),
    lastStatusText.c_str()
  );
}

String formatCurrentDateTimeShort() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    return "--:--";
  }

  char buf[24];
  strftime(buf, sizeof(buf), "%m-%d %H:%M", &timeinfo);
  return String(buf);
}

void persistLastSuccessStamp() {
  rtcLastSuccessEpoch = static_cast<uint32_t>(time(nullptr));
  strlcpy(rtcLastSuccessTextStore, lastSuccessTimeText.c_str(), sizeof(rtcLastSuccessTextStore));
}

void restorePersistedState() {
  if (rtcLastSuccessTextStore[0] != '\0') {
    lastSuccessTimeText = String(rtcLastSuccessTextStore);
  }
  if (rtcLastKnownIndexVersionStore[0] != '\0') {
    lastKnownIndexVersion = String(rtcLastKnownIndexVersionStore);
  }
}

void persistIndexVersion(const String& version) {
  lastKnownIndexVersion = version;
  strlcpy(rtcLastKnownIndexVersionStore, version.c_str(), sizeof(rtcLastKnownIndexVersionStore));
}

void persistLastViewedDetailPage(int pageIndex) {
  if (pageIndex >= 1 && pageIndex < PAGE_COUNT) {
    rtcLastViewedDetailPage = pageIndex;
  }
}

void updateLastSuccessStamp() {
  lastSuccessfulLoadMs = millis();
  lastSuccessTimeText = formatCurrentDateTimeShort();
  persistLastSuccessStamp();
}

bool isUsbPoweredNow() {
  int16_t vbusMv = M5.Power.getVBUSVoltage();
  if (vbusMv > 4300) {
    return true;
  }

  auto chargingState = M5.Power.isCharging();
  return chargingState == m5::Power_Class::is_charging_t::is_charging;
}

void sampleBatteryIfNeeded(bool force = false) {
  uint32_t now = millis();
  if (!force && (now - lastBatterySampleMs < BATTERY_SAMPLE_INTERVAL_MS)) {
    return;
  }

  lastBatterySampleMs = now;

  int level = M5.Power.getBatteryLevel();
  if (level < 0) level = -1;
  if (level > 100) level = 100;

  cachedBatteryLevel = level;
  cachedBatteryCharging = isUsbPoweredNow();
  cachedBatteryVoltage = static_cast<float>(M5.Power.getBatteryVoltage()) / 1000.0f;

  Serial.printf(
    "Battery: level=%d%% usb=%s voltage=%.3fV\n",
    cachedBatteryLevel,
    cachedBatteryCharging ? "true" : "false",
    cachedBatteryVoltage
  );
}

BatteryProfile getBatteryProfile() {
  if (cachedBatteryLevel >= 0 && cachedBatteryLevel < CRITICAL_BATTERY_LEVEL) {
    return BatteryProfile::Critical;
  }
  if (cachedBatteryLevel >= 0 && cachedBatteryLevel < LOW_BATTERY_THRESHOLD) {
    return BatteryProfile::Low;
  }
  return BatteryProfile::Normal;
}

String getBatteryLabel() {
  if (cachedBatteryLevel < 0) {
    return cachedBatteryCharging ? "BAT -- USB" : "BAT --";
  }

  String s = "BAT " + String(cachedBatteryLevel) + "%";
  if (cachedBatteryCharging) s += " USB";
  return s;
}

bool isLowBattery() {
  return getBatteryProfile() != BatteryProfile::Normal;
}

bool isCriticalBattery() {
  return getBatteryProfile() == BatteryProfile::Critical;
}

uint32_t getCurrentRefreshIntervalMs() {
  return currentPolicy.wakeIntervalMs;
}

String getRefreshIntervalLabel() {
  uint32_t ms = getCurrentRefreshIntervalMs();
  if (ms == 0) return "MANUAL";
  return String(ms / 60000UL) + "m";
}

uint32_t getPageCacheTtlMs(int pageIndex) {
  return (pageIndex == 0) ? INDEX_CACHE_TTL_MS : DETAIL_CACHE_TTL_MS;
}

bool isPageCacheValid(int pageIndex) {
  if (pageIndex < 0 || pageIndex >= PAGE_COUNT) return false;
  if (!SD.exists(PAGE_CACHE_PATHS[pageIndex])) return false;
  if (pageCacheFetchedAt[pageIndex] == 0) return false;

  uint32_t ttl = getPageCacheTtlMs(pageIndex);
  return (millis() - pageCacheFetchedAt[pageIndex] < ttl);
}

void markPageCacheFresh(int pageIndex) {
  if (pageIndex < 0 || pageIndex >= PAGE_COUNT) return;
  pageCacheFetchedAt[pageIndex] = millis();
}

void invalidatePageCache(int pageIndex) {
  if (pageIndex < 0 || pageIndex >= PAGE_COUNT) return;
  pageCacheFetchedAt[pageIndex] = 0;
}

void invalidateAllCaches() {
  for (int i = 0; i < PAGE_COUNT; ++i) {
    pageCacheFetchedAt[i] = 0;
  }
}

void invalidateAllDetailCaches() {
  for (int i = 1; i < PAGE_COUNT; ++i) {
    pageCacheFetchedAt[i] = 0;
  }
}

void noteUserActivity() {
  lastUserActivityMs = millis();
}

void drawOverlayStatusBar() {
  static constexpr int BAR_H = 18;
  static constexpr int CHAR_W = 6;
  const int y = M5.Display.height() - BAR_H;
  const int w = M5.Display.width();

  M5.Display.fillRect(0, y, w, BAR_H, TFT_WHITE);
  M5.Display.drawFastHLine(0, y, w, TFT_LIGHTGRAY);
  M5.Display.setTextSize(1);

  String leftText = "LAST " + lastSuccessTimeText;
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setCursor(6, y + 5);
  M5.Display.print(leftText);

  String centerText = lastStatusText + " " + getRefreshIntervalLabel();
  int centerWidth = centerText.length() * CHAR_W;
  int centerX = (w - centerWidth) / 2;
  M5.Display.setCursor(centerX, y + 5);
  M5.Display.print(centerText);

  String rightText = "P" + String(currentPage) + "/" + String(PAGE_COUNT - 1) + " " + getBatteryLabel();
  int rightWidth = static_cast<int>(M5.Display.textWidth(rightText.c_str())) + 8;
  int rightX = w - rightWidth - 4;

  if (isLowBattery()) {
    M5.Display.fillRoundRect(rightX, y + 2, rightWidth, BAR_H - 4, 3, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(rightX + 4, y + 5);
    M5.Display.print(rightText);
  } else {
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setCursor(rightX + 4, y + 5);
    M5.Display.print(rightText);
  }

  M5.Display.display();
}

void disconnectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi disconnect");
  }
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(50);
}

bool connectWiFiBlocking(uint32_t timeoutMs = 15000UL) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println("Wi-Fi connecting...");
  drawStatus("Wi-Fi connecting...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    if (millis() - start > timeoutMs) {
      Serial.println("Wi-Fi connect timeout");
      disconnectWiFi();
      return false;
    }
  }

  wifiConnectCount++;
  Serial.print("Wi-Fi connected. IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool syncClockBlocking(uint32_t timeoutMs = 10000UL) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("syncClockBlocking: Wi-Fi unavailable");
    return false;
  }

  Serial.println("NTP sync start");
  configTzTime("JST-9", "ntp.nict.jp", "time.cloudflare.com", "pool.ntp.org");

  uint32_t start = millis();
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo, 200)) {
    if (millis() - start > timeoutMs) {
      Serial.println("NTP sync timeout");
      return false;
    }
  }

  lastNtpSyncMs = millis();
  ntpSyncSuccessCount++;
  Serial.println("NTP sync OK");
  printCounters();
  return true;
}

bool ensureWiFiAndMaybeNtp(bool needNtp = false) {
  if (!connectWiFiBlocking()) {
    lastStatusText = "NET";
    return false;
  }

  if (needNtp) {
    if (!syncClockBlocking(5000UL)) {
      Serial.println("NTP sync failed, continue without fresh time");
    }
  }
  return true;
}

bool downloadBinaryToSD(const char* url, const char* path) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("downloadBinaryToSD: Wi-Fi unavailable");
    lastStatusText = "NET";
    return false;
  }

  HTTPClient http;
  WiFiClient client;
  http.setTimeout(10000);

  if (!http.begin(client, url)) {
    Serial.println("http.begin failed");
    lastStatusText = "HTTP";
    return false;
  }

  int httpCode = http.GET();
  Serial.printf("HTTP GET %s -> %d\n", url, httpCode);

  if (httpCode != HTTP_CODE_OK) {
    http.end();
    lastStatusText = "HTTP";
    return false;
  }

  if (SD.exists(path)) {
    SD.remove(path);
  }

  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open SD file for write");
    http.end();
    lastStatusText = "SDWR";
    return false;
  }

  int written = http.writeToStream(&file);
  file.close();
  http.end();

  if (written <= 0) {
    Serial.println("writeToStream failed");
    lastStatusText = "HTTP";
    return false;
  }

  if (!SD.exists(path)) {
    Serial.println("Downloaded file not found on SD");
    lastStatusText = "SDWR";
    return false;
  }

  Serial.printf("Downloaded %d bytes to %s\n", written, path);
  return true;
}

String readTextFile(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return "";

  String s;
  while (f.available()) {
    s += char(f.read());
  }
  f.close();
  s.trim();
  return s;
}

void primeIndexVersionFromSd() {
  if (lastKnownIndexVersion.length() > 0) {
    return;
  }

  if (!SD.exists(INDEX_VERSION_PATH)) {
    return;
  }

  String localVersion = readTextFile(INDEX_VERSION_PATH);
  if (localVersion.length() > 0) {
    persistIndexVersion(localVersion);
  }
}

bool downloadIndexVersion(String& outVersion) {
  if (!downloadBinaryToSD(INDEX_VERSION_URL, DL_TXT_PATH)) {
    return false;
  }

  outVersion = readTextFile(DL_TXT_PATH);
  if (outVersion.length() == 0) {
    lastStatusText = "VER";
    return false;
  }

  if (SD.exists(INDEX_VERSION_PATH)) {
    SD.remove(INDEX_VERSION_PATH);
  }
  SD.rename(DL_TXT_PATH, INDEX_VERSION_PATH);

  return true;
}

bool shouldPrefetchDetailsForCurrentPolicy() {
  return currentPolicy.allowInteractiveNetwork &&
         currentWakeContext.batteryProfile == BatteryProfile::Normal &&
         !currentWakeContext.usbPowered;
}

bool shouldPrefetchPage(int pageIndex) {
  if (pageIndex <= 0 || pageIndex >= PAGE_COUNT) return false;
  if (!shouldPrefetchDetailsForCurrentPolicy()) return false;
  return !isPageCacheValid(pageIndex);
}

bool fetchPageToCacheOnly(int pageIndex) {
  if (pageIndex < 0 || pageIndex >= PAGE_COUNT) return false;

  bool needNtp = (lastNtpSyncMs == 0) || ((millis() - lastNtpSyncMs) >= NTP_RESYNC_INTERVAL_MS);
  if (!ensureWiFiAndMaybeNtp(needNtp)) {
    Serial.printf("Prefetch: Wi-Fi failed for page %d\n", pageIndex);
    return false;
  }

  bool ok = true;
  if (!downloadBinaryToSD(PAGE_URLS[pageIndex], DL_PNG_PATH)) {
    Serial.printf("Prefetch: download failed for page %d\n", pageIndex);
    ok = false;
  } else if (!replacePageCacheFromDownload(pageIndex)) {
    Serial.printf("Prefetch: replace failed for page %d\n", pageIndex);
    ok = false;
  }

  disconnectWiFi();

  if (!ok) {
    invalidatePageCache(pageIndex);
    return false;
  }

  Serial.printf("Prefetch: page %d cached\n", pageIndex);
  return true;
}

void prefetchPriorityPages() {
  if (!shouldPrefetchDetailsForCurrentPolicy()) {
    return;
  }

  int candidates[2] = {1, rtcLastViewedDetailPage};
  for (int i = 0; i < 2; ++i) {
    int pageIndex = candidates[i];
    bool duplicate = (i > 0 && pageIndex == candidates[0]);
    if (duplicate || !shouldPrefetchPage(pageIndex)) {
      continue;
    }
    fetchPageToCacheOnly(pageIndex);
  }
}

bool refreshIndexCacheInBackground() {
  if (!currentPolicy.allowAutoUpdate || !currentPolicy.allowInteractiveNetwork) {
    return false;
  }

  String previousStatus = lastStatusText;
  bool needNtp = (lastNtpSyncMs == 0) || ((millis() - lastNtpSyncMs) >= NTP_RESYNC_INTERVAL_MS);
  if (!ensureWiFiAndMaybeNtp(needNtp)) {
    Serial.println("Background refresh: Wi-Fi failed");
    lastStatusText = previousStatus;
    return false;
  }

  String remoteVersion;
  bool versionOk = downloadIndexVersion(remoteVersion);
  disconnectWiFi();

  if (!versionOk) {
    Serial.println("Background refresh: index.version failed");
    lastStatusText = previousStatus;
    return false;
  }

  Serial.printf("Background refresh: remote=%s local=%s\n",
                remoteVersion.c_str(),
                lastKnownIndexVersion.c_str());

  bool versionChanged = (lastKnownIndexVersion.length() == 0 || remoteVersion != lastKnownIndexVersion);
  if (!versionChanged && SD.exists(PAGE_CACHE_PATHS[0])) {
    Serial.println("Background refresh: index unchanged");
    lastStatusText = previousStatus;
    return true;
  }

  persistIndexVersion(remoteVersion);
  invalidateAllDetailCaches();

  if (!fetchPageToCacheOnly(0)) {
    Serial.println("Background refresh: index fetch failed");
    lastStatusText = previousStatus;
    return false;
  }

  if (versionChanged) {
    prefetchPriorityPages();
  }

  if (currentPage == 0) {
    renderFromCache(0);
    lastStatusText = "LIVE";
    drawOverlayStatusBar();
  } else {
    lastStatusText = previousStatus;
  }

  updateLastSuccessStamp();
  printCounters();
  return true;
}

void runPeriodicIndexRefreshIfDue() {
  if (!currentPolicy.allowAutoUpdate) {
    return;
  }

  uint32_t now = millis();
  if (lastBackgroundRefreshCheckMs != 0 &&
      (now - lastBackgroundRefreshCheckMs < getCurrentRefreshIntervalMs())) {
    return;
  }

  if (pageLoaded &&
      (now - lastUserActivityMs < BACKGROUND_REFRESH_IDLE_GRACE_MS)) {
    return;
  }

  lastBackgroundRefreshCheckMs = now;
  refreshIndexCacheInBackground();
}

bool canUseNetworkForCurrentSession() {
  if (currentPolicy.allowInteractiveNetwork) {
    return true;
  }

  lastStatusText = "PWR";
  Serial.println("Network blocked by power policy");
  printCounters();
  return false;
}

bool shouldSkipIndexDownloadByVersion(bool forceRefresh) {
  if (forceRefresh) return false;
  if (currentPage != 0) return false;
  if (!canUseNetworkForCurrentSession()) return false;

  bool needNtp = (lastNtpSyncMs == 0) || ((millis() - lastNtpSyncMs) >= NTP_RESYNC_INTERVAL_MS);
  if (!ensureWiFiAndMaybeNtp(needNtp)) {
    return false;
  }

  String remoteVersion;
  bool ok = downloadIndexVersion(remoteVersion);
  disconnectWiFi();

  if (!ok) {
    return false;
  }

  Serial.printf("index.version remote=%s local=%s\n",
                remoteVersion.c_str(),
                lastKnownIndexVersion.c_str());

  if (lastKnownIndexVersion.length() > 0 &&
      remoteVersion == lastKnownIndexVersion &&
      SD.exists(PAGE_CACHE_PATHS[0])) {
    lastStatusText = "SKIP";
    cacheHitCount++;
    pageLoaded = true;
    drawOverlayStatusBar();
    printCounters();
    return true;
  }

  bool versionChanged = (lastKnownIndexVersion.length() == 0 || remoteVersion != lastKnownIndexVersion);
  persistIndexVersion(remoteVersion);
  invalidateAllDetailCaches();
  if (versionChanged) {
    prefetchPriorityPages();
  }
  return false;
}

bool renderPng(const char* path) {
  if (!SD.exists(path)) {
    Serial.printf("renderPng: not found: %s\n", path);
    lastStatusText = "MISS";
    return false;
  }

  M5.Display.fillScreen(TFT_WHITE);
  bool ok = M5.Display.drawPngFile(SD, path, 0, 0);
  M5.Display.display();

  Serial.printf("drawPngFile(%s) -> %s\n", path, ok ? "OK" : "FAIL");
  if (!ok) {
    lastStatusText = "REND";
    return false;
  }

  drawOverlayStatusBar();
  return true;
}

bool replacePageCacheFromDownload(int pageIndex) {
  if (pageIndex < 0 || pageIndex >= PAGE_COUNT) return false;

  const char* cachePath = PAGE_CACHE_PATHS[pageIndex];

  if (!SD.exists(DL_PNG_PATH)) {
    Serial.println("replacePageCacheFromDownload: download file missing");
    lastStatusText = "SDWR";
    return false;
  }

  if (SD.exists(cachePath)) {
    SD.remove(cachePath);
  }

  if (!SD.rename(DL_PNG_PATH, cachePath)) {
    Serial.printf("Failed to rename download.png -> %s\n", cachePath);
    lastStatusText = "SDWR";
    return false;
  }

  markPageCacheFresh(pageIndex);
  return true;
}

bool renderFromCache(int pageIndex) {
  if (pageIndex < 0 || pageIndex >= PAGE_COUNT) return false;
  if (!SD.exists(PAGE_CACHE_PATHS[pageIndex])) return false;

  Serial.printf("Cache hit for page %d\n", pageIndex);
  cacheHitCount++;

  if (!renderPng(PAGE_CACHE_PATHS[pageIndex])) {
    return false;
  }

  currentPage = pageIndex;
  pageLoaded = true;
  lastStatusText = "CACHED";
  drawOverlayStatusBar();
  printCounters();
  return true;
}

bool loadPage(int pageIndex, bool showStatusOnFailure = true, bool forceRefresh = false) {
  if (pageIndex < 0 || pageIndex >= PAGE_COUNT) {
    Serial.printf("loadPage: invalid pageIndex=%d\n", pageIndex);
    lastStatusText = "ARG";
    return false;
  }

  Serial.printf("Loading page %d/%d (force=%s)\n",
                pageIndex,
                PAGE_COUNT - 1,
                forceRefresh ? "true" : "false");

  if (pageIndex == 0 && !forceRefresh) {
    if (shouldSkipIndexDownloadByVersion(forceRefresh)) {
      return renderFromCache(0);
    }
  }

  if (!forceRefresh && isPageCacheValid(pageIndex)) {
    return renderFromCache(pageIndex);
  }

  if (!canUseNetworkForCurrentSession()) {
    if (renderFromCache(pageIndex)) {
      return true;
    }
    if (showStatusOnFailure && !pageLoaded) {
      drawStatus("Power-save: network blocked");
    }
    return false;
  }

  bool needNtp = (lastNtpSyncMs == 0) || ((millis() - lastNtpSyncMs) >= NTP_RESYNC_INTERVAL_MS);

  if (!ensureWiFiAndMaybeNtp(needNtp)) {
    Serial.printf("loadPage: Wi-Fi failed for page %d\n", pageIndex);
    if (showStatusOnFailure && !pageLoaded) {
      drawStatus(String("Wi-Fi failed: page ") + String(pageIndex));
    }
    return false;
  }

  bool ok = true;

  if (!downloadBinaryToSD(PAGE_URLS[pageIndex], DL_PNG_PATH)) {
    Serial.printf("loadPage: download failed for page %d\n", pageIndex);
    ok = false;
  } else if (!replacePageCacheFromDownload(pageIndex)) {
    Serial.printf("loadPage: replace failed for page %d\n", pageIndex);
    ok = false;
  } else if (!renderPng(PAGE_CACHE_PATHS[pageIndex])) {
    Serial.printf("loadPage: render failed for page %d\n", pageIndex);
    ok = false;
  }

  disconnectWiFi();

  if (!ok) {
    invalidatePageCache(pageIndex);
    if (showStatusOnFailure && !pageLoaded) {
      drawStatus(String("Load failed: page ") + String(pageIndex));
    }
    return false;
  }

  currentPage = pageIndex;
  pageLoaded = true;
  lastRefreshMs = millis();
  updateLastSuccessStamp();
  lastStatusText = "LIVE";
  noteUserActivity();
  persistLastViewedDetailPage(pageIndex);

  drawOverlayStatusBar();

  Serial.printf("Loaded page %d/%d successfully\n", currentPage, PAGE_COUNT - 1);
  return true;
}

void nextPage() {
  int next = currentPage + 1;
  if (next >= PAGE_COUNT) next = 0;

  Serial.printf("nextPage: %d -> %d\n", currentPage, next);

  if (!loadPage(next, true, false)) {
    pageChangeFailureCount++;
    if (lastStatusText.length() == 0 || lastStatusText == "LIVE" || lastStatusText == "CACHED") {
      lastStatusText = "PAGE";
    }
    Serial.println("nextPage failed. Keeping current display.");
    printCounters();
    drawOverlayStatusBar();
  } else {
    noteUserActivity();
  }
}

void prevPage() {
  int prev = currentPage - 1;
  if (prev < 0) prev = PAGE_COUNT - 1;

  Serial.printf("prevPage: %d -> %d\n", currentPage, prev);

  if (!loadPage(prev, true, false)) {
    pageChangeFailureCount++;
    if (lastStatusText.length() == 0 || lastStatusText == "LIVE" || lastStatusText == "CACHED") {
      lastStatusText = "PAGE";
    }
    Serial.println("prevPage failed. Keeping current display.");
    printCounters();
    drawOverlayStatusBar();
  } else {
    noteUserActivity();
  }
}

void jumpToIndexPage() {
  if (currentPage == 0) {
    Serial.println("Title tap ignored: already on index");
    return;
  }

  Serial.println("Title tap -> index");
  if (!loadPage(0, true, false)) {
    pageChangeFailureCount++;
    if (lastStatusText.length() == 0 || lastStatusText == "LIVE" || lastStatusText == "CACHED") {
      lastStatusText = "TITL";
    }
    Serial.println("Title tap jump failed. Keeping current display.");
    printCounters();
    drawOverlayStatusBar();
  } else {
    noteUserActivity();
  }
}

void jumpFromHeadlineTap(int detailPageIndex) {
  Serial.printf("Headline tap jump -> detail %d\n", detailPageIndex);

  if (!loadPage(detailPageIndex, true, false)) {
    pageChangeFailureCount++;
    if (lastStatusText.length() == 0 || lastStatusText == "LIVE" || lastStatusText == "CACHED") {
      lastStatusText = "TAP";
    }
    Serial.println("Headline tap jump failed. Keeping current display.");
    printCounters();
    drawOverlayStatusBar();
  } else {
    noteUserActivity();
  }
}

void manualRefreshCurrentPage() {
  Serial.println("Manual refresh requested");
  if (!loadPage(currentPage, false, true)) {
    refreshFailureCount++;
    if (lastStatusText.length() == 0 || lastStatusText == "LIVE" || lastStatusText == "CACHED") {
      lastStatusText = "MAN";
    }
    Serial.println("Manual refresh failed. Keeping current display.");
    printCounters();
    drawOverlayStatusBar();
    return;
  }
  Serial.println("Manual refresh OK");
  noteUserActivity();
}

bool isManualRefreshTap(int x, int y) {
  return (x >= M5.Display.width() - MANUAL_REFRESH_ZONE_W &&
          y <= MANUAL_REFRESH_ZONE_H);
}

bool isTitleTap(int x, int y) {
  return (x >= TITLE_TAP_X_MIN && x <= TITLE_TAP_X_MAX &&
          y >= TITLE_TAP_Y_MIN && y <= TITLE_TAP_Y_MAX);
}

int getTappedHeadlineIndex(int x, int y) {
  if (x < HEADLINE_TAP_X_MIN || x > HEADLINE_TAP_X_MAX) {
    return -1;
  }

  if (y >= HEADLINE1_Y_MIN && y <= HEADLINE1_Y_MAX) return 0;
  if (y >= HEADLINE2_Y_MIN && y <= HEADLINE2_Y_MAX) return 1;
  if (y >= HEADLINE3_Y_MIN && y <= HEADLINE3_Y_MAX) return 2;
  if (y >= HEADLINE4_Y_MIN && y <= HEADLINE4_Y_MAX) return 3;
  if (y >= HEADLINE5_Y_MIN && y <= HEADLINE5_Y_MAX) return 4;
  if (y >= HEADLINE6_Y_MIN && y <= HEADLINE6_Y_MAX) return 5;

  return -1;
}

bool isShortTap(int dx, int dy) {
  return (abs(dx) <= TAP_THRESHOLD_X && abs(dy) <= TAP_THRESHOLD_Y);
}

bool isBottomEdgeStart(int y) {
  return y >= (M5.Display.height() - BOTTOM_EDGE_ZONE_H);
}

bool isBottomEdgeSwipeUp(int dx, int dy) {
  if (!touchStartedAtBottomEdge) {
    return false;
  }

  return (abs(dx) <= BOTTOM_EDGE_SWIPE_UP_MAX_X &&
          dy <= -BOTTOM_EDGE_SWIPE_UP_MIN_Y);
}

void handleTouchInput() {
  auto t = M5.Touch.getDetail();

  if (t.wasPressed()) {
    touchTracking = true;
    touchStartedAtBottomEdge = isBottomEdgeStart(t.y);
    touchStartX = t.x;
    touchStartY = t.y;
    noteUserActivity();

    Serial.printf("Touch start: x=%d y=%d page=%d bottomEdge=%s\n",
                  touchStartX,
                  touchStartY,
                  currentPage,
                  touchStartedAtBottomEdge ? "true" : "false");
    return;
  }

  if (touchTracking && t.wasReleased()) {
    touchTracking = false;
    noteUserActivity();

    int endX = t.x;
    int endY = t.y;
    int dx = endX - touchStartX;
    int dy = endY - touchStartY;

    int tapX = touchStartX;
    int tapY = touchStartY;

    Serial.printf("Touch end: x=%d y=%d dx=%d dy=%d tapStart=(%d,%d)\n",
                  endX, endY, dx, dy, tapX, tapY);

    if (isShortTap(dx, dy) && isManualRefreshTap(tapX, tapY)) {
      Serial.println("Top-right tap -> manual refresh");
      manualRefreshCurrentPage();
      return;
    }

    if (isShortTap(dx, dy) && isTitleTap(tapX, tapY)) {
      jumpToIndexPage();
      return;
    }

    if (currentPage == 0 && isShortTap(dx, dy)) {
      int tappedHeadline = getTappedHeadlineIndex(tapX, tapY);
      if (tappedHeadline >= 0) {
        jumpFromHeadlineTap(tappedHeadline + 1);
        return;
      }
    }

    if (isBottomEdgeSwipeUp(dx, dy)) {
      Serial.println("Bottom-edge swipe up -> index");
      jumpToIndexPage();
      return;
    }

    if (abs(dx) >= SWIPE_THRESHOLD_X && abs(dy) <= SWIPE_THRESHOLD_Y) {
      if (dx < 0) {
        Serial.println("Swipe left -> next page");
        nextPage();
      } else {
        Serial.println("Swipe right -> previous page");
        prevPage();
      }
    } else {
      Serial.println("Touch ignored");
    }

    touchStartedAtBottomEdge = false;
  }
}

const char* getWakeReasonLabel(WakeReason reason) {
  switch (reason) {
    case WakeReason::ColdBoot:
      return "boot";
    case WakeReason::Timer:
      return "timer";
    case WakeReason::Button:
      return "button";
    case WakeReason::Touch:
      return "touch";
    default:
      return "unknown";
  }
}

WakeReason getWakeReason() {
  return WakeReason::ColdBoot;
}

float max3(float a, float b, float c) {
  return fmaxf(a, fmaxf(b, c));
}

bool detectLiftEvent() {
  float maxAccelDelta = 0.0f;
  float maxGyroAbs = 0.0f;
  int validSamples = 0;

  for (int i = 0; i < LIFT_SAMPLE_COUNT; ++i) {
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    bool accelOk = M5.Imu.getAccel(&ax, &ay, &az);
    bool gyroOk = M5.Imu.getGyro(&gx, &gy, &gz);

    if (accelOk || gyroOk) {
      validSamples++;
      if (accelOk) {
        float accelMag = sqrtf((ax * ax) + (ay * ay) + (az * az));
        float accelDelta = fabsf(accelMag - 1.0f);
        if (accelDelta > maxAccelDelta) {
          maxAccelDelta = accelDelta;
        }
      }
      if (gyroOk) {
        float gyroAbs = max3(fabsf(gx), fabsf(gy), fabsf(gz));
        if (gyroAbs > maxGyroAbs) {
          maxGyroAbs = gyroAbs;
        }
      }
    }

    delay(LIFT_SAMPLE_DELAY_MS);
  }

  if (validSamples == 0) {
    Serial.println("IMU unavailable. Allowing update by fallback.");
    return true;
  }

  Serial.printf("Lift detection: accelDelta=%.3f gyroPeak=%.3f\n", maxAccelDelta, maxGyroAbs);
  return (maxAccelDelta >= LIFT_ACCEL_DELTA_THRESHOLD) || (maxGyroAbs >= LIFT_GYRO_THRESHOLD_DPS);
}

PowerPolicy decidePowerPolicy(const WakeContext& ctx) {
  PowerPolicy policy;
  policy.allowInteractiveNetwork = !(ctx.batteryProfile == BatteryProfile::Critical && !ctx.usbPowered);

  if (ctx.usbPowered) {
    policy.wakeIntervalMs = REFRESH_MS_HIGH;
    policy.allowAutoUpdate = true;
    policy.keepAwake = true;
    policy.idleSleepMs = IDLE_WAIT_MS;
    policy.statusCode = "USB";
    policy.reasonText = "usb-powered";
    return policy;
  }

  if (ctx.batteryProfile == BatteryProfile::Critical) {
    policy.wakeIntervalMs = REFRESH_MS_CRITICAL;
    policy.allowAutoUpdate = false;
    policy.allowInteractiveNetwork = false;
    policy.keepAwake = true;
    policy.idleSleepMs = IDLE_WAIT_MS;
    policy.statusCode = "BCRIT";
    policy.reasonText = "critical-battery";
    return policy;
  }

  policy.wakeIntervalMs = (ctx.batteryProfile == BatteryProfile::Low) ? REFRESH_MS_LOW : REFRESH_MS_MID;
  policy.idleSleepMs = IDLE_WAIT_MS;

  policy.allowAutoUpdate = policy.allowInteractiveNetwork;
  policy.keepAwake = true;
  policy.statusCode = "RUN";
  policy.reasonText = "always-on";

  return policy;
}

void renderCachedIndexIfAvailable() {
  if (SD.exists(PAGE_CACHE_PATHS[0])) {
    renderFromCache(0);
  }
}

void runWakeFlow() {
  currentWakeContext.reason = getWakeReason();
  currentWakeContext.usbPowered = isUsbPoweredNow();
  currentWakeContext.batteryProfile = getBatteryProfile();
  currentWakeContext.motionDetected = true;
  currentPolicy = decidePowerPolicy(currentWakeContext);
  lastStatusText = String(currentPolicy.statusCode);

  Serial.printf(
    "RunFlow: boot=%lu reason=%s usb=%s motion=%s battery=%d policy=%s interval=%lu idle=%lu\n",
    static_cast<unsigned long>(rtcBootCount),
    getWakeReasonLabel(currentWakeContext.reason),
    currentWakeContext.usbPowered ? "true" : "false",
    currentWakeContext.motionDetected ? "true" : "false",
    static_cast<int>(currentWakeContext.batteryProfile),
    currentPolicy.reasonText,
    static_cast<unsigned long>(currentPolicy.wakeIntervalMs),
    static_cast<unsigned long>(currentPolicy.idleSleepMs)
  );

  bool haveIndexCache = SD.exists(PAGE_CACHE_PATHS[0]);
  if (haveIndexCache) {
    renderCachedIndexIfAvailable();
  }

  if (!haveIndexCache && currentPolicy.allowInteractiveNetwork) {
    currentPolicy.allowAutoUpdate = true;
    currentPolicy.keepAwake = true;
  }

  if (currentPolicy.allowAutoUpdate) {
    if (!haveIndexCache) {
      drawStatus("Checking updates...");
    } else {
      Serial.println("Keep cached index visible during refresh");
    }
    if (!loadPage(0, true, false)) {
      Serial.println("Run flow update failed");
      if (!pageLoaded && haveIndexCache) {
        renderCachedIndexIfAvailable();
      }
    }
  } else if (pageLoaded) {
    drawOverlayStatusBar();
  } else if (haveIndexCache) {
    renderCachedIndexIfAvailable();
    lastStatusText = String(currentPolicy.statusCode);
    drawOverlayStatusBar();
  } else {
    drawStatus("No cache. Waiting...");
  }

  noteUserActivity();
  lastBackgroundRefreshCheckMs = millis();
}

void setup() {
  rtcBootCount++;

  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  delay(1000);

  M5.Display.setRotation(0);
  M5.update();

  restorePersistedState();
  sampleBatteryIfNeeded(true);
  invalidateAllCaches();

  if (!SD.begin()) {
    Serial.println("SD init failed");
    drawStatus("SD init failed");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("SD init OK");

  primeIndexVersionFromSd();
  runWakeFlow();
  printCounters();
}

void loop() {
  M5.update();

  handleTouchInput();

  uint32_t beforeSample = lastBatterySampleMs;
  sampleBatteryIfNeeded(false);
  if (lastBatterySampleMs != beforeSample && pageLoaded) {
    drawOverlayStatusBar();
  }

  runPeriodicIndexRefreshIfDue();

  if (pageLoaded && (millis() - lastUserActivityMs >= currentPolicy.idleSleepMs)) {
    lastStatusText = "IDLE";
    drawOverlayStatusBar();
    lastUserActivityMs = millis();
  }

  delay(LOOP_WAIT_MS);
}
