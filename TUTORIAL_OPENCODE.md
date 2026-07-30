# Tutorial: Configurar Acesso ao OpenCode Go

Este guia explica como configurar o monitoramento do OpenCode Go no dispositivo.
Você pode fazer isso de **duas formas**: pela tela touch do ESP32 ou pelo navegador (webserver).

---

## Pré-requisitos

1. Conta no [OpenCode](https://opencode.ai) com assinatura **Go** ativa
2. Dispositivo conectado ao Wi-Fi
3. Firmware com suporte OpenCode instalado

---

## Passo 1: Ativar o modo OpenCode

### Pela tela do ESP32

1. Na tela principal, pressione **longo** o botão BOOT (ou toque no ícone ⚙️ engrenagem)
2. Role até **"Provedor"** (ou **"Provider"**)
3. Toque para alternar entre:
   - **Claude Code** (padrão)
   - **OpenCode Go**
4. A tela vai recarregar com o logo do OpenCode

### Pelo navegador (webserver)

1. Abra `http://claude-stick.local/settings` (ou `http://opencode-stick.local/settings`)
2. Digite o PIN de 4 dígitos para desbloquear
3. No formulário, selecione **OpenCode Go** no campo "Provedor"
4. Clique em **Salvar**

---

## Passo 2: Obter o Workspace ID

O Workspace ID é um identificador único do seu workspace OpenCode.

### Como achar

1. Acesse [https://opencode.ai/auth](https://opencode.ai/auth) e faça login
2. Na barra de endereço do navegador, copie o ID depois de `/workspace/`:

   ```
   https://opencode.ai/workspace/cly8x7k3w0001abc123def456
                                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^
                                 Este é o Workspace ID
   ```

### Inserir no dispositivo

| Pela tela touch | Pelo navegador |
|-----------------|----------------|
| Ajustes → **"OpenCode Workspace ID"** → teclado virtual | `/settings` → campo "Workspace ID" → colar |

---

## Passo 3: Obter o Auth Cookie

O cookie de autenticação permite que o ESP32 acesse seu dashboard de consumo.
**Ele expira periodicamente** (sessão do navegador) — quando expirar, repita este passo.

### Como achar (Chrome / Edge / Brave)

1. Acesse [https://opencode.ai/auth](https://opencode.ai/auth) e faça login
2. Abra as **Ferramentas do Desenvolvedor**:
   - `F12` ou `Ctrl+Shift+I` (Windows/Linux)
   - `Cmd+Option+I` (macOS)
3. Vá na aba **Application** (Aplicação)
4. No menu lateral, expanda **Cookies** e clique em `https://opencode.ai`
5. Procure o cookie chamado **`auth`**
6. **Clique duas vezes** no campo **Value** e copie todo o conteúdo

   ```
   ┌─────────────────────────────────────────────────┐
   │ Application                                     │
   │  ▸ Local Storage                                 │
   │  ▸ Session Storage                               │
   │  ▼ Cookies                                       │
   │      https://opencode.ai                         │
   │                                                  │
   │  Name    Value                     Domain   ...  │
   │  ──────  ───────────────────────   ──────        │
   │  auth    eyJhbGciOiJIUzI1NiIs...   opencode.ai   │  ← esta linha
   │                                                  │
   └─────────────────────────────────────────────────┘
   ```

### Como achar (Firefox)

1. Acesse [https://opencode.ai/auth](https://opencode.ai/auth) e faça login
2. Abra as **Ferramentas do Desenvolvedor** (`F12` ou `Cmd+Option+I`)
3. Vá na aba **Storage** (Armazenamento)
4. Expanda **Cookies** → `https://opencode.ai`
5. Procure o cookie **`auth`** e copie o valor

### Como achar (Safari)

1. Acesse [https://opencode.ai/auth](https://opencode.ai/auth) e faça login
2. Habilite o menu **Desenvolvedor**:
   - Safari → Preferências → Avançado → "Mostrar menu Desenvolvedor"
3. Menu **Desenvolvedor** → **Mostrar Inspetor Web**
4. Vá na aba **Armazenamento** → **Cookies** → `opencode.ai`
5. Procure **`auth`** e copie o valor

### Inserir no dispositivo

| Pela tela touch | Pelo navegador |
|-----------------|----------------|
| Ajustes → **"OpenCode Auth Cookie"** → colar (toque longo para colar) | `/settings` → campo "Auth Cookie" → colar |

> ⚠️ **Importante:** O cookie de sessão expira após alguns dias (ou quando você faz logout).
> Quando o dashboard mostrar "Cookie expirado", repita este passo.

---

## Passo 4: Verificar

Após configurar os 3 campos (provedor + workspace ID + cookie), o dispositivo fará a primeira consulta em até 5 minutos.

### Sinais de que está funcionando

- Header mostra o **ícone do OpenCode** em vez do Clawd
- Tile "AGORA" mostra **3 janelas**: 5h, Semanal e Mensal
- Indicador de status **verde** no header (último fetch OK)

### Sinais de problema

| O que aparece | Significado | Solução |
|---------------|-------------|---------|
| ⚠ **âmbar** no header | Último fetch falhou mas dados antigos ainda válidos | Aguarde o próximo poll automático |
| ⚠ **vermelho** no header | Erro persistente | Toque no ⚠ para ver detalhes |
| "Cookie expirado" | Auth cookie inválido | Refaça o **Passo 3** |
| "Sem conexão" | Wi-Fi fora do ar | Verifique a rede |

---

## Resumo rápido

```
1. Ajustes → Provedor → OpenCode Go
2. Ajustes → OpenCode Workspace ID → colar ID da URL
3. Ajustes → OpenCode Auth Cookie → colar cookie do navegador
4. Aguardar até 5 min → dashboard atualiza
```

### Sempre que o cookie expirar (a cada ~7 dias):

```
DevTools → Application → Cookies → opencode.ai → auth → copiar
Ajustes → OpenCode Auth Cookie → colar
```

---

## Como funciona (resumo técnico)

O ESP32 acessa a **página do dashboard** do OpenCode (não a API de LLM) usando o cookie de sessão como autenticação. A página contém dados de consumo em formato legível por máquina (SolidJS SSR). O dispositivo extrai as porcentagens de uso das janelas de 5 horas, semanal e mensal — sem gastar tokens, sem depender de bridge, sem depender de um computador ligado.

A consulta acontece a cada **5 minutos no mínimo** (para não sobrecarregar o servidor) e usa cache HTTP (ETag) para evitar baixar a página quando os dados não mudaram.
