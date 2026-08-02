#include "providers/opencode_provider.h"
#include "opencode_logo.h"
#include "config.h"
#include "certs.h"
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <time.h>

const lv_image_dsc_t* OpenCodeProvider::logoSmall()    { return &img_oc_sm; }
const lv_image_dsc_t* OpenCodeProvider::logoWordmark() { return &img_oc_wordmark; }
const lv_image_dsc_t* OpenCodeProvider::logoBig()      { return &img_oc_big; }
const lv_image_dsc_t* OpenCodeProvider::logoXl()       { return &img_oc_xl; }

// Extrai um numero float apos um label (ex: "usagePercent:45.2" ou "usagePercent: 45.2")
static bool extractFloat(const String& haystack, int start, int blockEnd,
                          const char* label, float& out) {
    int pos = haystack.indexOf(label, start);
    if (pos < 0 || pos > blockEnd) return false;
    pos += strlen(label);                                  // pula o label
    while (pos < blockEnd && (haystack.charAt(pos) == ':' || haystack.charAt(pos) == ' ')) pos++;
    int end = pos;
    while (end < blockEnd && (isDigit(haystack.charAt(end)) || haystack.charAt(end) == '.' || haystack.charAt(end) == '-')) end++;
    if (end == pos) return false;
    out = haystack.substring(pos, end).toFloat();
    return out >= 0 && out <= 100;
}

// Extrai um long apos um label
static bool extractLong(const String& haystack, int start, int blockEnd,
                         const char* label, long& out) {
    int pos = haystack.indexOf(label, start);
    if (pos < 0 || pos > blockEnd) return false;
    pos += strlen(label);
    while (pos < blockEnd && (haystack.charAt(pos) == ':' || haystack.charAt(pos) == ' ')) pos++;
    int end = pos;
    while (end < blockEnd && isDigit(haystack.charAt(end))) end++;
    if (end == pos) return false;
    out = haystack.substring(pos, end).toInt();
    return out >= 0;
}

bool OpenCodeProvider::parseWindow(const String& html, const char* prefix, float& pct, long& sec) {
    int start = html.indexOf(prefix);
    if (start < 0) return false;

    // Busca usagePercent e resetInSec num raio de 400 chars
    int windowEnd = start + 400;
    if (windowEnd > (int)html.length()) windowEnd = html.length();

    // Extrai usagePercent — procura label + numero
    int pctAt = html.indexOf("usagePercent", start);
    if (pctAt < 0 || pctAt > windowEnd) {
        pctAt = html.indexOf("UsagePercent", start);
    }
    if (pctAt >= 0 && pctAt < windowEnd) {
        int numStart = html.indexOf(':', pctAt);
        if (numStart >= 0 && numStart < windowEnd) {
            numStart++; // pula o ':'
            while (numStart < windowEnd && (html.charAt(numStart) == ' ' || html.charAt(numStart) == '\t')) numStart++;
            int numEnd = numStart;
            while (numEnd < windowEnd && (isDigit(html.charAt(numEnd)) || html.charAt(numEnd) == '.' || html.charAt(numEnd) == ',')) numEnd++;
            if (numEnd > numStart) {
                String val = html.substring(numStart, numEnd);
                val.replace(',', '.');
                pct = val.toFloat();
            }
        }
    }

    // Extrai resetInSec
    int secAt = html.indexOf("resetInSec", start);
    if (secAt < 0 || secAt > windowEnd) {
        secAt = html.indexOf("ResetInSec", start);
    }
    if (secAt >= 0 && secAt < windowEnd) {
        int numStart = html.indexOf(':', secAt);
        if (numStart >= 0 && numStart < windowEnd) {
            numStart++;
            while (numStart < windowEnd && (html.charAt(numStart) == ' ' || html.charAt(numStart) == '\t')) numStart++;
            int numEnd = numStart;
            while (numEnd < windowEnd && isDigit(html.charAt(numEnd))) numEnd++;
            if (numEnd > numStart) {
                sec = html.substring(numStart, numEnd).toInt();
            }
        }
    }

    return pct >= 0 && pct <= 200 && sec >= 0;
}

