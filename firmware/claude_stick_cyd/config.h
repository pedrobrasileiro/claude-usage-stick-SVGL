#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// Claude Usage Stick — ESP32-2432S028 "Cheap Yellow Display" (CYD)
// ESP32 clássico WROOM-32, sem PSRAM. Touch XPT2046 (resistivo),
// validado fisicamente em firmware/bringup_cyd/ — ver
// Manual_Pinagem_Foto.pdf (schematic oficial do fabricante, chip U3).
// Navegação touch, com botão físico BOOT como fallback.
// ============================================================

// ── Firmware ─────────────────────────────────────────────
#define FW_VERSION              "2.1-cyd"

// ── Display SPI (ILI9341) — pinos validados no bring-up ──
#define TFT_MOSI  13
#define TFT_MISO  12
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC    2
#define TFT_RST   -1   // não conectado nessa placa
#define TFT_BL    21

#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240
#define TFT_ROTATION   1
#define SPI_FREQ       40000000UL

// ── Touch SPI (XPT2046) — barramento separado do display, HSPI dedicado ──
#define TOUCH_CLK   25
#define TOUCH_CS    33
#define TOUCH_MOSI  32
#define TOUCH_MISO  39
#define TOUCH_IRQ   36

// Calibração medida no bring-up (4 cantos, unidade física validada)
#define TOUCH_X_MIN 270
#define TOUCH_X_MAX 3660
#define TOUCH_Y_MIN 440
#define TOUCH_Y_MAX 3710

// ── Navegação ─────────────────────────────────────────────
#define CFG_BOOT_PIN            0     // botão físico BOOT, ativo em LOW (fallback)
#define BOOT_LONGPRESS_MS    900

// ── Polling ──────────────────────────────────────────────
#define DEFAULT_POLL_SEC        120
#define MIN_POLL_SEC            30
#define MAX_POLL_SEC            300
#define STATUS_POLL_SEC         300      // status.claude.com a cada 5 min

// ── Segurança (PIN + AES-256-GCM) ────────────────────────
#define PIN_LEN                 4
#define LOCKOUT_BASE_SEC        10       // dobra a cada falha (10,20,40,80... máx 1h)
#define KDF_ROUNDS              10000
#define SETTINGS_SESSION_MS     (5UL * 60UL * 1000UL)  // sessão de /settings desbloqueada

// ── Rede / API Claude ────────────────────────────────────
#define WIFI_CONNECT_TIMEOUT_MS 8000
#define API_TIMEOUT_MS          15000
#define MESSAGES_ENDPOINT       "https://api.anthropic.com/v1/messages"
#define ANTHROPIC_VERSION       "2023-06-01"
#define PROBE_MODEL             "claude-haiku-4-5-20251001"
// status.anthropic.com redireciona para cá — consultar o host canônico direto
#define STATUS_ENDPOINT         "https://status.claude.com/api/v2/incidents/unresolved.json"

// NTP (necessário para os contadores de reset)
#define NTP_SERVER_1            "pool.ntp.org"
#define NTP_SERVER_2            "time.cloudflare.com"

// ── NVS ──────────────────────────────────────────────────
#define NVS_NAMESPACE           "claude"

// ── Portal de configuração (AP de primeiro uso) ──────────
#define PROVISION_AP_SSID       "ClaudeStick-Setup"

// ── OpenCode Go (scraping de dashboard web) ─────────────
#define OC_MIN_POLL_SEC         300      // poll minimo p/ scraping (evita rate-limit)
#define OC_SCRAPE_TIMEOUT_MS    10000    // timeout do GET no dashboard
#define OC_DASHBOARD_URL        "https://opencode.ai/workspace/"
#define OC_DASHBOARD_SUFFIX     "/go"
#define OC_USER_AGENT           "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Gecko/20100101 Firefox/148.0"
// Verde OpenCode (tailwind green-500)
#define OC_ACCENT               0x22C55E
// NVS keys dos campos OpenCode
#define NVS_OC_WSID             "oc_wsid"
#define NVS_OC_COOKIE           "oc_cookie"
#define NVS_PROVIDER            "provider"

#endif // CONFIG_H
