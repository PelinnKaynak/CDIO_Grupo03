#include <Wire.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 adc;

// Calibración (se actualiza en H001)
int lecturaSeco   = 16000;  // SECO
int lecturaMojado = 3400;   // MOJADO
bool estaCalibrado = false;

// Promedia lecturas para estabilizar
int16_t leerPromedioADC(uint8_t canal, uint16_t muestras = 32, uint16_t pausaMs = 5) {
  long suma = 0;
  for (uint16_t i = 0; i < muestras; i++) {
    suma += adc.readADC_SingleEnded(canal);
    delay(pausaMs);
  }
  return (int16_t)(suma / (long)muestras);
}

// Espera a que escribas la palabra indicada (acepta abreviatura inicial)
void esperarPalabraSerie(const String& palabraObjetivo) {
  String objetivo = palabraObjetivo; objetivo.toLowerCase();
  while (true) {
    if (Serial.available()) {
      String s = Serial.readStringUntil('\n');
      s.trim(); s.toLowerCase();
      if (s.length() > 0 && objetivo.startsWith(s)) break;
      Serial.println("Escribe '" + palabraObjetivo + "' y pulsa Enter.");
    }
    delay(10);
  }
}

// H001: Calibración SECO/MOJADO
void calibrarSensor() {
  Serial.println();
  Serial.println("=== Calibracion SECO/MOJADO ===");
  Serial.println("1) Pon el sensor en SECO (sin agua). Escribe 'seco' y Enter.");
  esperarPalabraSerie("seco");
  delay(300);
  int16_t seco = leerPromedioADC(0, 48, 6);
  Serial.print("Medida SECO = "); Serial.println(seco);

  Serial.println("2) Sumerge COMPLETAMENTE el sensor (MOJADO). Escribe 'mojado' y Enter.");
  esperarPalabraSerie("mojado");
  delay(300);
  int16_t mojado = leerPromedioADC(0, 48, 6);
  Serial.print("Medida MOJADO = "); Serial.println(mojado);

  lecturaSeco   = (int)seco;
  lecturaMojado = (int)mojado;
  estaCalibrado = (lecturaSeco != lecturaMojado);

  Serial.println("Resumen:");
  Serial.print("  SECO   = "); Serial.println(lecturaSeco);
  Serial.print("  MOJADO = "); Serial.println(lecturaMojado);

  if (!estaCalibrado) {
    Serial.println("Advertencia: SECO y MOJADO son iguales. Repite la calibracion.");
  } else {
    Serial.println("Listo. Umbral bajo = SECO (0%), umbral alto = MOJADO (100%).");
  }
  Serial.println("===============================");
  Serial.println();
}

// H003: avisos usando umbrales de la calibración
void avisarPorUmbral(float humedadPorcentajeReal) {
  if (!estaCalibrado) return;
  if (humedadPorcentajeReal < 0.0f) {
    Serial.print("AVISO: Humedad BAJA (actual ");
    Serial.print(humedadPorcentajeReal, 1);
    Serial.println("%, umbral bajo 0.0%)");
  } else if (humedadPorcentajeReal > 100.0f) {
    Serial.print("AVISO: Humedad ALTA (actual ");
    Serial.print(humedadPorcentajeReal, 1);
    Serial.println("%, umbral alto 100.0%)");
  }
}

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(600000); // tiempo de espera para escribir por serie

  adc.begin();
  adc.setGain(GAIN_ONE); // ±4.096 V

  calibrarSensor();
}

void loop() {
  // Lectura
  int lecturaActual = adc.readADC_SingleEnded(0);

  // % humedad a partir de tu calibración (valor bruto sin limitar)
  float humedadPorcentajeReal = 0.0f;
  if (estaCalibrado) {
    humedadPorcentajeReal = 100.0f * (lecturaActual - lecturaSeco) / (float)(lecturaMojado - lecturaSeco);
  }

  // Para mostrar “bonito”
  float humedadPorcentaje = constrain(humedadPorcentajeReal, 0.0f, 100.0f);

  // Salida
  Serial.print("Valor digital: ");
  Serial.print(lecturaActual);
  Serial.print(" | Humedad: ");
  Serial.print(humedadPorcentaje, 1);
  Serial.println("%");

  // Avisos fuera de 0–100 según calibración
  avisarPorUmbral(humedadPorcentajeReal);

  Serial.println("------------------------------");
  delay(1000);
}
