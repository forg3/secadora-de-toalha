# Secador de Toalhas Inteligente em Alumínio (390 W / 127 V~)

Projeto completo de engenharia para um secador e aquecedor de toalhas de alto rendimento térmico, estruturado em perfis de alumínio e controlado por placa de circuito impresso dedicada (KiCad 10) baseada no microcontrolador ESP32-C3.

---

## 1. Objetivo do Projeto

Desenvolver um secador de toalhas residencial autônomo, seguro e com acabamento de produto comercial, capaz de secar e higienizar toalhas de banho de forma rápida, eficiente e com consumo racional de energia. O projeto une eletrônica de potência com isolamento galvânico estrito, firmware em C++17 com controle térmico por histerese, conectividade Wi-Fi para agendamentos e uma estrutura mecânica leve com elevada condutividade térmica.

---

## 2. O Problema Enfrentado

1. **Proliferação bacteriana e umidade crônica**: Toalhas úmidas em banheiros com baixa ventilação permanecem molhadas por longos períodos, propiciando proliferação de fungos, bactérias e odores desagradáveis.
2. **Ineficiência de modelos comerciais**: Secadores de toalha de entrada utilizam potências muito baixas (40 W a 70 W), funcionando apenas como aquecedores brandos e demorando horas para secar toalhas pesadas de algodão.
3. **Alto custo e robustez precária**: Modelos importados de maior potência utilizam estruturas pesadas em aço inox com resistências secas ou óleo térmico de difícil manutenção, com preços proibitivos no mercado nacional.
4. **Complexidade e risco elétrico**: O ambiente de banheiro (área molhada) exige rigorosa conformidade com normas de segurança (NBR 5410 / IEC 60950). Soluções improvisadas baseadas em relés mecânicos ou caixas industriais em trilho DIN resultam em cabeamento exposto, risco de choque elétrico e queima prematura por condensação de vapor.

---

## 3. Hipóteses e Soluções Avaliadas

Durante a concepção da engenharia do produto (documentada no comparativo técnico interativo `comparativo-secador-aluminio.html`), foram analisadas quatro grandes hipóteses de arquitetura:

### Hipótese A: Eletrônica Modular (Trilho DIN) vs. Placa de Circuito Impresso Dedicada
- **Módulos Industriais (Controlador + SSR + Fonte externa)**: Embora simplifique o circuito preliminar, a fiação necessária para interligar fonte chaveada, relé de estado sólido (SSR) e microcontrolador ocupa volume excessivo, encarece o conjunto (R$ 571 em eletrônica por unidade) e cria múltiplos pontos de falha mecânica por vibração ou corrosão de terminais.
- **Placa Dedicada Integrada (Solução Escolhida)**: A placa customizada de 115 × 75 mm reúne em uma única PCB de 2 camadas a fonte AC/DC isolada (HLK-PM01), o disparo do Triac via optoacoplador com zero-crossing (MOC3063) e o soquete do ESP32-C3 SuperMini. Elimina cabos soltos e reduz a montagem a conexões de bornes padronizados.

### Hipótese B: Elemento de Aquecimento: Cartuchos Blindados vs. Fio de Silicone
- **6 Cartuchos Resistivos de 500 mm**: Custo aproximado de R$ 420 sob encomenda, com rigidez mecânica que complica o roteamento interno nas dobras dos perfis e gera pontos de aquecimento concentrados.
- **Fio Aquecedor em Silicone (Solução Escolhida)**: Cabo flexível isolado com liga resistiva passando em ida e volta pelas 6 barras tubulares (~R$ 121). Proporciona aquecimento perfeitamente homogêneo ao longo de toda a extensão tubular, montagem simplificada e alta durabilidade sob dilatação térmica.

