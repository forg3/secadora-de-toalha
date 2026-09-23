#!/usr/bin/env python3
"""Placa de controle do secador de toalhas — rev A, 2026-09-12.

KiCad 10.0.6 / Fedora 44. Uso:  python3 build_board.py 2>/dev/null
Topologia : rede 127 V~ -> fusível T5A -> MOV + X2 -> BTA16 (zero-cross via MOC3063) -> KSD301 85 °C
            + fusível térmico 102 °C (fora da placa) -> 6 barras, 390 W / 3,1 A.
Comando   : HLK-PM01 (5 V isolado) -> ESP32-C3 SuperMini em soquetes, NTC 10k, painel TM1637 + 2 botões.
Isolação  : reforçada. Faixa de 8 mm sem cobre + rasgo fresado de 2 mm; só HLK-PM01 e MOC3063
            (MOC3063TVM, terminais a 10,16 mm, pino 5 em furo sem cobre) atravessam a faixa.
Etapas    : monta a placa -> DSN -> Freerouting -> SES -> DRC -> Gerber.
"""
import json, os, subprocess, sys
import pcbnew
from pcbnew import FromMM, VECTOR2I

PROJECT_DIR  = os.path.dirname(os.path.abspath(__file__))
PROJECT_NAME = "secador-toalha"
PCB = os.path.join(PROJECT_DIR, f"{PROJECT_NAME}.kicad_pcb")
FP_ROOT = "/usr/share/kicad/footprints"
FREEROUTING = os.path.expanduser("~/.local/share/freerouting/freerouting.jar")
OX, OY, W, H = 30.0, 30.0, 115.0, 75.0          # origem na folha e tamanho da placa (mm)
BARRIER = (57.2, 63.8)   # keepout p/ o roteador; os 8 mm HV x LV são garantidos pela regra do .kicad_dru
SLOT = (59.5, 61.5, 10.0, 65.0)                  # rasgo fresado x0, x1, y0, y1

def P(x, y):
    return VECTOR2I(FromMM(OX + x), FromMM(OY + y))

R_P10 = ("Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal")
C_DISC = ("Capacitor_THT", "C_Disc_D5.0mm_W2.5mm_P5.00mm")
TB3 = ("TerminalBlock_Phoenix", "TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal")
SOCK8 = ("Connector_PinSocket_2.54mm", "PinSocket_1x08_P2.54mm_Vertical")