// Fallback: parse data-slot HTML (formato novo do dashboard).
bool OpenCodeProvider::parseDataSlot(const String& html, OpenCodeUsage& out) {
    int idx = html.indexOf("data-slot=\"usage-item\"");
    if (idx < 0) return false;

    int found = 0;
    String h = html.substring(idx);

    while ((idx = h.indexOf("data-slot=\"usage-item\"")) >= 0 && found != 7) {
        h = h.substring(idx + 1);
        int blockEnd = h.indexOf("data-slot=\"usage-item\"");
        if (blockEnd < 0) blockEnd = h.length();
        String block = h.substring(0, blockEnd);

        // Label: Rolling Usage, Weekly Usage, Monthly Usage
        int lblAt = block.indexOf("data-slot=\"usage-label\"");
        String label = "";
        if (lblAt >= 0) {
            int gt = block.indexOf('>', lblAt);
            int lt = block.indexOf('<', gt);
            if (gt >= 0 && lt > gt) label = block.substring(gt + 1, lt);
            label.toLowerCase();
        }

        // Value: percentage number
        int valAt = block.indexOf("data-slot=\"usage-value\"");
        float val = -1;
        if (valAt >= 0) {
            int gt = block.indexOf('>', valAt);
            int lt = block.indexOf('<', gt);
            if (gt >= 0 && lt > gt) {
                String vstr = block.substring(gt + 1, lt);
                vstr.replace("%", "");
                val = vstr.toFloat();
            }
        }

        // Reset time
        int rstAt = block.indexOf("data-slot=\"reset-time\"");
        if (rstAt < 0) rstAt = block.indexOf("data-slot=\"reset-now\"");
        long rstSec = -1;
        if (rstAt >= 0) {
            // Se for reset-now, o reset eh 0
            if (block.indexOf("reset-now", rstAt) >= 0 && block.indexOf("reset-now", rstAt) < block.indexOf('>', rstAt)) {
                rstSec = 0;
            } else {
                int gt = block.indexOf('>', rstAt);
                int lt = block.indexOf('<', gt);
                if (gt >= 0 && lt > gt) {
                    String tstr = block.substring(gt + 1, lt);
                    tstr.replace("<!--$-->", "");
                    tstr.replace("<!--/-->", "");
                    tstr.replace("Resets in ", "");
                    tstr.replace("Resets in", "");
                    tstr.trim();
                    tstr.toLowerCase();

                    // Parse "X days Y hours Z minutes"
                    long total = 0;
                    int dAt = tstr.indexOf("day");
                    if (dAt >= 0) {
                        int s = dAt;
                        while (s > 0 && isDigit(tstr.charAt(s - 1))) s--;
                        total += tstr.substring(s, dAt).toInt() * 86400L;
                    }
                    int hAt = tstr.indexOf("hour");
                    if (hAt >= 0) {
                        int s = hAt;
                        while (s > 0 && isDigit(tstr.charAt(s - 1))) s--;
                        total += tstr.substring(s, hAt).toInt() * 3600L;
                    }
                    int mAt = tstr.indexOf("minute");
                    if (mAt >= 0) {
                        int s = mAt;
                        while (s > 0 && isDigit(tstr.charAt(s - 1))) s--;
                        total += tstr.substring(s, mAt).toInt() * 60L;
                    }
                    if (total > 0 || tstr.indexOf("now") >= 0) rstSec = total;
                }
            }
        }

        if (val >= 0 && val <= 100 && rstSec >= 0) {
            if (label.indexOf("roll") >= 0 || label.indexOf("5h") >= 0) {
                if (!(found & 1)) { out.rollingPct = val; out.rollingSec = rstSec; found |= 1; }
            } else if (label.indexOf("week") >= 0) {
                if (!(found & 2)) { out.weeklyPct = val; out.weeklySec = rstSec; found |= 2; }
            } else if (label.indexOf("month") >= 0) {
                if (!(found & 4)) { out.monthlyPct = val; out.monthlySec = rstSec; found |= 4; }
            }
        }

        if (blockEnd >= (int)h.length()) break;
    }
    return found != 0;
}