### Hipótese C: Material Estrutural: Aço Inoxidável vs. Perfis de Alumínio
- **Aço Inox**: Baixa condutividade térmica (~16 W/m·K), peso elevado e maior custo de usinagem e corte.
- **Alumínio Estrutural (Solução Escolhida)**: Excelente condutividade térmica (~205 W/m·K), garantindo transferência quase instantânea de calor do elemento interno para a superfície de contato da toalha. Além disso, o perfil externo de alumínio atua como dissipador térmico direto para o Triac de potência.

### Hipótese D: Estratégia de Controle Térmico e Conectividade
- **Termostato Bimetálico Simples (XH-W3001)**: Econômico, porém desprovido de temporização dinâmica, conectividade, curvas de histerese programáveis ou telemetria.
- **Microcontrolador ESP32-C3 (Solução Escolhida)**: Permite controle fino com histerese (50–60 °C ajustável), watchdog de segurança em tempo real, Modo Eco para economia de energia e interface Web Wi-Fi para acionamento programado (ex.: pré-aquecimento matinal e pós-banho).

---

## 4. Arquitetura de Hardware e Segurança em Camadas

O circuito opera em **127 V~ / 60 Hz**, demandando **3,1 A** em regime pleno (potência nominal de **390 W**):

```
[ Redes 127 V~ ] ──> Fusível T5A ──> Filtro MOV/X2 ──> Triac BTA16 ──> KSD301 (85°C) ──> Fusível Térmico (102°C) ──> Barras
                                                            ▲
                                        [ BARREIRA DE ISOLAÇÃO: 8 mm + Slot Fresado ]
                                                            │
[ Lado Baixa Tensão ] ──> HLK-PM01 (5V) ──> ESP32-C3 ──> MOC3063 (Zero-Cross) ──> NTC 10k + Display TM1637
```

### Domínio de Alta Tensão (HV - 127 V~)
- **Proteção Primária**: Fusível de ação retardada T5A contra curto-circuito, varistor de óxido metálico (MOV 14D431K) para supressão de surtos de rede e capacitor de segurança X2 de 100 nF / 275 VAC para filtragem de EMI.
- **Comutação de Potência**: Triac BTA16-600B montado externamente à PCB, parafusado diretamente no perfil de alumínio aterrado. A aba do encapsulamento TOP3/TO-220 isolada (RG) garante isolação elétrica total mantendo dissipação térmica passiva eficiente (~4 W de calor rejeitado).
- **Proteção Térmica Redundante em Hardware**:
  - Termostato bimetálico **KSD301 (85 °C NF)** em contato direto com a estrutura, interrompendo a linha de alimentação caso ocorra superaquecimento independente do microcontrolador.
  - Fusível térmico irreversível de **102 °C** em série como salvaguarda contra falha mecânica de contato do KSD301.

### Barreira de Isolação Física e Galvânica
- **Isolação Reforçada**: Distância de fuga (creepage) e isolação em ar (clearance) de **≥ 8 mm** entre trilhas de alta e baixa tensão, em conformidade com as normas IEC 60950 e IEC 60335.
- **Rasgo Fresado (Milling Slot)**: Ranhura de 2 × 55 mm na camada `Edge_Cuts` da placa sob a área de isolamento galvânico, impedindo arcos elétricos e correntes de fuga mesmo sob condensação de umidade.
- **Acoplamento Óptico**: Disparo através do optotriac **MOC3063TVM** com detector de passagem por zero (zero-crossing) e isolação galvânica de 5.000 V RMS.

### Domínio de Baixa Tensão (LV - 5 V / 3.3 V)
- **Alimentação**: Módulo AC/DC isolado **HLK-PM01** (tensão de isolamento de 3.000 V RMS).
- **Processamento**: Módulo ESP32-C3 SuperMini (Wi-Fi + BLE, RISC-V).
- **Sensoriamento**: Termistor NTC 10k B3950 inserido na barra principal para feedback térmico contínuo.
- **Interface Local**: Módulo de display TM1637 de 4 dígitos e botões táteis de ajuste.

---

## 5. Firmware e Lógica de Controle

