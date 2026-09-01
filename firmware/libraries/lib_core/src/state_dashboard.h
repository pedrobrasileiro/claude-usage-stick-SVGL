#pragma once
#include <lvgl.h>
#include "config.h"
#include "state_app.h"   // NMODELS
#include "history.h"     // HIST_MAX

// ============================================================
// Ponteiros de UI do dashboard e buffers de desenho — o bloco
// mais volátil do app: invalidado via memset em render_state()
// a cada troca de tela. Qualquer módulo que guarde esses
// lv_obj_t* tem que respeitar esse ciclo de vida — não cachear
// ponteiro fora do tempo de vida da tela que o criou.
// ============================================================

#define NTILES 4
#define NSEG 18                       // segmentos do medidor de janela
struct DashUI {
  lv_obj_t *tv, *tile[NTILES], *dots[NTILES];
  lv_obj_t *refBar;
  lv_obj_t *errInd;               // indicador de erro (circulo verde/amb/vermelho)
#ifdef BATTERY_ADC_PIN
  lv_obj_t *battIcon, *battPct;   // icone + porcentagem da bateria (cabecalho)
#endif
  // agora (overview + reset mesclados)
  lv_obj_t *agChip, *agPct5, *agCd5, *agAt5;
  lv_obj_t *agPct7, *agCd7, *agAt7, *agTok;
  lv_obj_t *seg5[NSEG], *seg7[NSEG];  // medidores segmentados
  // modelos
  lv_obj_t *mChip[NMODELS], *incident;
  // tendência da janela 5h (linhas custom)
  lv_obj_t *trHist, *trProj, *trDot, *trCap, *trT0, *trT1;
  // ritmo por hora
  lv_obj_t *heat[24], *heatBtn[4];
};
extern DashUI g_ui;
extern int g_curTile;

// pontos das linhas do gráfico de tendência (precisam persistir)
extern lv_point_precise_t g_trPts[HIST_MAX];
extern lv_point_precise_t g_trProjPts[2];

// ---- Mascotes Clawd oficiais (pagina de modelos; humor por status) ----
// mood: 0=nunca sondado, 1=ok, 2=limitado(429), 3=erro/incidente, 4=n/d(404)
struct Mascot { lv_obj_t *cont, *img, *lid[2], *drop; int baseY, mood; };
extern Mascot g_masc[NMODELS];
extern int g_mascN;
extern lv_point_precise_t g_mXPts[NMODELS][4][2];   // olhos em X (mood 3)

// ---- Ponteiros reusados entre telas (header do dashboard / PIN) ----
extern lv_obj_t *g_hdrStatus;   // texto de status no cabeçalho do dashboard
extern lv_obj_t *g_pinDots, *g_pinMsg, *g_pinLockBar;
