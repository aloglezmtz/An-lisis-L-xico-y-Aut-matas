// Hands-on 1: Análisis léxico y autómatas (AFD + objeto instrucción + Moore)
// CLI:  g++ -std=c++17 -O2 hands_on1.cpp -o hands_on1 && ./hands_on1 "MOV AL, 6"
// Web:  emcc hands_on1.cpp -std=c++17 -O2 -o hands_on1.js -sEXPORTED_RUNTIME_METHODS=cwrap
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <optional>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
using namespace std;

// ---------- 1. AFD: tabla explícita de transiciones (' ' = espacio, '#' = dígito) ----------
struct Regla { string de; char c; string a; };
static const vector<Regla> R = {
  {"Q0",'M',"M1"},{"M1",'O',"M2"},{"M2",'V',"Q_MOV"},
  {"Q0",'A',"A1"},{"A1",'D',"A2"},{"A2",'D',"Q_ADD"},
  {"Q0",'S',"S1"},{"S1",'T',"S2"},{"S2",'O',"Q_STO"},
  {"Q0",'E',"E1"},{"E1",'N',"E2"},{"E2",'D',"Q_END"},
  // MOV R, d
  {"Q_MOV",' ',"MOV_SP"},{"MOV_SP",' ',"MOV_SP"},{"MOV_SP",'A',"MOV_R"},{"MOV_SP",'B',"MOV_R"},
  {"MOV_R",'L',"MOV_REG"},{"MOV_REG",' ',"MOV_REG"},{"MOV_REG",',',"MOV_COMA"},
  {"MOV_COMA",' ',"MOV_COMA"},{"MOV_COMA",'#',"MOV_NUM"},{"MOV_NUM",'#',"MOV_NUM"},
  // ADD AL, BL
  {"Q_ADD",' ',"ADD_SP"},{"ADD_SP",' ',"ADD_SP"},{"ADD_SP",'A',"ADD_A"},{"ADD_A",'L',"ADD_AL"},
  {"ADD_AL",' ',"ADD_AL"},{"ADD_AL",',',"ADD_COMA"},{"ADD_COMA",' ',"ADD_COMA"},
  {"ADD_COMA",'B',"ADD_B"},{"ADD_B",'L',"ADD_BL"},
  // STO d
  {"Q_STO",' ',"STO_SP"},{"STO_SP",' ',"STO_SP"},{"STO_SP",'#',"STO_NUM"},{"STO_NUM",'#',"STO_NUM"}};
static const set<string> ACEPTA = {"Q_END","MOV_NUM","ADD_BL","STO_NUM"};

static string sig(const string& e, char c) {
  for (auto& r : R)
    if (r.de == e && (r.c == c || (r.c == '#' && isdigit((unsigned char)c)))) return r.a;
  return "";  // sin transición -> estado de error
}
static string tipo(const string& s) {  // estado que cierra un lexema -> token
  static const map<string,string> m = {{"Q_MOV","MOV"},{"Q_ADD","ADD"},{"Q_STO","STO"},{"Q_END","END"},
    {"MOV_REG","REGISTRO"},{"ADD_AL","REGISTRO"},{"ADD_BL","REGISTRO"},{"MOV_COMA","COMA"},
    {"ADD_COMA","COMA"},{"MOV_NUM","NUMERO"},{"STO_NUM","NUMERO"}};
  auto it = m.find(s); return it == m.end() ? "" : it->second;
}
static string msg(const string& e, bool eof) {
  if (eof) {
    if (e == "Q0") return "entrada vacía";
    if (e == "MOV_COMA") return "falta la dirección después de la coma";
    if (e == "ADD_COMA" || e == "ADD_B") return "falta el registro BL";
    if (e == "MOV_REG") return "falta la coma y la dirección";
    if (e == "ADD_AL") return "falta la coma y el registro BL";
    if (e == "Q_MOV" || e == "MOV_SP") return "MOV requiere registro y dirección";
    if (e == "Q_ADD" || e == "ADD_SP") return "ADD requiere AL, BL";
    if (e == "Q_STO" || e == "STO_SP") return "falta la dirección de memoria en STO";
    return "instrucción incompleta";
  }
  if (e == "Q_END") return "END no admite operandos";
  if (ACEPTA.count(e)) return "sobran componentes al final de la instrucción";
  if (e == "MOV_REG" || e == "ADD_AL") return "falta la coma entre los operandos";
  if (e == "MOV_SP" || e == "MOV_R") return "registro inválido: MOV solo admite AL o BL";
  if (e == "ADD_SP" || e == "ADD_A" || e == "ADD_COMA" || e == "ADD_B") return "registro inválido: ADD requiere AL, BL en ese orden";
  if (e == "MOV_COMA" || e == "STO_SP") return "se esperaba una dirección decimal no negativa";
  if (e == "Q_MOV" || e == "Q_ADD" || e == "Q_STO") return "falta espacio después del mnemónico";
  return "carácter inesperado o mnemónico inválido (use MOV, ADD, STO, END en mayúsculas)";
}