bool OpenCodeProvider::fetchDashboardUsage(const char* workspaceId,
                                           const char* authCookie,
                                           OpenCodeUsage& out) {
    out.ok = false;
    out.errorCode = 0;
    out.errorAtMs = millis();
    out.errorMsg[0] = 0;
    out.errorHint[0] = 0;

    if (!workspaceId || !workspaceId[0]) {
        out.errorCode = -4;
        strncpy(out.errorMsg, "Workspace ID nao configurado", sizeof(out.errorMsg) - 1);
        strncpy(out.errorHint, "Configure nas Ajustes.", sizeof(out.errorHint) - 1);
        _lastCode = out.errorCode;
        strncpy(_lastMsg, out.errorMsg, sizeof(_lastMsg) - 1);
        strncpy(_lastHint, out.errorHint, sizeof(_lastHint) - 1);
        return false;
    }
    if (!authCookie || !authCookie[0]) {
        out.errorCode = -4;
        strncpy(out.errorMsg, "Cookie de sessao nao configurado", sizeof(out.errorMsg) - 1);
        strncpy(out.errorHint, "Configure nas Ajustes.", sizeof(out.errorHint) - 1);
        _lastCode = out.errorCode;
        strncpy(_lastMsg, out.errorMsg, sizeof(_lastMsg) - 1);
        strncpy(_lastHint, out.errorHint, sizeof(_lastHint) - 1);
        return false;
    }

    HTTPClient http;
    String url = String("https://opencode.ai/workspace/") + workspaceId + "/go";
    http.begin(url);
    http.addHeader("User-Agent", OC_USER_AGENT);
    http.addHeader("Accept", "text/html");
    http.addHeader("Cookie", "auth=" + String(authCookie));
    http.setTimeout(OC_SCRAPE_TIMEOUT_MS);

    int httpCode = http.GET();

    if (httpCode == 301 || httpCode == 302) {
        http.end();
        out.errorCode = httpCode;
        strncpy(out.errorMsg, "Cookie invalido ou expirado", sizeof(out.errorMsg) - 1);
        strncpy(out.errorHint, "Pegue o cookie auth em opencode.ai > DevTools > Application > Cookies", sizeof(out.errorHint) - 1);
        _lastCode = httpCode; strncpy(_lastMsg, out.errorMsg, sizeof(_lastMsg)-1);
        strncpy(_lastHint, out.errorHint, sizeof(_lastHint)-1);
        return false;
    }
    if (httpCode == 304) { http.end(); out.ok = true; out.errorCode = 0; return true; }
    if (httpCode <= 0) {
        http.end();
        out.errorCode = -3;
        strncpy(out.errorMsg, "Falha TLS/DNS", sizeof(out.errorMsg) - 1);
        _lastCode = out.errorCode; strncpy(_lastMsg, out.errorMsg, sizeof(_lastMsg)-1);
        return false;
    }
    if (httpCode != 200) {
        http.end();
        out.errorCode = httpCode;
        snprintf(out.errorMsg, sizeof(out.errorMsg), "HTTP %d", httpCode);
        _lastCode = httpCode; strncpy(_lastMsg, out.errorMsg, sizeof(_lastMsg)-1);
        return false;
    }

    // Le body via stream (buffer 32KB, mais seguro que getString())
    WiFiClient* stream = http.getStreamPtr();
    String html;
    if (!stream) {
        http.end();
        out.errorCode = -3;
        strncpy(out.errorMsg, "Falha ao ler resposta", sizeof(out.errorMsg)-1);
        _lastCode = out.errorCode; strncpy(_lastMsg, out.errorMsg, sizeof(_lastMsg)-1);
        return false;
    }
    char buf[512];
    uint32_t totalRead = 0, startMs = millis();
    const uint32_t MAX_READ = 32768;
    while ((stream->connected() || stream->available()) && totalRead < MAX_READ) {
        if (millis() - startMs > OC_SCRAPE_TIMEOUT_MS) break;
        int avail = stream->available();
        if (avail <= 0) { delay(5); continue; }
        int toRead = avail < (int)sizeof(buf)-1 ? avail : (int)sizeof(buf)-1;
        int len = stream->readBytes(buf, toRead);
        if (len <= 0) break;
        buf[len] = 0;
        totalRead += len;
        html += buf;
    }
    http.end();

    if (html.length() == 0) {
        out.errorCode = -5;
        strncpy(out.errorMsg, "Resposta vazia do servidor", sizeof(out.errorMsg)-1);
        strncpy(out.errorHint, "Tamanho da pagina excede a memoria disponivel.", sizeof(out.errorHint)-1);
        _lastCode = out.errorCode; strncpy(_lastMsg, out.errorMsg, sizeof(_lastMsg)-1);
        return false;
    }

    // ---- Parse: tenta SSR primeiro ----
    int found = 0;
    if (parseWindow(html, "rollingUsage", out.rollingPct, out.rollingSec)) found |= 1;
    if (parseWindow(html, "weeklyUsage",  out.weeklyPct,  out.weeklySec))  found |= 2;
    if (parseWindow(html, "monthlyUsage", out.monthlyPct, out.monthlySec)) found |= 4;
    if (found == 0) { if (parseDataSlot(html, out)) found = 7; }

    if (found == 0) {
        // Diagnostico: o HTML tem "opencode"? "rolling"?
        bool hasOC = html.indexOf("opencode") >= 0 || html.indexOf("OpenCode") >= 0;
        bool hasRolling = html.indexOf("rolling") >= 0 || html.indexOf("Rolling") >= 0;
        if (!hasOC) {
            strncpy(out.errorMsg, "Resposta inesperada (sem OpenCode)", sizeof(out.errorMsg) - 1);
            strncpy(out.errorHint, "O cookie pode estar redirecionando pra pagina errada.", sizeof(out.errorHint) - 1);
        } else if (!hasRolling) {
            strncpy(out.errorMsg, "Formato da pagina mudou", sizeof(out.errorMsg) - 1);
            strncpy(out.errorHint, "HTML sem dados de uso. Atualize o firmware.", sizeof(out.errorHint) - 1);
        } else {
            strncpy(out.errorMsg, "Dados nao encontrados no HTML", sizeof(out.errorMsg) - 1);
            strncpy(out.errorHint, "Tamanho do HTML insuficiente. Tente novamente.", sizeof(out.errorHint) - 1);
        }
        _lastCode = out.errorCode;
        strncpy(_lastMsg, out.errorMsg, sizeof(_lastMsg) - 1);
        strncpy(_lastHint, out.errorHint, sizeof(_lastHint) - 1);
        return false;
    }

    _backoffMs = 0; // reset backoff no sucesso
    out.ok = true;
    out.errorCode = 0;
    out.lastOkMs = millis();
    return true;
}
