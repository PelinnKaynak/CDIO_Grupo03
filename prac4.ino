// ===================== SALINIDAD — v3 para usuario =====================
// - Conversión ADC -> gramos con polinomio fijo (calculado en Excel)
// - Ajuste rápido del sensor (cero y escala) opcional, con memoria
// - Aviso cuando la salinidad está por debajo o por encima de tus límites
// =======================================================================

#include <EEPROM.h>

#define PIN_ALIMENTACION 5   // pin que alimenta la sonda
#define PIN_ENTRADA     A0   // pin de lectura ADC

// ---------- Polinomio fijo ADC -> gramos (de tu Excel) ----------
const float COEF_A2 = -0.000384727062f;  // x^2
const float COEF_B1 =  0.609727062f;     // x
const float COEF_C0 = -203.027875f;      // c

// ---------- Ajustes guardados (se aplican encima del polinomio) ----------
float ajusteOffsetG = 0.0f;  // suma en gramos
float ajusteEscalaK = 1.0f;  // multiplicador

// ---------- Límites de aviso ----------
float limiteBajoG = 5.0f;    // ajusta a tu cultivo
float limiteAltoG = 30.0f;   // ajusta a tu cultivo
int   estadoLimite = 99;     // -1 bajo, 0 ok, 1 alto, 99 sin evaluar

// ---------- EEPROM ----------
const uint32_t FIRMA_CONF = 0x434F4E46u; // 'CONF'
const uint32_t FIRMA_UMBR = 0x554D4252u; // 'UMBR'

struct DatosConf { uint32_t firma; float offset; float k; };
struct DatosUmbr { uint32_t firma; float bajo;   float alto; };

const int EEPROM_DIR_CONF = 0;
const int EEPROM_DIR_UMBR = EEPROM_DIR_CONF + sizeof(DatosConf);

// ===================== Utilidades serie =====================
char leerTecla(){
  while (!Serial.available()) {}
  char c = Serial.read();
  while (Serial.available()) Serial.read(); // limpia \r\n
  return c;
}

float leerNumero(){
  String s = "";
  while (true){
    if (Serial.available()){
      char ch = Serial.read();
      if (ch == '\n' || ch == '\r'){ if (s.length() > 0) break; }
      else s += ch;
    }
  }
  return s.toFloat();
}

// ============================ Lecturas ============================
int leerADC(){
  digitalWrite(PIN_ALIMENTACION, HIGH);
  delay(100);
  int v = analogRead(PIN_ENTRADA);
  digitalWrite(PIN_ALIMENTACION, LOW);
  delay(100);
  return v;
}

int mediaADC(int n){
  long suma = 0;
  for (int i = 0; i < n; i++) suma += leerADC();
  return (int)(suma / (long)n);
}

// ============ Conversión: polinomio + ajustes ============
float gramosDesdeADC(int x){
  float g = COEF_A2 * x * x + COEF_B1 * x + COEF_C0;
  g = (g + ajusteOffsetG) * ajusteEscalaK;
  if (g < 0) g = 0;
  return g;
}

// ======================= Guardar/cargar =======================
void guardarConf(){
  DatosConf d = { FIRMA_CONF, ajusteOffsetG, ajusteEscalaK };
  EEPROM.put(EEPROM_DIR_CONF, d);
}
bool cargarConf(){
  DatosConf d;
  EEPROM.get(EEPROM_DIR_CONF, d);
  if (d.firma != FIRMA_CONF) return false;
  ajusteOffsetG = d.offset;
  ajusteEscalaK = d.k;
  return true;
}

void guardarUmbrales(){
  DatosUmbr d = { FIRMA_UMBR, limiteBajoG, limiteAltoG };
  EEPROM.put(EEPROM_DIR_UMBR, d);
}
bool cargarUmbrales(){
  DatosUmbr d;
  EEPROM.get(EEPROM_DIR_UMBR, d);
  if (d.firma != FIRMA_UMBR) return false;
  limiteBajoG = d.bajo;
  limiteAltoG = d.alto;
  return true;
}