// ---------- 2. Objeto instrucción ----------
struct Instr { string op; vector<string> regs; optional<string> dir; };

// ---------- 3. Máquina de Moore ----------
struct Estado { string nombre, salida; };
class Moore {
  vector<Estado> ruta; size_t i = 0;
public:
  explicit Moore(const Instr& x) {  // recibe el objeto; no vuelve a analizar el texto
    string d = x.dir.value_or("");
    if (x.op == "MOV") ruta = {{"Preparar dirección","MAR ← "+d},{"Leer memoria","MBR ← M[MAR]"},
                               {"Cargar "+x.regs[0], x.regs[0]+" ← MBR"}};
    else if (x.op == "ADD") ruta = {{"Sumar","ACC ← AL + BL"}};
    else if (x.op == "STO") ruta = {{"Preparar dirección","MAR ← "+d},{"Copiar ACC","MBR ← ACC"},
                                    {"Escribir memoria","M[MAR] ← MBR"}};
    else ruta = {{"Detener","HALT ← 1"}};
    ruta.push_back({"Fin",""});
  }
  bool fin() const { return i >= ruta.size(); }
  Estado siguientePaso(string& sg) {  // emite la salida del estado actual y avanza
    Estado e = ruta[i++]; sg = i < ruta.size() ? ruta[i].nombre : "—"; return e;
  }
};

// ---------- Procesamiento ----------
struct Tok { string tipo, lex; int paso; };
struct Paso { int i; char c; string de, a; };
struct Res { string entrada, err; bool ok = false; vector<Paso> pasos; vector<Tok> toks; Instr ins;
             vector<array<string,3>> moore; };

static Res procesar(string s) {
  Res r;
  auto a = s.find_first_not_of(" \t\r\n");
  s = a == string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
  r.entrada = s;
  string cur = "Q0", lex; bool fail = false;
  auto flush = [&](const string& st, int paso) {
    if (!lex.empty()) { r.toks.push_back({tipo(st), lex, paso}); lex.clear(); }
  };
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i]; string to = sig(cur, c);
    if (to.empty()) { r.pasos.push_back({(int)i, c, cur, "ERROR"}); r.err = msg(cur, false); fail = true; break; }
    if (c == ' ' || c == ',') flush(cur, i + 1);
    if (c != ' ') lex += c;
    if (c == ',') flush(to, i + 1);
    r.pasos.push_back({(int)i, c, cur, to}); cur = to;
  }
  if (!fail) {
    if (ACEPTA.count(cur)) { flush(cur, s.size() + 1); r.pasos.push_back({(int)s.size(), 0, cur, "ACEPTA"}); r.ok = true; }
    else { r.err = msg(cur, true); r.pasos.push_back({(int)s.size(), 0, cur, "ERROR"}); }
  }
  if (r.ok) {
    r.ins.op = r.toks[0].lex;
    for (auto& t : r.toks) { if (t.tipo == "REGISTRO") r.ins.regs.push_back(t.lex); else if (t.tipo == "NUMERO") r.ins.dir = t.lex; }
    Moore m(r.ins);
    while (!m.fin()) { string sg; Estado e = m.siguientePaso(sg); r.moore.push_back({e.nombre, e.salida, sg}); }
  }
  return r;
}

