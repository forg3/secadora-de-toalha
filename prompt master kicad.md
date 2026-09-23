```markdown
# PROMPT-MESTRE: Geração Automatizada de PCBs no KiCad 10 via Python API

> **Contexto de destino:** Claude (Sonnet/Opus) e Antigravity IDE atuando como engenheiros de automação EDA.
> **Alvo de execução:** KiCad 10.0.6 x86_64 (release build, Fedora 44 Workstation, GCC 16.2.1, Boost 1.90, wxGTK 3.2.9, KICAD_IPC_API=ON).
> **Objetivo:** gerar, corrigir e otimizar placas de circuito impresso profissionalmente a partir de scripts Python (`pcbnew`), com roteamento automático funcional, bibliotecas completas e validação por DRC.

---

## 1. REGRAS DE OURO (nunca violar)

1. **Nunca inventar nomes de footprint.** Sempre verificar existência em disco antes de usar:
   ```bash
   find /usr/share/kicad/footprints -name "<nome>.kicad_mod" | head
   ls /usr/share/kicad/footprints/ | grep -i <lib>
   ```
   Em Fedora o pacote é `kicad-footprints`; instalar com `sudo dnf install kicad-footprints kicad-symbols kicad-packages3d kicad-doc-pt`.

2. **Nunca usar `PROJECT_NAME` sem antes defini-lo.** Erro clássico do v1 original.

3. **Nunca atribuir a mesma net a dois pinos do mesmo componente** sem justificativa topológica (curto acidental). Ex.: `R_LED2 pin1=pin2` é sempre bug.

4. **Nunca rotear tudo em `B_Cu`.** Regra: potência em `B_Cu` (trilha grossa), lógica/sinais em `F_Cu`. Cruzamentos exigem `PCB_VIA`.

5. **Nunca declarar "pronto" sem rodar DRC.** Rodar:
   ```bash
   kicad-cli pcb drc --exit-code-violations board.kicad_pcb
   ```
   Zero violações de erro é o critério de aceite. Warnings podem ser justificados em comentário.

6. **Nunca ignorar a barreira de isolação** para placas com 127/220 V~. Slot fresado (`Edge_Cuts`) + clearance ≥ 8 mm entre domínios HV e LV, único cruzamento pelo optoacoplador.

7. **Nunca usar clearance < 0.2 mm** para 127 V~. Mínimo absoluto: HV = 3 mm (IEC 60950 básico) / 8 mm (reforçado); LV = 0.2 mm.

8. **Silenciar o assert do KiCad 10** (bug conhecido do `PROPERTY_ENUM` no bind Python — é cosmético):
   ```bash
   python3 build_board.py 2>/dev/null
   ```
   Nunca tratar o assert como erro fatal — o script roda até o fim mesmo com ele.

---

## 2. COMPATIBILIDADE KiCad 10.0.6 (diferenças vs. 7/8/9)

| API antiga | API KiCad 10 | Observação |
|-----------|--------------|------------|
| `pcbnew.FromMM(x)` | `pcbnew.FromMM(x)` | OK, mantido |
| `SetMinClearance()` | `ds.GetDefault().SetClearance()` | Removido em 9 |
| `EDA_ANGLE(rot, pcbnew.DEGREES_T)` | idem | OK |
| `pcbnew.NETINFO_ITEM(b, name)` | idem | OK |
| `FootprintLoad(dir, name)` | idem | Retorna `None` em falha — checar |
| `pcbnew.PCB_SHAPE(b)` | idem | OK |
| `SetNet()` em pad | idem | Requer net pré-existente |
| `BuildConnectivity()` | idem | Obrigatório antes do save |
| Auto-route | `kicad-cli pcb autoroute` **não existe** | Usar Specctra DSN + Freerouting |
| IPC API | `KICAD_IPC_API=ON` | Habilita automação via socket (v10) |

**Notas específicas do Fedora:**
- `pcbnew` fica em `/usr/lib64/python3.*/site-packages/pcbnew.py` (instalado por `kicad` RPM).
- `python3-pcbnew` **não** é pacote separado — vem embutido.
- Bibliotecas em `/usr/share/kicad/{footprints,symbols,3dmodels,template}`.
- Se faltar `pcbnew`, instalar `sudo dnf install kicad-python` (se existir) ou usar o Python do próprio KiCad.
- Wayland + wxGTK 3.2.9: se a GUI travar no `kicad-cli`, forçar `GDK_BACKEND=x11`.

---

## 3. PIPELINE OBRIGATÓRIO DE GERAÇÃO

```
[1] Descoberta de footprints reais    →  bash find/ls nos /usr/share/kicad
[2] Validação de pinout do símbolo    →  parse do .kicad_sym com sexpdata
[3] Geração do .kicad_pcb via pcbnew  →  script Python
[4] BuildConnectivity + Save          →  sempre
[5] Net classes HV/LV/ISO             →  script ou kicad-cli
[6] Zones (GND plane, HV plane)       →  pcbnew.ZONE ou kicad-cli
[7] DRC automatizado                  →  kicad-cli pcb drc
[8] Auto-route                        →  DSN → freerouting → SES
[9] Refill zones                      →  kicad-cli pcb render / refill
[10] Gerbers + drill                  →  kicad-cli pcb export gerbers/drill
[11] Inspeção 3D (opcional)           →  kicad-cli pcb render --side top
```

Cada etapa falha → **abortar e reportar**; nunca continuar em estado inconsistente.

---

## 4. TEMPLATE DE SCRIPT `pcbnew` (canônico)

```python
#!/usr/bin/env python3
"""Template canônico — KiCad 10 / Fedora 44."""
import os, sys, subprocess, pcbnew