// ======================= Ajuste del sensor =======================
// Paso 1: “cero” en agua sin sal. Paso 2 (opcional): escala con una solución conocida.
void ajustarSensor(){
  Serial.println();
  Serial.println("AJUSTE DEL SENSOR");
  Serial.println("1) Coloca los electrodos en AGUA SIN SAL y pulsa ENTER.");
  while (Serial.available()) Serial.read();
  while (!Serial.available()) {}
  while (Serial.available()) Serial.read();

  int adcCero = mediaADC(15);
  float gCero = COEF_A2 * adcCero * adcCero + COEF_B1 * adcCero + COEF_C0;
  ajusteOffsetG = -gCero; // que marque 0 g en agua sin sal

  Serial.print("ADC cero = "); Serial.print(adcCero);
  Serial.print(" | marca = "); Serial.print(gCero, 4);
  Serial.print(" g -> offset = "); Serial.println(ajusteOffsetG, 4);

  Serial.println();
  Serial.println("2) ¿Quieres ajustar la escala con una solucion conocida? (s/n)");
  char r = leerTecla();
  if (r == 's' || r == 'S'){
    Serial.println("   Escribe los gramos REALES de esa solucion y pulsa ENTER:");
    float gReal = leerNumero();

    Serial.println("   Mete los electrodos en la solucion y pulsa ENTER.");
    while (Serial.available()) Serial.read();
    while (!Serial.available()) {}
    while (Serial.available()) Serial.read();

    int adcRef = mediaADC(15);
    float gMedida = (COEF_A2 * adcRef * adcRef + COEF_B1 * adcRef + COEF_C0) + ajusteOffsetG;

    if (gMedida <= 0.0001f) ajusteEscalaK = 1.0f;
    else ajusteEscalaK = gReal / gMedida;

    if (ajusteEscalaK < 0.5f) ajusteEscalaK = 0.5f;
    if (ajusteEscalaK > 1.5f) ajusteEscalaK = 1.5f;

    Serial.print("ADC ref = "); Serial.print(adcRef);
    Serial.print(" | marca (tras offset) = "); Serial.print(gMedida, 4); Serial.println(" g");
    Serial.print("Escala = "); Serial.println(ajusteEscalaK, 4);
  } else {
    ajusteEscalaK = 1.0f;
  }

  guardarConf();
  Serial.println("Ajuste guardado.");
  Serial.println();
}

// ========================= Límites de aviso =========================
void configurarLimites(){
  Serial.println();
  Serial.println("CONFIGURAR LIMITES");
  Serial.println("Introduce LIMITE BAJO (g) y pulsa ENTER:");
  float bajo = leerNumero();
  Serial.println("Introduce LIMITE ALTO (g) y pulsa ENTER:");
  float alto = leerNumero();

  if (bajo < 0) bajo = 0;
  if (alto < 0) alto = 0;
  if (alto < bajo){ float t = bajo; bajo = alto; alto = t; }

  limiteBajoG = bajo;
  limiteAltoG = alto;
  guardarUmbrales();

  Serial.print("Guardado: bajo="); Serial.print(limiteBajoG, 3);
  Serial.print(" g | alto="); Serial.print(limiteAltoG, 3); Serial.println(" g");
  estadoLimite = 99;
  Serial.println();
}

void avisarSegunLimites(float g){
  int estado = 0;
  if (g < limiteBajoG) estado = -1;
  else if (g > limiteAltoG) estado = 1;
  else estado = 0;

  if (estado != estadoLimite){
    if (estado == -1) Serial.println("AVISO: Salinidad BAJA");
    else if (estado == 1) Serial.println("AVISO: Salinidad ALTA");
    else Serial.println("Salinidad en rango");
    estadoLimite = estado;
  }
}

// =============================== SETUP ===============================
void setup(){
  pinMode(PIN_ALIMENTACION, OUTPUT);
  Serial.begin(9600);
  delay(50);

  if (cargarConf()){
    Serial.print("Ajustes cargados: offset="); Serial.print(ajusteOffsetG, 4);
    Serial.print(" | escala="); Serial.println(ajusteEscalaK, 4);
  } else {
    Serial.println("Sin ajustes previos: offset=0, escala=1.");
  }

  if (cargarUmbrales()){
    Serial.print("Limites: bajo="); Serial.print(limiteBajoG, 3);
    Serial.print(" g | alto="); Serial.print(limiteAltoG, 3); Serial.println(" g");
  } else {
    Serial.print("Limites por defecto: bajo="); Serial.print(limiteBajoG, 3);
    Serial.print(" g | alto="); Serial.print(limiteAltoG, 3); Serial.println(" g");
    guardarUmbrales();
  }

  Serial.println("En 5 s puedes pulsar: 'A' (ajustar sensor) o 'U' (cambiar limites).");
  unsigned long t0 = millis();
  while (millis() - t0 < 5000){
    if (Serial.available()){
      char c = Serial.read();
      if (c == 'A' || c == 'a'){ ajustarSensor(); break; }
      if (c == 'U' || c == 'u'){ configurarLimites(); break; }
    }
  }
  Serial.println("Iniciando lecturas...");
}

// ================================ LOOP ================================
void loop(){
  // Accesos rápidos
  if (Serial.available()){
    char c = Serial.read();
    if (c == 'A' || c == 'a') ajustarSensor();
    if (c == 'U' || c == 'u') configurarLimites();
  }

  int   adc    = leerADC();
  float gramos = gramosDesdeADC(adc);

  Serial.print("ADC = ");
  Serial.print(adc);
  Serial.print(" | Salinidad = ");
  Serial.print(gramos, 3);
  Serial.println(" g");

  avisarSegunLimites(gramos);

  Serial.println("-------------------------------");
  delay(3000);
}