# ref, footprint, valor, x, y, rotação, {pad: net}
PARTS = [
    # --- domínio HV (x < 56.5) ---
    ("J1", TB3, "AC_IN L/N/PE", 12, 8, 0, {"1": "AC_L", "2": "AC_N", "3": "EARTH_PE"}),
    ("F1", ("Fuse", "Fuseholder_Cylinder-5x20mm_Schurter_0031_8201_Horizontal_Open"), "T5A 250V", 29, 8, 0,
     {"1": "AC_L", "2": "AC_L_FUSED"}),
    ("RV1", ("Varistor", "RV_Disc_D12mm_W4.2mm_P7.5mm"), "14D431K", 24, 20, 0, {"1": "AC_L_FUSED", "2": "AC_N"}),
    ("C1", ("Capacitor_THT", "C_Rect_L13.0mm_W6.0mm_P10.00mm_FKS3_FKP3_MKS4"), "100nF X2 275V", 38, 20, 0,
     {"1": "AC_L_FUSED", "2": "AC_N"}),
    ("PS1", ("Converter_ACDC", "Converter_ACDC_Hi-Link_HLK-PMxx"), "HLK-PM01", 45, 36, 0,
     {"1": "AC_L_FUSED", "2": "AC_N", "3": "GND", "4": "+5V"}),
    ("R1", R_P10, "360R", 40, 58, 0, {"1": "AC_L_FUSED", "2": "MOC_OUT"}),
    ("R2", R_P10, "330R", 8, 54, 0, {"1": "TRIAC_GATE", "2": "HEAT_SW"}),
    # BTA16-600B (aba isolada) fora da placa, parafusado no perfil de alumínio: dissipa ~4 W a 390 W.
    ("J7", TB3, "BTA16 G/A2/A1", 11, 63, 0, {"1": "TRIAC_GATE", "2": "AC_L_FUSED", "3": "HEAT_SW"}),
    ("J2", TB3, "RES N PE", 30, 67, 0, {"1": "HEAT_SW", "2": "AC_N", "3": "EARTH_PE"}),
    ("U1", ("Package_DIP", "DIP-6_W10.16mm"), "MOC3063", 65.06, 60, 180,
     {"1": "MOC_LED", "2": "GND", "4": "TRIAC_GATE", "6": "MOC_OUT"}),
    # --- domínio LV (x > 64.5) ---
    ("C2", ("Capacitor_THT", "CP_Radial_D5.0mm_P2.00mm"), "10uF 16V", 82, 34, 0, {"1": "+5V", "2": "GND"}),
    ("C3", C_DISC, "100nF", 82, 44, 0, {"1": "+5V", "2": "GND"}),
    ("R3", R_P10, "150R", 70, 58, 0, {"1": "MOC_LED", "2": "TRIAC_DRV"}),
    ("R4", R_P10, "10k", 70, 64, 0, {"1": "GND", "2": "TRIAC_DRV"}),
    ("R5", R_P10, "10k", 70, 70, 0, {"1": "+3V3", "2": "NTC_ADC"}),
    ("R6", R_P10, "10k", 90, 62, 0, {"1": "+3V3", "2": "TM_DIO"}),
    ("C4", C_DISC, "100nF", 88, 70, 0, {"1": "NTC_ADC", "2": "GND"}),
    ("J3", ("Connector_JST", "JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical"), "NTC 10k", 98, 70, 0,
     {"1": "NTC_ADC", "2": "GND"}),
    ("J4", ("Connector_JST", "JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical"), "PAINEL", 90, 55, 0,
     {"1": "+3V3", "2": "GND", "3": "TM_CLK", "4": "TM_DIO", "5": "BTN_UP", "6": "BTN_DN"}),
    # ESP32-C3 SuperMini: fileiras a 15,24 mm, pino 1 = lado do USB-C.
    # Fileira A (5V GND 3V3 IO4 IO3 IO2 IO1 IO0) é a única cuja ordem importa (alimentação).
    ("J5", SOCK8, "ESP32-C3 fila A", 88, 8, 0,
     {"1": "+5V", "2": "GND", "3": "+3V3", "4": "TRIAC_DRV", "5": "TM_CLK", "6": "TM_DIO", "7": "BTN_UP", "8": "NTC_ADC"}),
    # Fileira B, pino 3 = IO7 ou IO10 conforme o fabricante; ambos são GPIO livres (ajuste só no firmware).
    ("J6", SOCK8, "ESP32-C3 fila B", 103.24, 8, 0, {"3": "BTN_DN"}),
]
HOLES = [(4, 4), (111, 4), (4, 71), (111, 71)]

NETCLASSES = {  # nome: (clearance, trilha, via, furo) mm
    "HV":     (1.3, 1.5, 1.6, 0.8),   # IPC-2221B B2, 151–300 V: 1,25 mm; 1,5 mm leva 3,1 A com folga
    "HV_SIG": (1.3, 0.6, 1.2, 0.6),
    "LV_PWR": (0.3, 0.8, 0.8, 0.4),
}
PATTERNS = {"AC_*": "HV", "HEAT_SW": "HV", "EARTH_PE": "HV", "MOC_OUT": "HV_SIG", "TRIAC_GATE": "HV_SIG",
            "+5V": "LV_PWR", "+3V3": "LV_PWR", "GND": "LV_PWR"}

DRU = """(version 1)
(rule "isolacao reforcada HV x LV"
  (constraint clearance (min 8mm))
  (condition "(A.hasNetclass('HV') || A.hasNetclass('HV_SIG')) && !(B.hasNetclass('HV') || B.hasNetclass('HV_SIG')) && B.NetName != ''"))
"""

