#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// Claude Usage Stick — Fikra ES3C28P (ESP32-S3, 2.8" ILI9341V SPI,
// touch capacitivo FT6336G I2C). Pinos conforme Manual Técnico Fikra 2.0
// (fikra.com.br/esp32). Lógica de negócio/UI compartilhada com o CYD via
// firmware/lib_core/ — ver firmware/README.md.
// ============================================================

// ── Firmware ─────────────────────────────────────────────
#define FW_VERSION              "2.1-fikra"

// ── Display SPI (ILI9341V, 4 linhas) ─────────────────────
#define TFT_MOSI  11
#define TFT_MISO  13
#define TFT_SCLK  12
#define TFT_CS    10
#define TFT_DC    46
#define TFT_RST   -1   // compartilhado com CHIP_PU (reset geral do ESP32-S3)
#define TFT_BL    45   // aceita PWM

#define SCREEN_WIDTH   320   // após rotação — painel nativo é 240x320 portrait
#define SCREEN_HEIGHT  240
#define TFT_ROTATION   1     // aposta inicial p/ landscape (mesma orientação do CYD) — validar no bring-up físico
#define SPI_FREQ       40000000UL

// ── Touch I2C (FT6336G, endereço 0x38) ───────────────────
#define TOUCH_SDA  16
#define TOUCH_SCL  15
#define TOUCH_RST  18
#define TOUCH_INT  17
#define TOUCH_ADDR 0x38

// ── LED RGB endereçável (WS2812, embutido na placa) ──────
#define RGB_LED_PIN 42

// ── Navegação ─────────────────────────────────────────────
#define CFG_BOOT_PIN            0     // IO0/BOOT, ativo em LOW (fallback)
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