PROJECT_DIR  = os.path.expanduser("~/Downloads/<projeto>")
PROJECT_NAME = "<projeto>"
os.makedirs(PROJECT_DIR, exist_ok=True)

FP_ROOTS = [
    "/usr/share/kicad/footprints",
    "/usr/share/kicad/modules",
    os.path.expanduser("~/.local/share/kicad/footprints"),
]

def load_fp(lib, name):
    """Carrega footprint com fallback multi-root e erro claro."""
    for root in FP_ROOTS:
        d = os.path.join(root, lib)
        if not os.path.isdir(d):
            continue
        fp = pcbnew.FootprintLoad(d, name)
        if fp:
            return fp
    raise FileNotFoundError(f"{lib}/{name} não encontrado em {FP_ROOTS}")

def main():
    b = pcbnew.BOARD()
    ds = b.GetDesignSettings()
    ds.m_TrackMinWidth = pcbnew.FromMM(0.25)
    ds.m_ViasMinSize   = pcbnew.FromMM(0.6)
    try:
        ds.GetDefault().SetClearance(pcbnew.FromMM(0.3))
    except Exception:
        pass  # KiCad < 9

    # ... contorno, nets, componentes, rotas, silk ...

    b.BuildConnectivity()
    out = os.path.join(PROJECT_DIR, f"{PROJECT_NAME}.kicad_pcb")
    b.Save(out)
    print(f"OK -> {out}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

**Convenções obrigatórias:**
- Nomes de net: `SCREAMING_SNAKE_CASE` (`AC_L_FUSED`, `+5V`, `GND`).
- Refs: `U*` (CI), `R*`, `C*`, `L*`, `D*`, `Q*`, `J*` (conector), `SW*`, `LED*`, `Y*` (cristal), `H*` (mounting), `JP*` (jumper).
- Cada `add_text` deve ter tamanho ≥ 0.8 mm (legibilidade).
- Comentário no topo do script com: versão, data, topologia, tensão, corrente, barreira de isolação.

---

## 5. NET CLASSES (obrigatório para HV)

Criar via `pcbnew`:

```python
def add_netclass(b, name, clearance_mm, track_mm, via_mm, via_drill_mm, nets):
    nc = pcbnew.NETCLASS(name)
    nc.SetClearance(pcbnew.FromMM(clearance_mm))
    nc.SetTrackWidth(pcbnew.FromMM(track_mm))
    nc.SetViaDiameter(pcbnew.FromMM(via_mm))
    nc.SetViaDrill(pcbnew.FromMM(via_drill_mm))
    b.GetDesignSettings().m_NetClasses.Add(nc)
    for n in nets:
        ni = b.FindNet(n)
        if ni:
            ni.SetNetClass(nc)
```

**Classes mínimas para placas 127 V~:**

| Classe | Clearance | Track | Via | Nets |
|--------|-----------|-------|-----|------|
| `HV` | 3.0 mm | 2.0 mm | 1.2/0.6 | `AC_L_*`, `AC_N`, `EARTH_PE` |
| `ISO_HV` | 8.0 mm | 0.8 mm | 1.0/0.5 | `GATE_DRV`, `TRIAC_GATE`, `OPTO_DRIVE` |
| `LV` | 0.3 mm | 0.4 mm | 0.8/0.4 | `+5V`, `+12V`, `GND`, sinais |
| `PWR_LV` | 0.3 mm | 1.0 mm | 0.8/0.4 | `+12V`, `+5V` principais |

---

## 6. AUTOROTEAMENTO (não há nativo no KiCad 10)

**Fluxo canônico:**
```bash
# 1) Exportar Specctra
kicad-cli pcb export dsn --output board.dsn board.kicad_pcb

# 2) Rodar Freerouting (Java)
java -jar freerouting.jar \
  -de board.dsn -do board.ses \
  -mp 100 -mt 4 -oit 5

# 3) Importar sessão
kicad-cli pcb import ses --input board.ses board.kicad_pcb

# 4) Refill de zonas
kicad-cli pcb refill-zones board.kicad_pcb
```

**Se não tiver Java:** instalar `sudo dnf install java-21-openjdk` + baixar `freerouting-1.9.0.jar` de [github.com/freerouting/freerouting/releases](https://github.com/freerouting/freerouting/releases).

**Alternativa 100% Python:** roteador *maze* próprio. Só recomendado para placas < 4 camadas e < 50 nets. Complexidade O(n²) — inviável para HV com regras de clearance altas.

**Regra prática:** para HV (clearance 3–8 mm), **rotear manualmente o domínio HV** e só auto-rotear o LV. Freerouting não respeita net class corretamente em placas com clearance > 2 mm.

---

## 7. BAIXAR BIBLIOTECAS ADICIONAIS

**Fontes oficiais:**
```bash
# Já vem no Fedora:
sudo dnf install kicad-footprints kicad-symbols kicad-packages3d

# Bibliotecas de terceiros (git clone para ~/.local/share/kicad/):
git clone https://github.com/kicad/kicad-footprints ~/.local/share/kicad/footprints-extra
git clone https://github.com/Digi-Key/digikey-kicad-library
git clone https://github.com/Seeed-Studio/OPL_Kicad_Library
git clone https://github.com/sparkfun/SparkFun_KiCad_Library

# Registre em ~/.config/kicad/10.0/fp-lib-table:
# (fp_lib_table
#   (lib (name "Digi-Key")(type "KiCad")(uri "${KIPRJMOD}/digikey-kicad-library/...")(options "")(descr ""))
# )
```

**Serviços online:**
- **SnapEDA** (`snapeda.com`) — baixa `.kicad_mod` + `.kicad_sym` + 3D.
- **Ultra Librarian** (`ultralibrarian.com`) — exporta KiCad 6+.
- **Component Search Engine** (`componentsearchengine.com`).
- **SamacSys** (`componentsearchengine.com`) — plugin no KiCad.

**Regra:** validar cada footprint baixado com `kicad-cli fp upgrade` e conferir visualmente via `kicad-cli fp render`.

---

## 8. DRC / ERC — COMANDOS DE ACEITE

```bash
# DRC elétrico + geométrico
kicad-cli pcb drc \
  --output drc_report.txt \
  --severity-all \
  --exit-code-violations \
  board.kicad_pcb

# Verificação de conectividade (nets não roteadas)
kicad-cli pcb drc --format json board.kicad_pcb | jq '.violations[] | select(.type=="unconnected")'

# Inspeção de furos
kicad-cli pcb export drill --format excellon --output drill/ board.kicad_pcb

# Render para inspeção visual
kicad-cli pcb render --side top --quality high --output top.png board.kicad_pcb
kicad-cli pcb render --side bottom --quality high --output bot.png board.kicad_pcb
```

**Critério de aceite:**
- `unconnected = 0` (obrigatório)
- `clearance_violations = 0` (obrigatório)
- `track_width < 0.2 mm` em HV: erro
- `courtyard_overlap` em HV: erro
- `silkscreen_overlap` com pad: warning aceitável se ≤ 2

---

## 9. CHECKLIST DE SEGURANÇA (placas 127/220 V~)

- [ ] Barreira de isolação física (slot `Edge_Cuts`) ≥ 1 mm de largura entre HV e LV
- [ ] Clearance HV→LV ≥ 8 mm (reforçado) ou ≥ 3 mm (básico) — documentar qual
- [ ] Único cruzamento por optoacoplador (MOC3063, PC814, EL817) — **nunca** jumper direto
- [ ] Fusível na fase de entrada, **antes** de qualquer outro componente
- [ ] MOV (varistor) fase↔neutro, classe X2 ≥ 275 V~ para 127 V~, 385 V~ para 220 V~
- [ ] Cap X2 fase↔neutro para EMI
- [ ] Terminal de terra (PE) ligado a **chassi** e a **anel fechado** de mounting holes
- [ ] Termostato de segurança NC **em série com o gate** do triac/relé (kill independente do MCU)
- [ ] Watchdog + brown-out detect + EEPROM no MCU
- [ ] Marcação silkscreen: "127V~", "5A", "ISOLATION", "PE", "CUIDADO: ALTA TENSÃO"
- [ ] Furo de 4 mm para parafuso M3 com pad de terra nos mounting holes
- [ ] Label do modelo de placa, revisão e data no silk

---

## 10. ESTRUTURA DE PROJETO RECOMENDADA

```
projeto/
├── docs/
│   ├── schematic.pdf
│   ├── bom.csv
│   └── drc_report.txt
├── firmware/
│   ├── main.c
│   └── Makefile
├── scripts/
│   ├── build_board.py
│   ├── generate_project.py
│   └── validate_board.py
├── gerbers/
│   ├── top.gtl
│   ├── bottom.gbl
│   ├── drill.drl
│   └── *.zip
├── projeto.kicad_pro
├── projeto.kicad_sch
├── projeto.kicad_pcb
└── README.md
```

---

## 11. ERROS COMUNS E CORREÇÕES

| Sintoma | Causa | Correção |
|---------|-------|----------|
| `PROPERTY_ENUM assert` | Bug KiCad 10 | `2>/dev/null` |
| `FootprintLoad` retorna `None` | Nome de lib/fp errado | `find /usr/share/kicad/footprints -name "*.kicad_mod"` |
| `SetMinClearance` não existe | Removido no KiCad 9 | Usar `GetDefault().SetClearance()` |
| `NameError: PROJECT_NAME` | Esqueceu de definir | Definir no topo |
| Pads sem net | Pad number ≠ chave do dict | Imprimir `[p.GetNumber() for p in fp.Pads()]` |
| DRC: unconnected | Trilha não roteada ou net faltando | `BuildConnectivity()` + rodar DRC |
| DRC: clearance | Net classes faltando | Criar `HV`, `LV`, `ISO` |
| Freerouting ignora clearance | Net class não exportada | Editar `.dsn` manualmente ou rotear HV na mão |
| GUI trava no Wayland | wxGTK 3.2.9 + wayland | `GDK_BACKEND=x11 kicad ...` |
| Bibliotecas não achadas | `KICAD*_PATH` não setado | `export KICAD10_FOOTPRINT_DIR=/usr/share/kicad/footprints` |

---

## 12. COMANDO DE INVOCAÇÃO (para Claude / Antigravity)

Ao receber uma tarefa de geração de PCB, o agente deve:

1. **Perguntar** (se não especificado): tensão de operação, corrente máxima, topologia (linear/switching/AC), barreira de isolação (sim/não), conectores de entrada/saída.
2. **Rodar** `python3 -c "import pcbnew; print(pcbnew.GetBuildVersion())" 2>/dev/null` para confirmar ambiente.
3. **Listar** footprints necessários e validar existência em disco.
4. **Gerar** script seguindo template §4.
5. **Executar** com `2>/dev/null` (silenciar assert).
6. **Validar** com `kicad-cli pcb drc` (§8).
7. **Reportar** em markdown: número de pads, nets, tracks, violações DRC, warnings justificados, próximos passos.
8. **Nunca** declarar sucesso sem DRC limpo.

---

## 13. REFERÊNCIAS

- KiCad Python API: https://docs.kicad.org/doxygen-python-10.0/
- IPC-2221B (clearance vs. tensão): Tabela 6-1
- IEC 60950-1 / IEC 62368-1 (isolação HV/LV)
- NBR 5410 (instalações elétricas BT)
- NR-10 (segurança em eletricidade)
- Freerouting: https://github.com/freerouting/freerouting
- KiCad CLI docs: `kicad-cli --help` e `kicad-cli pcb --help`

---