def line(b, layer, x0, y0, x1, y1, w=0.1):
    s = pcbnew.PCB_SHAPE(b)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(P(x0, y0)); s.SetEnd(P(x1, y1))
    s.SetLayer(layer); s.SetWidth(FromMM(w))
    b.Add(s)

def rect(b, layer, x0, y0, x1, y1):
    for a, c, d, e in ((x0, y0, x1, y0), (x1, y0, x1, y1), (x1, y1, x0, y1), (x0, y1, x0, y0)):
        line(b, layer, a, c, d, e)

def text(b, s, x, y, size=1.0, rot=0):
    t = pcbnew.PCB_TEXT(b)
    t.SetText(s); t.SetPosition(P(x, y)); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(VECTOR2I(FromMM(size), FromMM(size))); t.SetTextThickness(FromMM(size * 0.15))
    t.SetTextAngle(pcbnew.EDA_ANGLE(rot, pcbnew.DEGREES_T))
    b.Add(t)

def load_fp(lib, name):
    fp = pcbnew.FootprintLoad(os.path.join(FP_ROOT, lib + ".pretty"), name)
    if not fp:
        raise FileNotFoundError(f"{lib}/{name}")
    return fp

def build():
    b = pcbnew.NewBoard(PCB)
    ds = b.GetDesignSettings()
    ds.m_TrackMinWidth = FromMM(0.25)
    ds.m_ViasMinSize = FromMM(0.6)
    ds.m_CopperEdgeClearance = FromMM(0.5)
    ns = ds.m_NetSettings
    ns.GetDefaultNetclass().SetClearance(FromMM(0.3))
    ns.GetDefaultNetclass().SetTrackWidth(FromMM(0.4))
    for name, (cl, tw, vd, vdr) in NETCLASSES.items():
        nc = pcbnew.NETCLASS(name)
        nc.SetClearance(FromMM(cl)); nc.SetTrackWidth(FromMM(tw))
        nc.SetViaDiameter(FromMM(vd)); nc.SetViaDrill(FromMM(vdr))
        ns.SetNetclass(name, nc)
    for pat, name in PATTERNS.items():
        ns.SetNetclassPatternAssignment(pat, name)

    for net in sorted({n for *_, pads in PARTS for n in pads.values()}):
        b.Add(pcbnew.NETINFO_ITEM(b, net))

    for ref, (lib, name), val, x, y, rot, pads in PARTS:
        fp = load_fp(lib, name)
        fp.SetReference(ref); fp.SetValue(val)
        fp.SetPosition(P(x, y)); fp.SetOrientation(pcbnew.EDA_ANGLE(rot, pcbnew.DEGREES_T))
        if ref == "U1":  # pino 5 (substrato, NC) num furo sem cobre: sem pad a 0,94 mm dos pinos de rede
            for p in fp.Pads():
                if p.GetNumber() == "5":
                    p.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
                    p.SetLayerSet(pcbnew.PAD.UnplatedHoleMask())
                    p.SetNumber("")
                    p.SetSize(pcbnew.F_Cu, VECTOR2I(FromMM(1.0), FromMM(1.0)))
                    p.SetDrillSize(VECTOR2I(FromMM(1.0), FromMM(1.0)))
        numbers = {p.GetNumber() for p in fp.Pads()}
        missing = set(pads) - numbers
        if missing:
            raise KeyError(f"{ref}: pads {missing} não existem em {sorted(numbers)}")
        for p in fp.Pads():
            if p.GetNumber() in pads:
                p.SetNet(b.FindNet(pads[p.GetNumber()]))
        b.Add(fp)

    for i, (x, y) in enumerate(HOLES, 1):
        fp = load_fp("MountingHole", "MountingHole_3.2mm_M3")
        fp.SetReference(f"H{i}"); fp.Reference().SetVisible(False); fp.SetPosition(P(x, y)); b.Add(fp)

    rect(b, pcbnew.Edge_Cuts, 0, 0, W, H)
    rect(b, pcbnew.Edge_Cuts, SLOT[0], SLOT[2], SLOT[1], SLOT[3])

    z = pcbnew.ZONE(b)  # faixa de isolação: sem trilha, via ou preenchimento
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True); z.SetDoNotAllowZoneFills(True)
    z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET(); ls.AddLayer(pcbnew.F_Cu); ls.AddLayer(pcbnew.B_Cu); z.SetLayerSet(ls)
    o = z.Outline(); o.NewOutline()
    for x, y in ((BARRIER[0], 0), (BARRIER[1], 0), (BARRIER[1], H), (BARRIER[0], H)):
        o.Append(FromMM(OX + x), FromMM(OY + y))
    z.SetZoneName("ISOLACAO_8MM")
    b.Add(z)

    g = pcbnew.ZONE(b)  # plano de GND só no domínio LV
    g.SetLayer(pcbnew.B_Cu); g.SetNet(b.FindNet("GND")); g.SetLocalClearance(FromMM(0.3))
    g.SetMinThickness(FromMM(0.3)); g.SetZoneName("GND_LV")
    g.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    o = g.Outline(); o.NewOutline()
    for x, y in ((BARRIER[1], 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (BARRIER[1], H - 0.5)):
        o.Append(FromMM(OX + x), FromMM(OY + y))
    b.Add(g)

    text(b, "ALTA TENSÃO 127V~ 390W", 22, 44, 1.5)
    text(b, "CUIDADO", 22, 47, 1.5)
    text(b, "FUSÍVEL T5A", 8, 16.5)
    text(b, "L    N    PE", 17, 14.5)
    text(b, "RES   N    PE", 35, 59.5)
    text(b, "G    A2   A1", 16, 69.5)
    text(b, "ISOLAÇÃO 8mm", 60.5, 70)
    text(b, "3V3 GND CLK DIO UP DN", 96, 50)
    text(b, "NTC", 105.5, 70)
    text(b, "ESP32-C3 SuperMini  USB ^", 95, 3)
    text(b, "SECADOR v1 2026-09", 100, 38)
    text(b, "WayWayWay", 100, 45)  # local do número do pedido da PCBWay ("Specify a location")
    text(b, "BTA16 no perfil de alumínio", 16, 72.5)

    b.BuildConnectivity()
    pcbnew.SaveBoard(PCB, b)
    with open(os.path.join(PROJECT_DIR, f"{PROJECT_NAME}.kicad_dru"), "w") as f:
        f.write(DRU)
    return b

def route(b):
    dsn = PCB.replace(".kicad_pcb", ".dsn"); ses = PCB.replace(".kicad_pcb", ".ses")
    if not pcbnew.ExportSpecctraDSN(b, dsn):
        raise RuntimeError("falha ao exportar DSN")
    subprocess.run(["java", "-jar", FREEROUTING, "--gui.enabled=false",
                    "--usage_and_diagnostic_data.disable_analytics=true",
                    "-de", dsn, "-do", ses, "-mp", "30", "-mt", "1"],
                   check=True, timeout=600, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not pcbnew.ImportSpecctraSES(b, ses):
        raise RuntimeError("falha ao importar SES")
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    b.BuildConnectivity()
    pcbnew.SaveBoard(PCB, b)

def drc():
    rpt = os.path.join(PROJECT_DIR, "drc.json")
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "--severity-all", "--all-track-errors", "--refill-zones",
                    "--output", rpt, PCB], check=False, stdout=subprocess.DEVNULL)
    r = json.load(open(rpt))
    errs = [v for v in r["violations"] if v["severity"] == "error"]
    warns = [v for v in r["violations"] if v["severity"] != "error"]
    unconn = r.get("unconnected_items", [])
    print(f"DRC: {len(errs)} erros, {len(warns)} avisos, {len(unconn)} não conectados")
    for v in errs[:30]:
        print("  E", v["type"], "|", v["description"], "|", "; ".join(i["description"] for i in v["items"]))
    for v in unconn[:30]:
        print("  U", "; ".join(i["description"] for i in v["items"]))
    return len(errs) + len(unconn)

if __name__ == "__main__":
    board = build()
    if "--no-route" not in sys.argv:
        route(board)
    sys.exit(1 if drc() else 0)
