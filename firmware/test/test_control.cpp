// Teste da lógica de aquecimento no PC: g++ -std=c++17 test/test_control.cpp -o /tmp/t && /tmp/t
#include <cassert>
#include <cstdio>
#include "../src/control.h"

int main() {
  // Sensor: 10k a 25 °C dá metade da tensão.
  assert(std::fabs(ntcCelsius(1650) - 25) < 0.1);
  assert(classify(ntcCelsius(3000)) == Fault::SensorOpen);
  assert(classify(ntcCelsius(20)) == Fault::SensorShort);
  assert(classify(80) == Fault::OverTemp);

  Settings s;  // alvo 55 °C, histerese 3 °C
  Controller c;
  assert(!c.update(Mode::Off, s, 40, 0));
  assert(c.update(Mode::Normal, s, 50, 0));   // frio: liga
  assert(c.update(Mode::Normal, s, 54, 0));   // subindo dentro da faixa: continua ligado
  assert(!c.update(Mode::Normal, s, 55, 0));  // chegou: desliga
  assert(!c.update(Mode::Normal, s, 53, 0));  // esfriando dentro da faixa: continua desligado
  assert(c.update(Mode::Normal, s, 52, 0));   // caiu 3 °C: religa

  // Eco: alvo 45 °C, liga só nos primeiros 5 min de cada 10.
  Controller e;
  assert(e.update(Mode::Eco, s, 40, 0));
  assert(!e.update(Mode::Eco, s, 40, 6UL * 60 * 1000));
  assert(e.update(Mode::Eco, s, 40, 11UL * 60 * 1000));
  assert(!e.update(Mode::Eco, s, 46, 12UL * 60 * 1000));

  // Falha: uma leitura ruim desliga mas não trava; três seguidas travam até reiniciar.
  Controller f;
  assert(!f.update(Mode::Normal, s, -INFINITY, 0) && f.fault == Fault::None);
  assert(f.update(Mode::Normal, s, 40, 0));
  for (int i = 0; i < 3; i++) f.update(Mode::Normal, s, -INFINITY, 0);
  assert(f.fault == Fault::SensorOpen);
  assert(!f.update(Mode::Normal, s, 40, 0));  // sensor voltou, mas continua travado

  Controller h;
  for (int i = 0; i < 3; i++) h.update(Mode::Normal, s, 80, 0);
  assert(h.fault == Fault::OverTemp);
  for (int i = 0; i < 300; i++) h.update(Mode::Normal, s, 80, 0);  // contador não estoura
  assert(h.fault == Fault::OverTemp);

  puts("control.h: todos os testes passaram");
}
