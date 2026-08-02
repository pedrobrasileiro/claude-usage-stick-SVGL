#pragma once

// ============================================================
// NVS / persistência — carrega/salva config e blob cifrado,
// aplica brilho e faz o reset de fábrica.
// ============================================================

void load_persisted();
void save_blob();
void save_attempts();
void apply_brightness();
void factory_reset();