// ---------- Salidas ----------
static string esc(const string& s) {
  string o; for (char c : s) { if ((unsigned char)c < 32) { o += ' '; continue; } if (c == '"' || c == '\\') o += '\\'; o += c; } return o;
}
static string aJson(const Res& r) {
  ostringstream o;
  o << "{\"entrada\":\"" << esc(r.entrada) << "\",\"ok\":" << (r.ok ? "true" : "false") << ",\"error\":\"" << esc(r.err) << "\",\"pasos\":[";
  for (size_t i = 0; i < r.pasos.size(); ++i) { auto& p = r.pasos[i];
    o << (i ? "," : "") << "{\"c\":\"" << (p.c ? esc(string(1, p.c)) : string()) << "\",\"de\":\"" << p.de << "\",\"a\":\"" << p.a << "\"}"; }
  o << "],\"tokens\":[";
  for (size_t i = 0; i < r.toks.size(); ++i)
    o << (i ? "," : "") << "{\"tipo\":\"" << r.toks[i].tipo << "\",\"lex\":\"" << esc(r.toks[i].lex) << "\",\"paso\":" << r.toks[i].paso << "}";
  o << "],\"objeto\":";
  if (!r.ok) o << "null";
  else { o << "{\"operacion\":\"" << r.ins.op << "\",\"registros\":[";
    for (size_t i = 0; i < r.ins.regs.size(); ++i) o << (i ? "," : "") << "\"" << r.ins.regs[i] << "\"";
    o << "],\"direccion\":" << (r.ins.dir ? *r.ins.dir : "null") << "}"; }
  o << ",\"moore\":[";
  for (size_t i = 0; i < r.moore.size(); ++i)
    o << (i ? "," : "") << "{\"estado\":\"" << r.moore[i][0] << "\",\"salida\":\"" << r.moore[i][1] << "\",\"sig\":\"" << r.moore[i][2] << "\"}";
  o << "],\"afd\":{\"aristas\":[";
  for (size_t i = 0; i < R.size(); ++i) o << (i ? "," : "") << "[\"" << R[i].de << "\",\"" << R[i].c << "\",\"" << R[i].a << "\"]";
  o << "],\"acepta\":[";
  bool f = true; for (auto& s : ACEPTA) { o << (f ? "" : ",") << "\"" << s << "\""; f = false; }
  o << "]}}";
  return o.str();
}
static void imprimir(const Res& r) {
  cout << "ENTRADA\n" << r.entrada << "\n\nVALIDACIÓN MEDIANTE AFD\n";
  if (!r.ok) { cout << "Instrucción inválida: " << r.err << ".\nNo se construye el objeto instrucción.\nNo se generan microoperaciones.\n"; return; }
  cout << "Instrucción válida.\n\nTOKENS RECONOCIDOS\n";
  for (auto& t : r.toks) cout << t.tipo << "(\"" << t.lex << "\")\n";
  cout << "\nCOMPONENTES IDENTIFICADOS\nMnemónico: " << r.ins.op << "\n";
  for (auto& g : r.ins.regs) cout << "Registro: " << g << "\n";
  if (r.ins.dir) cout << "Dirección de memoria: " << *r.ins.dir << "\nDireccionamiento: directo\n";
  cout << "\nOBJETO INSTRUCCIÓN\n{\n  operacion: \"" << r.ins.op << "\",\n  registros: [";
  for (size_t i = 0; i < r.ins.regs.size(); ++i) cout << (i ? ", " : "") << "\"" << r.ins.regs[i] << "\"";
  cout << "],\n  direccion: " << (r.ins.dir ? *r.ins.dir : "null") << "\n}\n\nMICROOPERACIONES GENERADAS POR MOORE\n";
  int n = 0; for (auto& m : r.moore) if (!m[1].empty()) cout << ++n << ". " << m[1] << "\n";
  cout << "Generación terminada.\n";
}

#ifdef __EMSCRIPTEN__
// Interfaz WebAssembly SOLO con enteros: funciona con cualquier bandera de emcc
// (no depende de cwrap, ccall, malloc ni HEAPU8). JS envía la entrada byte a byte
// y lee el JSON de salida byte a byte.
static string g_in, g_out;
extern "C" {
  EMSCRIPTEN_KEEPALIVE void h1_limpiar() { g_in.clear(); }
  EMSCRIPTEN_KEEPALIVE void h1_byte(int b) { g_in.push_back((char)b); }
  EMSCRIPTEN_KEEPALIVE int h1_procesar() { g_out = aJson(procesar(g_in)); return (int)g_out.size(); }
  EMSCRIPTEN_KEEPALIVE int h1_leer(int i) { return (unsigned char)g_out[i]; }
  // Versión por cadena (requiere -sEXPORTED_RUNTIME_METHODS=cwrap); se conserva por compatibilidad
  EMSCRIPTEN_KEEPALIVE const char* analizar(const char* s) {
    static string out; out = aJson(procesar(s)); return out.c_str();
  }
}
#else
int main(int argc, char** argv) {
  bool js = false; string in;
  for (int i = 1; i < argc; ++i) { string a = argv[i]; if (a == "--json") js = true; else in += (in.empty() ? "" : " ") + a; }
  if (in.empty()) getline(cin, in);
  Res r = procesar(in);
  if (js) cout << aJson(r) << "\n"; else imprimir(r);
}
#endif