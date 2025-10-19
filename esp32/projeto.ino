#include <DHT.h>

#define PIN_DHT   23     // DHT22 DATA
#define PIN_LDR   34     // LDR (A0 do módulo) -> ADC
#define PIN_RELE  5      // IN do relé (ativo em LOW)
#define PIN_N     18     // Botão N
#define PIN_P     19     // Botão P
#define PIN_K     21     // Botão K

#define DHTTYPE   DHT22
DHT dht(PIN_DHT, DHTTYPE);


const float PH_MIN        = 6.0;   // janela pH aceitável
const float PH_MAX        = 6.8;
const int   HUM_ON_TH     = 45;    // liga irrigação abaixo de 45%
const int   HUM_OFF_TH    = 55;    // desliga quando >=55% (histerese)
bool requireAllNPK        = true;  // true = exige N AND P AND K

bool forecastRain = false;         // true => inibe irrigação
unsigned long rainHoldUntil = 0;
const unsigned long RAIN_HOLD_MS = 30UL * 60UL * 1000UL; // 30 min

bool  pumpOn = false;
bool  useEMA = true;
float humEMA = NAN;
const float EMA_ALPHA = 0.30f;     // filtro de umidade

const int ADC_MIN = 0;             // LDR escuro
const int ADC_MAX = 4095;          // LDR muito iluminado

static inline float clampf(float x, float lo, float hi) {
  return (x < lo) ? lo : (x > hi ? hi : x);
}

float ldrToPH(int raw) {
  float norm;
  if (ADC_MAX > ADC_MIN) norm = (float)(raw - ADC_MIN) / (float)(ADC_MAX - ADC_MIN);
  else norm = raw / 4095.0f;
  return clampf(norm, 0.0f, 1.0f) * 14.0f;
}

void setPump(bool on) {            // Relé ativo em LOW
  pumpOn = on;
  digitalWrite(PIN_RELE, on ? LOW : HIGH);
}

void maybeUpdateRainFlagFromSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'R' || c == 'r') {
      forecastRain = true;
      rainHoldUntil = millis() + RAIN_HOLD_MS;
      Serial.println(F("[Meteo] Chuva prevista: irrigacao suspensa (30min)."));
    } else if (c == 'C' || c == 'c') {
      forecastRain = false;
      rainHoldUntil = 0;
      Serial.println(F("[Meteo] Chuva cancelada: irrigacao liberada."));
    }
  }
  if (forecastRain && rainHoldUntil > 0 && millis() > rainHoldUntil) {
    forecastRain = false;
    rainHoldUntil = 0;
    Serial.println(F("[Meteo] Janela de chuva expirou."));
  }
}

void setup() {
  Serial.begin(9600);
  dht.begin();

  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, HIGH); // garante bomba desligada

  pinMode(PIN_N, INPUT_PULLUP);
  pinMode(PIN_P, INPUT_PULLUP);
  pinMode(PIN_K, INPUT_PULLUP);

  Serial.println(F("FarmTech Fase 2 - ESP32/Wokwi"));
  Serial.println(F("Comandos: 'R' (chuva), 'C' (cancela)."));
}

void loop() {
  maybeUpdateRainFlagFromSerial();

  float hum = dht.readHumidity();     // %
  float tmp = dht.readTemperature();  // °C 
  int   ldr = analogRead(PIN_LDR);    // 0..4095
  bool Nok = (digitalRead(PIN_N) == LOW); // pressionado = LOW (OK)
  bool Pok = (digitalRead(PIN_P) == LOW);
  bool Kok = (digitalRead(PIN_K) == LOW);

  if (isnan(hum) || isnan(tmp)) {
    Serial.println(F("[ERR] Falha ao ler DHT22."));
    delay(1200);
    return;
  }

  float humUsed;
  if (useEMA) {
    if (isnan(humEMA)) humEMA = hum;
    humEMA = EMA_ALPHA * hum + (1.0f - EMA_ALPHA) * humEMA;
    humUsed = humEMA;
  } else {
    humUsed = hum;
  }

  float ph = ldrToPH(ldr);
  bool npkOK = requireAllNPK ? (Nok && Pok && Kok) : (Nok || Pok || Kok);
  bool phOK  = (ph >= PH_MIN && ph <= PH_MAX);

  if (!forecastRain) {
    if (!pumpOn && (humUsed < HUM_ON_TH) && npkOK && phOK) {
      setPump(true);
    } else if (pumpOn && (humUsed >= HUM_OFF_TH)) {
      setPump(false);
    }
  } else {
    if (pumpOn) setPump(false);
  }

  Serial.print(F("Umid(%): ")); Serial.print(humUsed, 1);
  Serial.print(F("  Temp(C): ")); Serial.print(tmp, 1);
  Serial.print(F("  LDR: ")); Serial.print(ldr);
  Serial.print(F("  pH: ")); Serial.print(ph, 2);
  Serial.print(F("  N: ")); Serial.print(Nok ? "OK" : "--");
  Serial.print(F("  P: ")); Serial.print(Pok ? "OK" : "--");
  Serial.print(F("  K: ")); Serial.print(Kok ? "OK" : "--");
  Serial.print(F("  pHwin: ")); Serial.print(phOK ? "OK" : "OUT");
  Serial.print(F("  NPKrule: ")); Serial.print(npkOK ? "OK" : "NO");
  Serial.print(F("  Rain: ")); Serial.print(forecastRain ? "YES" : "NO");
  Serial.print(F("  PUMP: ")); Serial.println(pumpOn ? "ON" : "OFF");

  delay(1200);
}