O firmware foi desenvolvido em C++17 com separação completa entre regras de negócio (`firmware/src/control.h`) e drivers de hardware (`firmware/src/main.cpp`), permitindo compilação e testes unitários em ambiente Linux sem necessidade do emulador de hardware.

### Recursos de Software:
1. **Controle por Histerese**: Alvo ajustável (default 55 °C com histerese de 3 °C).
2. **Modo Eco**: Redução de 10 °C no setpoint e ciclo com modulação por tempo (máximo 5 minutos ligado a cada janela de 10 minutos).
3. **Classificação de Falhas de Sensor**:
   - Tensão ADC > 2.800 mV: Detecção de sensor aberto (fio solto).
   - Tensão ADC < 60 mV: Detecção de sensor em curto-circuito.
   - Temperatura lida < 0 °C ou > 75 °C: Interrupção imediata da condução do Triac.
   - Trava de proteção (lockout) após 3 leituras de falha consecutivas, exigindo reinicialização do dispositivo.
4. **Watchdog Timer**: Reinício automático do ESP32-C3 em caso de travamento do loop principal.

---

## 6. Estrutura do Repositório

```
├── comparativo-secador-aluminio.html      # Estudo técnico comparativo, custos e balanço térmico
├── prompt master kicad.md                 # Diretrizes e regras de automação EDA no KiCad 10
├── Esquemático Completo do Secador...png  # Diagrama esquemático ilustrativo
├── Infográfico Técnico_ Secador...png     # Diagrama em blocos e fluxo de potência
├── pcb/                                   # Projeto de hardware no KiCad 10
│   ├── secador-toalha.kicad_pcb           # Layout da PCB (115 × 75 mm, 2 camadas)
│   ├── build_board.py                     # Script Python gerador da placa via pcbnew API
│   ├── fabricacao/                        # Pacote completo para fábrica (Turnkey PCBWay)
│   │   ├── secador-toalha-gerbers.zip     # Arquivos Gerber e furação
│   │   ├── secador-toalha-bom-pcbway.xlsx # Lista de componentes (BOM formatada)
│   │   ├── secador-toalha-posicao.csv     # Coordenadas Pick & Place (Centroid)
│   │   ├── secador-toalha-montagem.pdf    # Guia visual de montagem de componentes
│   │   └── pedido-pcbway.txt              # Instruções e parâmetros para cotação
│   └── gerbers/                           # Camadas gerber individuais (GTL, GBL, etc.)
└── firmware/                              # Firmware em C++17 para ESP32-C3
    ├── src/
    │   ├── control.h                      # Motor de regras e controle térmico puro
    │   └── main.cpp                       # Código principal (Arduino / PlatformIO)
    ├── test/
    │   └── test_control.cpp               # Suíte de testes unitários para execução em PC
    └── firmware.ino                       # Sketch Arduino compatível
```

---

## 7. Instruções de Fabricação e Montagem

1. **Placa de Circuito**: O pacote em `pcb/fabricacao/` contém todos os arquivos validados para cotação e montagem Turnkey (ex.: PCBWay). Todos os componentes da placa utilizam montagem Through-Hole (THT), facilitando inspeção e montagem manual.
2. **Gravação do Firmware**: Gravar o ESP32-C3 exclusivamente via porta USB-C com a placa desconectada da rede elétrica 127 V~.
3. **Comissionamento em Bancada**:
   - Utilize uma lâmpada incandescente de 60 W conectada em série no borne da carga (`J2`) durante o primeiro teste na tomada para validar o chaveamento zero-crossing sem risco de sobrecorrente.
   - Verifique com multímetro o aterramento do chassi e a atuação do termostato mecânico.
4. **Instalação Residencial (NBR 5410)**:
   - É mandatório ligar o pino de terra (PE) ao condutor de proteção da residência.
   - O circuito de tomada do banheiro deve ser protegido por Disjuntor Residual (DR) de alta sensibilidade (30 mA).
