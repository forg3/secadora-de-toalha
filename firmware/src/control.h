#pragma once
// Decisão de ligar/desligar as barras. Não depende do Arduino: compila e é testado no PC
// (g++ -std=c++17 test/test_control.cpp -o /tmp/t && /tmp/t).
#include <cmath>
#include <cstdint>

enum class Mode : uint8_t { Off, Normal, Eco };
enum class Fault : uint8_t { None, SensorOpen, SensorShort, OverTemp };

struct Settings {
  float setpointC = 55;  // temperatura da barra no modo normal
  float hystC = 3;       // religa quando a barra cai hystC abaixo do alvo
  float offsetC = 0;     // calibração do NTC: compare com um termômetro encostado na barra
};

constexpr float ECO_DROP_C = 10;                     // eco: alvo 10 °C abaixo do normal
constexpr uint32_t ECO_WINDOW_MS = 10UL * 60 * 1000; // eco: janela de 10 min...
constexpr uint32_t ECO_ON_MS = ECO_WINDOW_MS / 2;    // ...com no máximo 5 min ligado
constexpr float OPEN_MV = 2800;   // acima: NTC aberto (o ADC do C3 satura perto de 2,9 V)
constexpr float SHORT_MV = 60;    // abaixo: NTC em curto
constexpr float MIN_VALID_C = 0;  // banheiro nunca fica abaixo de 0 °C: leitura assim é fio solto
constexpr float MAX_SAFE_C = 75;  // corte por software; o KSD301 corta em 85 °C por hardware
constexpr uint8_t FAULT_READS = 3;  // leituras ruins seguidas para travar em erro

// NTC 10k B3950 para GND e 10k para 3V3; mv = tensão no pino do ADC.
inline float ntcCelsius(float mv, float vccMv = 3300) {
  if (mv >= OPEN_MV) return -INFINITY;
  if (mv <= SHORT_MV) return INFINITY;
  float r = 10000.0f * mv / (vccMv - mv);
  return 1.0f / (1.0f / 298.15f + std::log(r / 10000.0f) / 3950.0f) - 273.15f;
}

inline Fault classify(float c) {
  if (std::isnan(c) || c < MIN_VALID_C) return Fault::SensorOpen;
  if (std::isinf(c)) return Fault::SensorShort;
  if (c > MAX_SAFE_C) return Fault::OverTemp;
  return Fault::None;
}

struct Controller {
  bool heating = false;       // memória da histerese
  Fault fault = Fault::None;  // travado até desligar o aparelho da tomada
  uint8_t badReads = 0;

  // Chamado a cada leitura do sensor. Retorna true se o triac deve conduzir.
  bool update(Mode mode, const Settings& s, float c, uint32_t nowMs) {
    Fault f = classify(c);
    if (f != Fault::None) {
      heating = false;
      if (badReads < FAULT_READS && ++badReads == FAULT_READS) fault = f;
      return false;  // desliga já na primeira leitura ruim; só trava depois de 3 seguidas
    }
    badReads = 0;
    if (fault != Fault::None || mode == Mode::Off) return heating = false;

    float target = mode == Mode::Eco ? s.setpointC - ECO_DROP_C : s.setpointC;
    if (c >= target) heating = false;
    else if (c <= target - s.hystC) heating = true;  // entre os dois limites: mantém o estado

    bool ecoPause = mode == Mode::Eco && nowMs % ECO_WINDOW_MS >= ECO_ON_MS;
    return heating && !ecoPause;
  }
};
