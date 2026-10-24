#include <iostream>
#include <chrono>
#include <list>
#include <random>
#include <numeric>
#include <algorithm>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <string>
#include <limits>
#include <cfloat>

#include "CubicajeApi.h"

using namespace std;

// Variables globales

default_random_engine generator{ 123 };
uniform_real_distribution<double> dist01(0.0, 1.0);
double const error = 0.0001;
const double epsilon = 1e-9;
double errorArea = 100;
double tolerancia = 0; // tolerancia para que una caja esté dentro de polígono
auto tIni = chrono::high_resolution_clock::now();
double duracion = 0;

double const utilizacionMaxima = 0.99; // Utilización máxima de los bloques

int beamWidth = 5;

auto semilla = chrono::system_clock::now().time_since_epoch().count();

//int holi = 0;
//double holiholi = 0;
int ContenedorDimx = 0;
int ContenedorDimy = 0;
int ContenedorDimz = 0;
double ContenedorVol = 0;

int r_estabilidad = 1; // 1: estabilidad parcial, 2: full support
int r_juntarEspacios = 0; // 0: Sin juntar ni expandir, 1: juntando sin expandir, 2: juntando y expandiendo
bool r_maxPresionItems = false; // 0: No hay restricción, 1: si hay restricción
int r_multidrop = 0; // 0: No hay restricción, 1: Visibilidad, 2: Capas con 0 (Junqueira con d_i = 0)

// ===== Integración in-memory para Unity/HoloLens =====
// Wrapper de E/S: cuando g_inMemoryMode = true, ReadData2 puebla BoxTypes0 desde
// g_inPiezas/g_inCont* en vez de leer un archivo, y WriteData vuelca
// incumbente.empacados a g_outPiezas en vez de escribir un archivo.
// Ciclo_BS y la lógica de Container/Bloque/soporte no se tocan.

struct PiezaEntradaMemoria {
	string idTexto;        // ID real de la pieza (ej. "A1"), preservado para el resultado
	int dimx, dimy, dimz;  // dimensiones en unidades enteras (ej. milímetros)
};

struct PiezaColocadaMemoria {
	string idTexto;
	int indiceEntrada;          // posición de la pieza en el vector de entrada (= idReal)
	int x1, y1, z1, x2, y2, z2; // esquinas de la caja colocada, unidades enteras
};

bool g_inMemoryMode = false;
int g_inContDimx = 0, g_inContDimy = 0, g_inContDimz = 0;
vector<PiezaEntradaMemoria> g_inPiezas;
vector<PiezaColocadaMemoria> g_outPiezas;
vector<string> g_idRealATexto; // idReal (índice de lectura) -> ID de texto original
double g_outUtilizacion = 0;   // % de volumen del pallet ocupado por la solución incumbente
int g_outTotalIter = 0;        // iteraciones GRASP completadas dentro de maxTime
int g_outTotalIterLS = 0;      // de esas, cuántas dispararon búsqueda local (mejoraron la incumbente)

int maxNBloques = 10; // Número máximo de bloques que se generan por el número de piezas para cada cliente

class Alpha {
public:

	// Parámetros

	double val, funObj, frecuencia;

	// Constructor

	Alpha(double const& p_val) {
		val = p_val;
		funObj = 0;
		frecuencia = 0;
	}

	// Métodos

	void Actualizar(double const& fo) {
		++frecuencia;
		funObj += (fo - funObj) / frecuencia;
	}

	// Operadores

	bool const operator<(Alpha& otro) {
		return funObj > otro.funObj;
	}
};
vector<Alpha> alphas({ Alpha(0.1), Alpha(0.2), Alpha(0.3), Alpha(0.4), Alpha(0.5), Alpha(0.6), Alpha(0.7), Alpha(0.8), Alpha(0.9), Alpha(1.0) });
vector<double> pesos_acum(alphas.size(), 0);

double totalVolumen = 0;

// Clases

class Point {
public:

	// Parámetros

	int x, y;

	// Constructor

	Point() {
		x = 0;
		y = 0;
	}
	Point(int const& px, int const& py) {
		x = px;
		y = py;
	}
	Point(Point const& p) {
		x = p.x;
		y = p.y;
	}

	// Métodos

	const double distanciaP(Point const& p) {
		return abs(x - p.x) + abs(y - p.y);
	}

	// Operador

	const bool operator==(Point const& otro) {
		return x == otro.x && y == otro.y;
	}
	const bool operator<(Point const& otro) {
		if (x == otro.x) return y < otro.y;
		return x < otro.x;
	}
};
class Linea {
public:

	// Parámetros

	double A, B, C;

	// Constructor

	Linea() {
		A = 0; B = 0; C = 0;
	}
	Linea(Linea const& l) {
		A = l.A;
		B = l.B;
		C = l.C;
	}
	Linea(Point const& p1, Point const& p2) {
		A = p1.y - p2.y;
		B = p2.x - p1.x;
		double n = sqrt(A * A + B * B);
		if (n > error) {
			A /= n;
			B /= n;
			C = -p1.x * A - p1.y * B;
		}
		else {
			A = 0.0;
			B = 0.0;
			C = 0.0;
		}
	}
	Linea(double const& x1, double const& y1, double const& x2, double const& y2) {
		A = y1 - y2;
		B = x2 - x1;
		double n = sqrt(A * A + B * B);
		if (n > error) {
			A /= n;
			B /= n;
			C = -x1 * A - y1 * B;
		}
		else {
			A = 0.0;
			B = 0.0;
			C = 0.0;
		}
	}

	// Métodos

	const double distancia(double const& cx, double const& cy) {
		return A * cx + B * cy + C;
	}
	const double distancia(Point const& p) {
		return A * p.x + B * p.y + C;
	}
	void neg() {
		A = -A; B = -B; C = -C;
	}
};
class Box {
public:

	// Parámetros

	int indBoxType;
	int x, y, z, area; // El área es el área de la base
	double presion, soporteNecesario;

	// Constructor

	Box() {
		indBoxType = -100;
		x = 0;
		y = 0;
		z = 0;
		area = 0;
		presion = 0.0;
		soporteNecesario = 0.0;
	}
	Box(Box const& b) {
		indBoxType = b.indBoxType;
		x = b.x;
		y = b.y;
		z = b.z;
		area = b.area;
		presion = b.presion;
		soporteNecesario = b.soporteNecesario;
	}
	Box(int const& p_indBT, int const& p_x, int const& p_y, int const& p_z) {
		indBoxType = p_indBT;
		x = p_x;
		y = p_y;
		area = p_x * p_y;
		z = p_z;
		presion = 0.0;
		soporteNecesario = 0.0;
	}
	Box(int const& p_indBT, int const& p_x, int const& p_y, int const& p_z, double const& p_presion, double const& masa) {
		indBoxType = p_indBT;
		x = p_x;
		y = p_y;
		area = p_x * p_y;
		z = p_z;
		presion = p_presion;
		soporteNecesario = masa / (double)(p_x * p_y);
	}

	// Operadores

	bool const operator==(Box const& otra) {
		return (x == otra.x && y == otra.y && z == otra.z);
	}
	bool const operator<(Box const& otra) {
		if (x == otra.x) {
			if (y == otra.y) return z < otra.z;
			return y < otra.y;
		}
		return x < otra.x;
	}
};
class BoxType {
public:

	// Parámetros

	int cliente;
	int minDx, minDy, minDz;
	int id, cantidad, volumenReal, idReal;
	double volumenContenedor;
	double masa;
	vector<Box> boxes;

private:

	void FullRotation(int const& p_dimx, int const& p_dimy, int const& p_dimz, int const& p_rotx, int const& p_roty, int const& p_rotz) {
		// Rx

		if (p_rotx == 1 || p_rotx == 3) boxes.push_back(Box(id, p_dimz, p_dimy, p_dimx));
		if (p_rotx == 2 || p_rotx == 3) boxes.push_back(Box(id, p_dimy, p_dimz, p_dimx));

		// Ry

		if (p_roty == 1 || p_roty == 3) boxes.push_back(Box(id, p_dimx, p_dimz, p_dimy));
		if (p_roty == 2 || p_roty == 3) boxes.push_back(Box(id, p_dimz, p_dimx, p_dimy));

		// Rz

		if (p_rotz == 1 || p_rotz == 3) boxes.push_back(Box(id, p_dimx, p_dimy, p_dimz));
		if (p_rotz == 2 || p_rotz == 3) boxes.push_back(Box(id, p_dimy, p_dimx, p_dimz));

		// Eliminación de cajas iguales

		sort(boxes.begin(), boxes.end());
		boxes.resize(distance(boxes.begin(), unique(boxes.begin(), boxes.end())));
	}
	void FullRotationPresion(int const& p_dimx, int const& p_dimy, int const& p_dimz, int const& p_rotx, int const& p_roty, int const& p_rotz, double& p_presionx, double& p_presiony, double& p_presionz) {
		// Se ajustan valores de presión

		if (p_dimx == p_dimy)
		{
			if (p_presionx > p_presiony) p_presiony = p_presionx;
			else p_presionx = p_presiony;
		}
		if (p_dimx == p_dimz)
		{
			if (p_presionx > p_presionz) p_presionz = p_presionx;
			else p_presionx = p_presionz;
		}
		if (p_dimy == p_dimz)
		{
			if (p_presiony > p_presionz) p_presionz = p_presiony;
			else p_presiony = p_presionz;
		}

		// Rx

		if (p_rotx == 1 || p_rotx == 3) boxes.push_back(Box(id, p_dimz, p_dimy, p_dimx, p_presionx, masa));
		if (p_rotx == 2 || p_rotx == 3) boxes.push_back(Box(id, p_dimy, p_dimz, p_dimx, p_presionx, masa));


		// Ry

		if (p_roty == 1 || p_roty == 3) boxes.push_back(Box(id, p_dimx, p_dimz, p_dimy, p_presiony, masa));
		if (p_roty == 2 || p_roty == 3) boxes.push_back(Box(id, p_dimz, p_dimx, p_dimy, p_presiony, masa));

		// Rz

		if (p_rotz == 1 || p_rotz == 3) boxes.push_back(Box(id, p_dimx, p_dimy, p_dimz, p_presionz, masa));
		if (p_rotz == 2 || p_rotz == 3) boxes.push_back(Box(id, p_dimy, p_dimx, p_dimz, p_presionz, masa));

		// Eliminación de cajas iguales

		sort(boxes.begin(), boxes.end());
		boxes.resize(distance(boxes.begin(), unique(boxes.begin(), boxes.end())));
	}

public:

	// Constructores

	BoxType() {
		boxes = vector<Box>(0);
	}
	BoxType(BoxType const& bt) {
		cliente = bt.cliente;
		minDx = bt.minDx;
		minDy = bt.minDy;
		minDz = bt.minDz;
		id = bt.id;
		idReal = bt.idReal;
		cantidad = bt.cantidad;
		volumenReal = bt.volumenReal;
		volumenContenedor = bt.volumenContenedor;
		masa = bt.masa;
		boxes = bt.boxes;
	}
	BoxType(int const& p_id, int const& p_cantidad, int const& p_dimx, int const& p_dimy, int const& p_dimz, int const& p_rotx, int const& p_roty, int const& p_rotz, double const& p_masa = 0.0f, int const& p_cliente = 0) {
		id = p_id;
		idReal = p_id;
		cantidad = p_cantidad;
		cliente = p_cliente;
		masa = p_masa;
		volumenReal = p_dimx * p_dimy * p_dimz;
		volumenContenedor = (double)volumenReal / ContenedorVol;
		boxes.reserve(6);
		FullRotation(p_dimx, p_dimy, p_dimz, p_rotx, p_roty, p_rotz);
		DetermineDmins();
	}
	BoxType(int const& p_id, int const& p_cantidad, int const& p_dimx, int const& p_dimy, int const& p_dimz, int const& p_rotx, int const& p_roty, int const& p_rotz, double const& p_masa, double& p_presionx, double& p_presiony, double& p_presionz, int const& p_cliente = 0) {
		id = p_id;
		idReal = p_id;
		cantidad = p_cantidad;
		cliente = p_cliente;
		masa = p_masa;
		volumenReal = p_dimx * p_dimy * p_dimz;
		volumenContenedor = (double)volumenReal / ContenedorVol;
		boxes.reserve(6);
		FullRotationPresion(p_dimx, p_dimy, p_dimz, p_rotx, p_roty, p_rotz, p_presionx, p_presiony, p_presionz);
		DetermineDmins();
	}

	// Métodos

	void ActualizarIndice(int const& p_id) {
		id = p_id;
		for (vector<Box>::iterator b_it = boxes.begin(); b_it < boxes.end(); ++b_it) (*b_it).indBoxType = p_id;
	}
	void DetermineDmins() {
		minDx = boxes.front().x;
		minDy = boxes.front().y;
		minDz = boxes.front().z;
		for (vector<Box>::iterator b_it = boxes.begin() + 1; b_it < boxes.end(); ++b_it) {
			if ((*b_it).x < minDx) minDx = (*b_it).x;
			if ((*b_it).y < minDy) minDy = (*b_it).y;
			if ((*b_it).z < minDz) minDz = (*b_it).z;
		}
	}

	// Operadores

	bool const operator<(BoxType const& otro) {
		return id < otro.id;
	}
	bool const operator==(BoxType const& otro) {
		return id == otro.id;
	}
};

vector<BoxType> BoxTypes0;
vector<int> cantidadesBoxTypes0;

class SoporteAbajo {
public:

	// Parámetros

	int indPB; // índice de la caja empacada abajo
	double area; // area de contacto
	double centrox, centroy; // centro del area de contacto
	double masaSoporte; // Esta cambia al agregar cajas encima
	double masaMax; // masa que máximo soporta según el area y la presión
	bool sirve; // Indica si el peso es soportado o no

	// Constructor

	SoporteAbajo() {
	}
	SoporteAbajo(SoporteAbajo const& s) {
		indPB = s.indPB;
		area = s.area;
		centrox = s.centrox;
		centroy = s.centroy;
		masaSoporte = s.masaSoporte;
		masaMax = s.masaMax;
		sirve = s.sirve;
	}
	SoporteAbajo(int const& p_indPB, int const& p_area, double const& p_cx, double const& p_cy, double const& p_presion) {
		indPB = p_indPB;
		area = (double)p_area;
		centrox = p_cx;
		centroy = p_cy;
		masaMax = p_presion * area;
		masaSoporte = 0;
		sirve = true;
	}
};
class PolAux {

	// Parámetros

private:

	Linea linea;

public:

	vector<Point> grupo;
	Point v1, v2;

	// Constructor

	PolAux(vector<Point> const& p_grupo, Linea const& p_li, Point const& p_v1, Point const& p_v2) {
		grupo = p_grupo;
		linea = p_li;
		v1 = p_v1;
		v2 = p_v2;
	}

	// Métodos

	int DeterminarAlejado() {
		double dAlejado = linea.distancia(grupo.front());
		int id = 0;
		int i = 1;
		for (vector<Point>::iterator g_it = grupo.begin() + 1; g_it < grupo.end(); ++g_it, ++i) {
			double d = linea.distancia(*g_it);
			if (d < dAlejado) {
				dAlejado = d;
				id = i;
			}
		}
		return id;
	}
};
class Poligono {
public:

	// Parámetros

	double xmin, xmax, ymin, ymax;
	vector<Linea> lineas;

	// Constructor

	Poligono() {
		xmin = 0;
		xmax = 0;
		ymin = 0;
		ymax = 0;
		lineas = vector<Linea>(0);
	}
	Poligono(Poligono const& p) {
		xmin = p.xmin;
		xmax = p.xmax;
		ymin = p.ymin;
		ymax = p.ymax;
		lineas = p.lineas;
	}
	Poligono(vector<Point>& points)
	{
		// Obtener únicos

		sort(points.begin(), points.end());
		points.resize(distance(points.begin(), unique(points.begin(), points.end())));

		// Encontrar 2 vértices extremos

		vector<Point> ch({ points.front(), points.back() });
		xmin = points.front().x;
		xmax = points.back().x;
		Linea l12(points.front(), points.back());

		// Se dividen los puntos

		vector<Point> grupo1; grupo1.reserve(points.size());
		vector<Point> grupo2; grupo2.reserve(points.size());
		for (vector<Point>::iterator p_it = points.begin() + 1; p_it < points.end() - 1; ++p_it) {
			double d = l12.distancia(*p_it);
			if (d < -error) grupo2.push_back(*p_it);
			else if (d > error) grupo1.push_back(*p_it);
		}

		// Se crea la lista de PolAux

		vector<PolAux> lista; lista.reserve(points.size());
		if (grupo2.size() > 0) lista.push_back(PolAux(grupo2, l12, ch.front(), ch.back()));
		if (grupo1.size() > 0) {
			l12.neg();
			lista.push_back(PolAux(grupo1, l12, ch.back(), ch.front()));
		}

		while (lista.size() > 0) {
			vector<PolAux> copia = lista;
			lista.clear();
			for (vector<PolAux>::iterator p = copia.begin(); p < copia.end(); ++p) {
				int ind = (*p).DeterminarAlejado();
				ch.push_back((*p).grupo[ind]);
				//(*p).grupo.erase((*p).grupo.begin() + ind);

				// Linea 1

				vector<Point> grupo; grupo.reserve((*p).grupo.size());
				Linea linea((*p).v1, ch.back());
				for (vector<Point>::iterator po = (*p).grupo.begin(); po < (*p).grupo.end(); ++po) {
					if (linea.distancia(*po) < -error) grupo.push_back(*po);
				}
				if (grupo.size() > 0) lista.push_back(PolAux(grupo, linea, (*p).v1, ch.back()));

				// Linea 2

				grupo.clear();
				linea = Linea(ch.back(), (*p).v2);
				for (vector<Point>::iterator po = (*p).grupo.begin(); po < (*p).grupo.end(); ++po) {
					if (linea.distancia(*po) < -error) grupo.push_back(*po);
				}
				if (grupo.size() > 0) lista.push_back(PolAux(grupo, linea, ch.back(), (*p).v2));
			}
		}

		// min y max

		ymin = ch.front().y;
		ymax = ch.front().y;
		for (vector<Point>::iterator p = ch.begin() + 1; p < ch.end(); ++p) {
			if (ymin > (*p).y) ymin = (*p).y;
			else if (ymax < (*p).y) ymax = (*p).y;
		}

		// Lineas

		lineas.reserve(ch.size());
		for (int i = 0; i < (int)ch.size() - 1; ++i) {
			Point point1 = ch[i];
			for (int j = i + 1; j < ch.size(); ++j) {
				Point point2 = ch[j];
				double maxD = point1.distanciaP(point2);
				Linea l(point1, point2);
				bool sirve = true;
				for (int k = (int)ch.size() - 1; k >= 0; --k) {
					if (k != i && k != j) {
						Point point3 = ch[k];
						double d = l.distancia(point3);
						if (d < -error) {
							sirve = false;
							break;
						}
						if (k > i) {
							if (abs(d) < error) { // colineal
								d = point1.distanciaP(point3);
								if (maxD < d) {
									maxD = d;
									ch[j] = ch[k];
									point2 = ch[j];
									l = Linea(point1, point2);
								}
								ch.erase(ch.begin() + k);
								if (k < j) --j;
							}
						}
					}
				}
				if (sirve) {
					lineas.push_back(l);
					Point temp = ch[j];
					ch[j] = ch[i + 1];
					ch[i + 1] = temp;
					break;
				}
			}
		}
		lineas.push_back(Linea(ch.back(), ch.front()));
	}

	// Métodos

	void ActualizarPoligono(int const& x, int const& y) { // xy son el aumento del bloque en la nueva posición
		xmin += x;
		xmax += x;
		ymin += y;
		ymax += y;
		for (vector<Linea>::iterator it = lineas.begin(); it != lineas.end(); ++it) {
			(*it).C -= ((*it).A * x + (*it).B * y);
		}
	}
	const bool puntoDentroPoligono(double const& cx, double const& cy) {
		if (xmin <= cx && cx <= xmax) {
			if (ymin <= cy && cy <= ymax) {
				for (vector<Linea>::iterator l = lineas.begin(); l < lineas.end(); ++l) {
					if ((*l).distancia(cx, cy) < tolerancia) return false;
				}
				return true;
			}
			return false;
		}
		return false;
	}
};
class SoporteArriba {
public:

	// Parámetros

	int indPB; // indice de la caja empacada arriba
	int idS; // id del soporte

	// constructor

	SoporteArriba() {

	}
	SoporteArriba(int const& p_indPB, int const& p_idS) {
		indPB = p_indPB;
		idS = p_idS;
	}
};
class PackedBox {
public:

	// Parámetros

	int id; // Id de la caja empacada
	int x1, y1, z1, x2, y2, z2;
	int idBloque; // id del bloque general de donde viene
	double cx, cy; // Centro geométrico
	BoxType* bt;
	Box* b;

	double masaTotal; // Sumatoria de las masas de arriba y de la caja actual
	double centroMasax, centroMasay;
	vector<SoporteAbajo> soportes; // Cajas debajo de esta
	Poligono poligono;

	list<SoporteArriba> soportesArriba; // Soportes de cajas empacadas de arriba
	bool soportada; // indica si la caja está soportada o no

	double maxSoporteFast;

	// Constructor

	PackedBox() {
		bt = nullptr;
		b = nullptr;
		soportes = vector<SoporteAbajo>(0);
		soportesArriba = list<SoporteArriba>(0);
		maxSoporteFast = 1;
	}
	PackedBox(Box* p_b) {
		b = p_b;
		x1 = 0;
		y1 = 0;
		z1 = 0;
		x2 = (*b).x;
		y2 = (*b).y;
		z2 = (*b).z;
		bt = &BoxTypes0[(*p_b).indBoxType];
		masaTotal = (*bt).masa;
		cx = (double)(x2) / 2.0;
		cy = (double)(y2) / 2.0;
		centroMasax = cx;
		centroMasay = cy;
		soportes = vector<SoporteAbajo>(0);
		soportesArriba = list<SoporteArriba>(0);
		soportada = true;
		id = 0;
		maxSoporteFast = (*b).presion;
	}

	// Metodos

	void DistribuirPesoEntreSoportes() {
		if (z1 == 0) {
			soportada = true;
			return;
		}
		vector<double> resp;
		ObtenerEcuacionesSoporte(resp);
		vector<double>::iterator r_it = resp.begin();
		for (vector<SoporteAbajo>::iterator s = soportes.begin(); s != soportes.end(); ++s, ++r_it) {
			if ((*s).masaMax < *r_it) {
				soportada = false;
				break;
			}
			else {
				(*s).masaSoporte = *r_it;
			}
		}
	}
	void ActualizarCentroMasaYPeso(vector<PackedBox>* empacados) {
		masaTotal = (*bt).masa;
		centroMasax = (*bt).masa * cx;
		centroMasay = (*bt).masa * cy;
		for (list<SoporteArriba>::iterator s = soportesArriba.begin(); s != soportesArriba.end(); ++s) {
			masaTotal += (*empacados)[(*s).indPB].soportes[(*s).idS].masaSoporte;
			centroMasax += (*empacados)[(*s).indPB].soportes[(*s).idS].masaSoporte * (*empacados)[(*s).indPB].soportes[(*s).idS].centrox;
			centroMasay += (*empacados)[(*s).indPB].soportes[(*s).idS].masaSoporte * (*empacados)[(*s).indPB].soportes[(*s).idS].centroy;
		}
		centroMasax /= masaTotal;
		centroMasay /= masaTotal;
		if (z1 == 0) {
			soportada = true;
		}
		else {
			soportada = poligono.puntoDentroPoligono(centroMasax, centroMasay);
		}
	}

private:

	void ObtenerEcuacionesSoporte(vector<double>& resp) {
		vector<vector<double>> A;
		vector<double> A1; A1.reserve(soportes.size() + 1);
		vector<double> A2; A2.reserve(soportes.size() + 1);
		for (vector<SoporteAbajo>::iterator s = soportes.begin(); s != soportes.end(); ++s) {
			A1.push_back(centroMasax - (*s).centrox);
			A2.push_back(centroMasay - (*s).centroy);
		}
		A1.push_back(0);
		for (vector<double>::iterator a = A1.begin(); a != A1.end(); ++a) {
			if (abs(*a) > error) {
				A.push_back(A1);
				break;
			}
		}
		A2.push_back(0);
		for (vector<double>::iterator a = A2.begin(); a != A2.end(); ++a) {
			if (abs(*a) > error) {
				A.push_back(A2);
				break;
			}
		}
		vector<double> A3; A3.reserve(soportes.size() + 1);
		for (vector<SoporteAbajo>::iterator s1 = soportes.begin(); s1 != soportes.end(); ++s1) {
			A1.clear();
			A2.clear();
			for (vector<SoporteAbajo>::iterator s2 = soportes.begin(); s2 != soportes.end(); ++s2) {
				A1.push_back((*s1).centrox - (*s2).centrox);
				A2.push_back((*s1).centroy - (*s2).centroy);
			}
			A1.push_back(((*s1).centrox - centroMasax) * masaTotal);
			for (vector<double>::iterator a = A1.begin(); a != A1.end(); ++a) {
				if (abs(*a) > error) {
					A.push_back(A1);
					break;
				}
			}
			A2.push_back(((*s1).centroy - centroMasay) * masaTotal);
			for (vector<double>::iterator a = A2.begin(); a != A2.end(); ++a) {
				if (abs(*a) > error) {
					A.push_back(A2);
					break;
				}
			}
			A3.push_back(1);
		}
		A3.push_back(masaTotal);
		A.push_back(A3); // Sumatoria de fuerzas
		resp.clear(); resp.reserve(soportes.size());
		GaussianElimination(A, resp);
		if (A.size() < (int)A.front().size() - 1) { // Se supone presion igual
			int nColumnas = (int)A.front().size();

			// Se añaden todas las ecuaciones de soporte según área

			int i = 0;
			for (vector<SoporteAbajo>::iterator s_it1 = soportes.begin(); s_it1 != prev(soportes.end()); ++s_it1, ++i) {
				int j = i + 1;
				for (vector<SoporteAbajo>::iterator s_it2 = next(s_it1); s_it2 != soportes.end(); ++s_it2, ++j) {
					A.push_back(vector<double>(nColumnas, 0));
					A.back()[i] = 1.0f / (*s_it1).area;
					A.back()[j] = -1.0f / (*s_it2).area;
				}
			}

			// Eliminación gaussiana 2

			GaussianElimination2(A, resp);
		}
	}
	void GaussianElimination(vector<vector<double>>& A, vector<double>& resp) {

		// La matriz A son los indices de cada variable y el resultado negativo

		int nF = (int)A.front().size() - 1;
		resp.resize(nF);
		int nR = (int)A.size();

		// Obtener matriz triangular

		for (int j = 0; j < nF; ++j) {

			// Encontrar el máximo valor absoluto de la columna hacia abajo

			if (j >= nR) {
				A.erase(A.begin() + j, A.end());

				// Poner 0s

				int i = 1;
				for (vector<vector<double>>::iterator A2 = A.begin() + 1; A2 < A.end(); ++A2, ++i) {
					vector<double>::iterator A1 = (*A2).begin();
					for (int j2 = 0; j2 < i; ++j2, ++A1) {
						(*A1) = 0;
					}
				}
				return;
			}
			int imax = j;
			double valmax = abs(A[imax][j]);
			for (int i = j + 1; i < nR; ++i) {
				if (abs(A[i][j]) > valmax) {
					imax = i;
					valmax = abs(A[i][j]);
				}
			}
			if (valmax < errorArea) {
				//A.erase(A.begin() + j, A.begin() + (int)A.size() - j);
				A.erase(A.begin() + j, A.end());

				// Poner 0s

				int i = 1;
				for (vector<vector<double>>::iterator A2 = A.begin() + 1; A2 < A.end(); ++A2, ++i) {
					vector<double>::iterator A1 = (*A2).begin();
					for (int j2 = 0; j2 < i; ++j2, ++A1) {
						(*A1) = 0;
					}
				}
				return;
			}
			valmax = A[imax][j];

			// Intercambiar fila actual con la de máximo valor

			if (imax != j) {
				vector<double> temp(A[imax]);
				A[imax] = A[j];
				A[j] = temp;
			}

			// Dividir las siguientes filas y columnas según un factor para tener una matriz triangular

			int i2 = j + 1;
			for (int i = j + 1; i < nR; ++i, ++i2) {
				double factor = A[i][j] / valmax;
				if (abs(factor) < errorArea) continue;
				int j2 = j + 1;
				for (int k = j + 1; k < nF + 1; ++k, ++j2) {
					A[i][k] -= A[j][j2] * factor;
				}
				A[i2][j] = 0.0;
			}

			// Poner el pivote en 1

			A[j][j] = 1;
			for (int k = j + 1; k < nF + 1; ++k) {
				A[j][k] /= valmax;
			}
		}
		A.resize(nF);

		// Obtener respuesta a partir de resolver las ecuaciones con la matriz triangular de abajo para arriba

		int ii = nF - 1;
		for (int k = (int)A.size() - 1; k >= 0; --k, --ii) {
			resp[ii] = A[k][nF];
			int j = ii + 1;
			for (int m = ii + 1; m < nF; ++m, ++j) {
				resp[ii] -= A[k][m] * resp[j];
			}
		}
	}
	void GaussianElimination2(vector<vector<double>>& A, vector<double>& resp) {

		// La matriz A son los indices de cada variable y el resultado negativo

		int nF = (int)A.front().size() - 1;// Número de variables
		int nR = (int)A.size();// Número de ecuaciones 

		// Obtener matriz triangular

		for (int j = 0; j < nF; ++j)
		{
			// Si el número actual es diferente de cero se deja ese, sino se determina el máximo valor absoluto. Esto para ir en orden de las ecuaciones

			int imax = j;
			double valmax = abs(A[imax][j]);
			if (valmax < errorArea) {
				for (int i = j + 1; i < nR; ++i) {
					if (abs(A[i][j]) > valmax) {
						imax = i;
						valmax = abs(A[i][j]);
					}
				}
			}
			if (valmax < errorArea) {
				// MACHETAZO
				for (vector<double>::iterator r_it = resp.begin(); r_it < resp.end(); ++r_it) {
					(*r_it) = numeric_limits<double>::max();
				}
				cout << "Matriz singular" << endl;
				return;
			}
			valmax = A[imax][j];

			// Intercambiar fila actual con la de máximo valor

			if (imax != j)
			{
				vector<double> temp(A[imax]);
				A[imax] = A[j];
				A[j] = temp;
			}

			// Dividir las siguientes filas y columnas según un factor para tener una matriz triangular

			int i2 = j + 1;
			for (int i = j + 1; i < nR; ++i)
			{
				double factor = A[i][j] / valmax;
				if (abs(factor) < errorArea) continue;
				int j2 = j + 1;
				for (int k = j + 1; k < nF + 1; ++k)
				{
					A[i][k] -= A[j][j2] * factor;
					++j2;
				}
				A[i2][j] = 0.0;
				++i2;
			}

			// Poner el pivote en 1

			A[j][j] = 1;
			for (int k = j + 1; k < nF + 1; ++k)
			{
				A[j][k] /= valmax;
			}
		}
		A.resize(nF);

		// Obtener respuesta a partir de resolver las ecuaciones con la matriz triangular de abajo para arriba

		int ii = nF - 1;
		for (int k = (int)A.size() - 1; k >= 0; --k, --ii) {
			resp[ii] = A[k][nF];
			int j = ii + 1;
			for (int m = ii + 1; m < nF; ++m, ++j) {
				resp[ii] -= A[k][m] * resp[j];
			}
		}
	}
};
class Bloque {
public:

	// Parámetros

	int dx, dy, dz, vol, volOcupado;
	double volContenedor;
	vector<PackedBox> cajas;
	vector<int> cantidades; // Cantidades de cada tipo de caja
	bool sirve; // Indica si se puede construir o no

	int bf1, bf2, bf3;

	// Constructor

	Bloque() {
		cajas = vector<PackedBox>(0);
	}
	Bloque(Box* p_b) {
		cajas = vector<PackedBox>(1, PackedBox(p_b));
		BoxType* bt = cajas.front().bt;
		dx = (*p_b).x;
		dy = (*p_b).y;
		dz = (*p_b).z;
		vol = (*bt).volumenReal;
		volContenedor = (*bt).volumenContenedor;
		volOcupado = (*bt).volumenReal; // cambio aqui
		//volOcupado = 0; // cambio aqui
		cantidades = vector<int>(BoxTypes0.size(), 0);
		cantidades[(*bt).id] = 1;
		sirve = true;
	}
	Bloque(Bloque& b1, Bloque& b2, int const& esc) {

		// Dimensiones: el bloque debe estar dentro del contenedor

		sirve = true;
		if (esc <= 2) { // x
			dx = b1.dx + b2.dx;
			if (dx > ContenedorDimx) {
				sirve = false;
			}
			else {
				dy = max(b1.dy, b2.dy);
				dz = max(b1.dz, b2.dz);
			}
		}
		else if (esc <= 4) { // y
			dy = b1.dy + b2.dy;
			if (dy > ContenedorDimy) {
				sirve = false;
			}
			else {
				dx = max(b1.dx, b2.dx);
				dz = max(b1.dz, b2.dz);
			}
		}
		else { // z
			dz = b1.dz + b2.dz;
			if (dz > ContenedorDimz) {
				sirve = false;
			}
			else {
				dx = max(b1.dx, b2.dx);
				dy = max(b1.dy, b2.dy);
			}
		}
		if (sirve) {

			// Utilización volumétrica del bloque

			vol = dx * dy * dz;
			volOcupado = b1.volOcupado + b2.volOcupado;
			double ut = (double)volOcupado / (double)vol;
			if (ut < utilizacionMaxima) {
				sirve = false;
			}
			else {

				// Cantidades

				volContenedor = b1.volContenedor + b2.volContenedor;
				cantidades = b1.cantidades;
				vector<int>::iterator c0 = cantidadesBoxTypes0.begin();
				vector<int>::iterator c2 = b2.cantidades.begin();
				for (vector<int>::iterator c = cantidades.begin(); c < cantidades.end(); ++c, ++c2, ++c0) {
					*c += *c2;
					if (*c > *c0) {
						sirve = false;
						break;
					}
				}

				// Escenarios

				if (sirve) {
					if (esc == 1) { // b1 en -x y b2 en +x
						cajas = b1.cajas;
						int desde = (int)cajas.size();
						cajas.insert(cajas.end(), b2.cajas.begin(), b2.cajas.end());
						AjustarPiezasEjex(desde, b1.dx);
					}
					else if (esc == 2) { // b2 en -x y b1 en +x
						cajas = b2.cajas;
						int desde = (int)cajas.size();
						cajas.insert(cajas.end(), b1.cajas.begin(), b1.cajas.end());
						AjustarPiezasEjex(desde, b2.dx);
					}
					else if (esc == 3) { // b1 en -y y b2 en +y
						cajas = b1.cajas;
						int desde = (int)cajas.size();
						cajas.insert(cajas.end(), b2.cajas.begin(), b2.cajas.end());
						AjustarPiezasEjey(desde, b1.dy);
					}
					else if (esc == 4) { // b2 en -y y b1 en +y
						cajas = b2.cajas;
						int desde = (int)cajas.size();
						cajas.insert(cajas.end(), b1.cajas.begin(), b1.cajas.end());
						AjustarPiezasEjey(desde, b2.dy);
					}
					else if (esc == 5) { // b1 en -z y b2 en +z
						cajas = b1.cajas;
						int desde = (int)cajas.size();
						cajas.insert(cajas.end(), b2.cajas.begin(), b2.cajas.end());
						AjustarPiezasEjez(desde, b1.dz);
					}
					else { // b2 en -z y b1 en +z
						cajas = b2.cajas;
						int desde = (int)cajas.size();
						cajas.insert(cajas.end(), b1.cajas.begin(), b1.cajas.end());
						AjustarPiezasEjez(desde, b2.dz);
					}
				}
			}
		}
	}

	// Métodos

	void DeterminarEspacioMaximal_BS(int const& dxe, int const& dye, int const& dze) {
		bf1 = dxe - dx;
		bf2 = dye - dy;
		bf3 = dze - dz;
		if (bf2 <= bf1 && bf2 <= bf3) {
			int t = bf2;
			bf2 = bf1;
			bf1 = t;
		}
		else if (bf3 <= bf1 && bf3 <= bf2) {
			int t = bf3;
			bf3 = bf1;
			bf1 = t;
		}
		if (bf3 < bf2) {
			int t = bf3;
			bf3 = bf2;
			bf2 = t;
		}
	}

	// Operator

	bool const operator==(Bloque& otro) {
		if (dx == otro.dx && dy == otro.dy && dz == otro.dz && vol == otro.vol && volOcupado == otro.volOcupado && cajas.size() == otro.cajas.size()) {
			vector<int>::iterator c1 = otro.cantidades.begin();
			for (vector<int>::iterator c2 = cantidades.begin(); c2 < cantidades.end(); ++c1, ++c2) {
				if (*c1 != *c2) {
					return false;
				}
			}
			if (r_maxPresionItems) {

				// Asegurar que al menos una caja tenga diferente presion o posición de las piezas?

				for (vector<PackedBox>::iterator p1 = cajas.begin(); p1 < cajas.end(); ++p1) {
					bool igual = false;
					for (vector<PackedBox>::iterator p2 = otro.cajas.begin(); p2 < otro.cajas.end(); ++p2) {
						if ((*(*p1).b).indBoxType == (*(*p2).b).indBoxType && (*(*p1).b).x == (*(*p2).b).x && (*(*p1).b).y == (*(*p2).b).y && (*(*p1).b).z == (*(*p2).b).z) {
							igual = true;
							break;
						}
					}
					if (!igual) {
						return false;
					}
				}
			}
			return true;
		}
		return false;
	}
	bool const operator<(Bloque const& otro) {
		if (dx == otro.dx) {
			if (dy == otro.dy) {
				return dz < otro.dz;
			}
			return dy < otro.dy;
		}
		return dx < otro.dx;
	}

private:

	// Métodos

	void AjustarPiezasEjex(int const& desde, int const& deltax) {
		for (vector<PackedBox>::iterator p = cajas.begin() + desde; p < cajas.end(); ++p) {

			// Posición

			(*p).id += desde;
			(*p).x1 += deltax;
			(*p).x2 += deltax;
			(*p).cx += deltax;
			(*p).centroMasax += deltax;

			// Soportes

			for (vector<SoporteAbajo>::iterator s = (*p).soportes.begin(); s != (*p).soportes.end(); ++s) {
				(*s).indPB += desde;
				(*s).centrox += (double)deltax;
			}
			for (list<SoporteArriba>::iterator s = (*p).soportesArriba.begin(); s != (*p).soportesArriba.end(); ++s) {
				(*s).indPB += desde;
			}

			// Polígono

			(*p).poligono.xmin += (double)deltax;
			(*p).poligono.xmax += (double)deltax;
			for (vector<Linea>::iterator l = (*p).poligono.lineas.begin(); l < (*p).poligono.lineas.end(); ++l) {
				(*l).C -= (*l).A * deltax;
			}
		}
	}
	void AjustarPiezasEjey(int const& desde, int const& deltay) {
		for (vector<PackedBox>::iterator p = cajas.begin() + desde; p < cajas.end(); ++p) {

			// Posición

			(*p).id += desde;
			(*p).y1 += deltay;
			(*p).y2 += deltay;
			(*p).cy += deltay;
			(*p).centroMasay += deltay;

			// Soportes

			for (vector<SoporteAbajo>::iterator s = (*p).soportes.begin(); s != (*p).soportes.end(); ++s) {
				(*s).indPB += desde;
				(*s).centroy += (double)deltay;
			}
			for (list<SoporteArriba>::iterator s = (*p).soportesArriba.begin(); s != (*p).soportesArriba.end(); ++s) {
				(*s).indPB += desde;
			}

			// Polígono

			(*p).poligono.ymin += (double)deltay;
			(*p).poligono.ymax += (double)deltay;
			for (vector<Linea>::iterator l = (*p).poligono.lineas.begin(); l < (*p).poligono.lineas.end(); ++l) {
				(*l).C -= (*l).B * deltay;
			}
		}
	}
	void AjustarPiezasEjez(int const& desde, int const& deltaz) {

		// Determinar cajas top

		vector<vector<PackedBox>::iterator> cajasArbol;
		vector<vector<PackedBox>::iterator> cajasTop;
		for (vector<PackedBox>::iterator p = cajas.begin(); p < cajas.begin() + desde; ++p) {
			cajasArbol.push_back(p);
			if ((*p).z2 == deltaz) {
				cajasTop.push_back(p);
			}
		}

		// Actualizar valores para las cajas de arriba 

		vector<vector<PackedBox>::iterator> cajasSuperioresBloqueSuperior;
		cajasSuperioresBloqueSuperior.reserve(cajas.size());
		int i = desde;
		for (vector<PackedBox>::iterator p = cajas.begin() + desde; p < cajas.end(); ++p, ++i) {

			// Posición

			(*p).id += desde;
			(*p).z1 += deltaz;
			(*p).z2 += deltaz;

			// Soportes

			for (vector<SoporteAbajo>::iterator s = (*p).soportes.begin(); s != (*p).soportes.end(); ++s) {
				(*s).indPB += desde;
			}
			for (list<SoporteArriba>::iterator s = (*p).soportesArriba.begin(); s != (*p).soportesArriba.end(); ++s) {
				(*s).indPB += desde;
			}

			// Determinar polígono de soportes si es una caja down con las cajas top

			if ((*p).z1 == deltaz) {

				// Determinar intersecciones

				vector<Point> intersecciones;
				for (vector<vector<PackedBox>::iterator>::iterator p2 = cajasTop.begin(); p2 < cajasTop.end(); ++p2) {
					if (!((*p).x1 >= (**p2).x2 || (**p2).x1 >= (*p).x2)) {
						if (!((*p).y1 >= (**p2).y2 || (**p2).y1 >= (*p).y2)) {
							vector<int> misx({ (*p).x1, (*p).x2, (**p2).x1, (**p2).x2 });
							sort(misx.begin(), misx.end());
							vector<int> misy({ (*p).y1, (*p).y2, (**p2).y1, (**p2).y2 });
							sort(misy.begin(), misy.end());
							intersecciones.push_back(Point(misx[1], misy[1]));
							intersecciones.push_back(Point(misx[2], misy[1]));
							intersecciones.push_back(Point(misx[1], misy[2]));
							intersecciones.push_back(Point(misx[2], misy[2]));

							// Creación de soportes

							double cx = (double)(misx[2] - misx[1]) / 2.0;
							double cy = (double)(misy[2] - misy[1]) / 2.0;
							(*p).soportes.push_back(SoporteAbajo((const int)distance(cajas.begin(), *p2), (misx[2] - misx[1]) * (misy[2] - misy[1]), cx, cy, (*(**p2).b).presion));
							(**p2).soportesArriba.push_back(SoporteArriba(i, (int)(*p).soportes.size() - 1));

							// Soporte Fast

							(*p).maxSoporteFast = min((*p).maxSoporteFast, (**p2).maxSoporteFast - (*(*p).b).soporteNecesario);
						}
					}
				}
				if (intersecciones.size() > 0) {
					(*p).poligono = Poligono(intersecciones);
					if (!(*p).poligono.puntoDentroPoligono((*p).centroMasax, (*p).centroMasay)) {
						sirve = false;
						break;
					}
					else {
						cajasArbol.push_back(p);
					}
				}
				else {
					sirve = false;
					break;
				}
			}
			else {
				cajasSuperioresBloqueSuperior.push_back(p); // Esto es para el soporte fast
			}
		}

		// Se actualiza el árbol

		if (sirve && r_maxPresionItems) {

			// Se ordenan las piezas de mayor a menor z1

			sort(cajasArbol.begin(), cajasArbol.end(), [](vector<PackedBox>::iterator p1, vector<PackedBox>::iterator p2)->bool {return (*p1).z1 > (*p2).z1; });

			// Distribuir peso en soportes y actualizar centros de masa de los soportes

			int z1Actual = (*cajasArbol.front()).z1;
			for (vector<vector<PackedBox>::iterator>::iterator it = cajasArbol.begin(); it < cajasArbol.end(); ++it) {
				if ((**it).z1 == z1Actual) {
					(**it).DistribuirPesoEntreSoportes();
					if (!(**it).soportada) {
						sirve = false;
						break;
					}
				}
				else {
					for (vector<vector<PackedBox>::iterator>::iterator it2 = cajasArbol.begin(); it2 < cajasArbol.end(); ++it2) {
						if ((**it2).z2 == z1Actual) {
							(**it2).ActualizarCentroMasaYPeso(&cajas);
							if (!(**it2).soportada) {
								sirve = false;
								break;
							}
						}
					}
					if (!sirve) {
						break;
					}
					z1Actual = (**it).z1;
					if (z1Actual == 0) break;
					--it;
				}
			}
			for (vector<vector<PackedBox>::iterator>::iterator it2 = cajasArbol.begin(); it2 < cajasArbol.end(); ++it2) {
				if ((**it2).z1 == 0) { // Las de más abajo
					(**it2).ActualizarCentroMasaYPeso(&cajas);
					if (!(**it2).soportada) {
						sirve = false;
						break;
					}
				}
			}

			// Actualizar árbol superior con soporte fast

			sort(cajasSuperioresBloqueSuperior.begin(), cajasSuperioresBloqueSuperior.end(), [](vector<PackedBox>::iterator p1, vector<PackedBox>::iterator p2)->bool {return (*p1).z1 < (*p2).z1; });
			for (vector<vector<PackedBox>::iterator>::iterator it = cajasSuperioresBloqueSuperior.begin(); it != cajasSuperioresBloqueSuperior.end(); ++it) {
				(**it).maxSoporteFast = (*(**it).b).presion;
				for (vector<SoporteAbajo>::iterator s = (**it).soportes.begin(); s != (**it).soportes.end(); ++s) {
					(**it).maxSoporteFast = min((**it).maxSoporteFast, cajas[(*s).indPB].maxSoporteFast - (*(**it).b).soporteNecesario);
				}
			}
		}
	}
};

vector<list<Bloque>> Bloques0; // El vector es porque es para cada cliente
vector<list<Bloque*>> Bloques00; // Apuntadores

class MaximalSpace {
public:

	// Parámetros

	int indEsquina; // 1: (-x, -y, -z), 2: (+x, -y, -z), 3: (-x +y, -z), 4: (+x, +y, -z) -> los demás con +z
	int metrica;
	int x1, y1, z1, x2, y2, z2, dx, dy, dz;
	int maxSopx;

	bool cambio; // Indica si hay cambio o no para determinar los indices de las cajas
	list<int> indCajas; // Indices de las cajas empacadas sobre la que se forma el EM

	// Constructor

	MaximalSpace() {
		x1 = 0;
		y1 = 0;
		z1 = 0;
		y2 = ContenedorDimy;
		x2 = ContenedorDimx;
		z2 = ContenedorDimz;
		dx = ContenedorDimx;
		dy = ContenedorDimy;
		dz = ContenedorDimz;
		indEsquina = 1;
		cambio = false;
		indCajas = list<int>(0);
		metrica = 0;
		maxSopx = ContenedorDimx;
	}
	MaximalSpace(int const& p_e1x, int const& p_e1y, int const& p_e1z, int const& p_e2x, int const& p_e2y, int const& p_e2z) {
		x1 = p_e1x;
		y1 = p_e1y;
		z1 = p_e1z;
		x2 = p_e2x;
		y2 = p_e2y;
		z2 = p_e2z;
		dx = p_e2x - p_e1x;
		dy = p_e2y - p_e1y;
		dz = p_e2z - p_e1z;
		indCajas = list<int>(0);
		cambio = true;
		indEsquina = -1;
		maxSopx = ContenedorDimx;
	}

	// Operadores

	bool const operator<(MaximalSpace const& otro) {
		if (x1 == otro.x1) {
			if (z1 == otro.z1) {
				return metrica < otro.metrica;
			}
			return z1 > otro.z1;
		}
		return x1 < otro.x1;
	}
};
class BloqueEmpacado {
public:

	// Parámetros

	const Bloque* b;
	int x1, y1, z1, x2, y2, z2;
	int bf1, bf2, bf3; // Métricas de best-fit
	list<vector<SoporteAbajo>> soportesAbajo; // Soportes de las cajas de abajo de este bloque
	list<list<SoporteArriba>> soportesArriba; // Los soportes de las cajas arriba donde recae el nuevo bloque
	list<Poligono> poligonos;
	vector<PackedBox> cajasEmpacadas;

	// Constructor

	BloqueEmpacado() {
		b = nullptr;
		soportesAbajo = list<vector<SoporteAbajo>>(0);
		soportesArriba = list<list<SoporteArriba>>(0);
		poligonos = list<Poligono>(0);
		cajasEmpacadas = vector<PackedBox>(0);
	}
	BloqueEmpacado(const Bloque* p_b, const MaximalSpace* e) {
		b = p_b;
		if ((*e).indEsquina == 1) { // -x, -y
			x1 = (*e).x1;
			y1 = (*e).y1;
		}
		else if ((*e).indEsquina == 2) { // -x, +y
			x1 = (*e).x1;
			y1 = (*e).y2 - (*p_b).dy;
		}
		else if ((*e).indEsquina == 3) { // +x, -y
			x1 = (*e).x2 - (*p_b).dx;
			y1 = (*e).y1;
		}
		else if ((*e).indEsquina == 4) { // +x, +y
			x1 = (*e).x2 - (*p_b).dx;
			y1 = (*e).y2 - (*p_b).dy;
		}
		x2 = x1 + (*b).dx;
		y2 = y1 + (*b).dy;
		z1 = (*e).z1;
		z2 = z1 + (*b).dz;

		// Métricas de Best-Fit

		bf1 = (*e).dz - (*b).dz;
		bf2 = (*e).dy - (*b).dy;
		bf3 = (*e).dx - (*b).dx;

		if (bf2 <= bf1 && bf2 <= bf3) {
			int t = bf2;
			bf2 = bf1;
			bf1 = t;
		}
		else if (bf3 <= bf1 && bf3 <= bf2) {
			int t = bf3;
			bf3 = bf1;
			bf1 = t;
		}
		if (bf3 < bf2) {
			int t = bf3;
			bf3 = bf2;
			bf2 = t;
		}

		soportesAbajo = list<vector<SoporteAbajo>>(0);
		soportesArriba = list<list<SoporteArriba>>(0);
		poligonos = list<Poligono>(0);
		cajasEmpacadas = vector<PackedBox>(0);
	}
	BloqueEmpacado(const int p_x1, const int p_y1, const int p_z1, const int p_x2, const int p_y2, const int p_z2) {

		// Este es un bloque ficticio para hacer los espacios maximales cuando se retiran piezas.

		x1 = p_x1;
		y1 = p_y1;
		z1 = p_z1;
		x2 = p_x2;
		y2 = p_y2;
		z2 = p_z2;
	}

	// Operador

	bool const operator==(BloqueEmpacado const& otro) {
		return x1 == otro.x1 && y1 == otro.y1 && z1 == otro.z1 && x2 == otro.x2 && y2 == otro.y2 && z2 == otro.z2 && b == otro.b;
	}
};
class AreaBase {
public:

	// Parámetros

	int x1, y1, x2, y2;

	// Constructor

	AreaBase(int const& p_x1, int const& p_y1, int const& p_x2, int const& p_y2) {
		x1 = p_x1;
		y1 = p_y1;
		x2 = p_x2;
		y2 = p_y2;
	}
};
class Container;
bool const Orden_Fondo_Altura(MaximalSpace const& m1, MaximalSpace const& m2) {
	if (m1.x1 == m2.x1) {
		if (m1.z1 == m2.z1) {
			return m1.metrica < m2.metrica;
		}
		return m1.z1 > m2.z1;
	}
	return m1.x1 < m2.x1;
}
bool const Orden_Fondo_Bajo(MaximalSpace const& m1, MaximalSpace const& m2) {
	if (m1.x1 == m2.x1) {
		if (m1.z1 == m2.z1) {
			return m1.metrica < m2.metrica;
		}
		return m1.z1 < m2.z1;
	}
	return m1.x1 < m2.x1;
}
bool const Orden_Altura_Fondo(MaximalSpace const& m1, MaximalSpace const& m2) {
	if (m1.z1 == m2.z1) {
		if (m1.x1 == m2.x1) {
			return m1.metrica < m2.metrica;
		}
		return m1.x1 < m2.x1;
	}
	return m1.z1 > m2.z1;
}
bool const Orden_BestFit_MaxVol(BloqueEmpacado const& b1, BloqueEmpacado const& b2) {
	if (b1.bf1 == b2.bf1) {
		if (b1.bf2 == b2.bf2) {
			if (b1.bf3 == b2.bf3) {
				if ((*b1.b).volOcupado == (*b2.b).volOcupado) {
					return (*b1.b).vol < (*b2.b).vol;
				}
				return (*b1.b).volOcupado > (*b2.b).volOcupado;
			}
			return b1.bf3 < b2.bf3;
		}
		return b1.bf2 < b2.bf2;
	}
	return b1.bf1 < b2.bf1;
}
bool const Orden_MaxVol_BestFit(BloqueEmpacado const& b1, BloqueEmpacado const& b2) {
	if ((*b1.b).volOcupado == (*b2.b).volOcupado) {
		if ((*b1.b).vol == (*b2.b).vol) {
			if (b1.bf1 == b2.bf1) {
				if (b1.bf2 == b2.bf2) {
					return b1.bf3 < b2.bf3;
				}
				return b1.bf2 < b2.bf2;
			}
			return b1.bf1 < b2.bf1;
		}
		return (*b1.b).vol < (*b2.b).vol;
	}
	return (*b1.b).volOcupado > (*b2.b).volOcupado;
}

class Container {

	// Parámetros 

public:
	int minDx, minDy, minDz;
	int clienteActual;
	double alpha = 1;
	list<MaximalSpace> espacios;
	list<MaximalSpace> espaciosFast;
	vector<int> cantidadesBoxType;
	double utilizacion, utilizacionBW;
	int volEmpacado = 0;
	vector<list<Bloque*>> bloques;
	vector<BloqueEmpacado> opciones; // Tiene que ser vector por la búsqueda binaria
	vector<PackedBox> empacados; // Tiene que ser vector por la actualización de los soportes que se hace acceso por ínidice
	bool hayCajasPorEmpacar;
	list<int> arbol; // Árbol de cajas empacadas que se obtiene con los soportes abajo
	list<PackedBox> arbolTop; // Cájas del árbol que no tienen nada arriba

	// Constructor

	Container() {
		cantidadesBoxType = cantidadesBoxTypes0;
		ActualizarMinD();
		clienteActual = 0;
		espacios = list<MaximalSpace>(1);
		espaciosFast = list<MaximalSpace>(1);
		utilizacion = 0.0;
		bloques = Bloques00;
		empacados = vector<PackedBox>(0);
		hayCajasPorEmpacar = true;
	}
	Container(Container& c, bool const& copiaCompleta) {
		if (copiaCompleta) {
			minDx = c.minDx;
			minDy = c.minDy;
			minDz = c.minDz;
			clienteActual = c.clienteActual;
			espacios = c.espacios;
			espaciosFast = c.espaciosFast;
			cantidadesBoxType = c.cantidadesBoxType;
			utilizacion = c.utilizacion;
			bloques = c.bloques;
			opciones.clear();
			empacados = c.empacados;
			hayCajasPorEmpacar = c.hayCajasPorEmpacar;
			arbol = c.arbol;
			arbolTop = c.arbolTop;
		}
		else {
			utilizacion = c.utilizacion;
			empacados = c.empacados;
			hayCajasPorEmpacar = c.hayCajasPorEmpacar;
			volEmpacado = 0;
			for (vector<PackedBox>::iterator p_it = empacados.begin(); p_it != empacados.end(); ++p_it) {
				volEmpacado += (*(*p_it).bt).volumenReal;
			}
		}
	}

	// Métodos

private:

	void ActualizarMinD() {
		minDx = ContenedorDimx;
		minDy = ContenedorDimy;
		minDz = ContenedorDimz;
		vector<BoxType>::const_iterator bt = BoxTypes0.begin();
		for (vector<int>::const_iterator b_it = cantidadesBoxType.begin(); b_it < cantidadesBoxType.end(); ++b_it, ++bt) {
			if (*b_it > 0) {
				if ((*bt).minDx < minDx) minDx = (*bt).minDx;
				if ((*bt).minDy < minDy) minDy = (*bt).minDy;
				if ((*bt).minDz < minDz) minDz = (*bt).minDz;
			}
		}
	}

	// Espacios maximales

	void EliminarEspaciosMinD(list<MaximalSpace>& le) {
		for (list<MaximalSpace>::iterator e = le.begin(); e != le.end();) {
			if ((*e).dx < minDx || (*e).dy < minDy || (*e).dz < minDz) {
				e = le.erase(e);
			}
			else {
				++e;
			}
		}
	}
	list<MaximalSpace>::reverse_iterator ActualizarEspacios(const BloqueEmpacado* c) {
		list<MaximalSpace>::reverse_iterator resp = espacios.rbegin();
		if (r_estabilidad == 1) {
			for (list<MaximalSpace>::reverse_iterator e = espacios.rbegin(); e != espacios.rend(); ) {
				if (e != espacios.rbegin()) {
					if (!((*e).x1 >= (*c).x2 || (*e).y1 >= (*c).y2 || (*e).z1 >= (*c).z2 || (*c).x1 >= (*e).x2 || (*c).y1 >= (*e).y2 || (*c).z1 >= (*e).z2)) {

						// Espacios (*e)n x

						if ((*c).x1 > (*e).x1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*c).x1, (*e).y2, (*e).z2));
						if ((*e).x2 > (*c).x2) espacios.push_back(MaximalSpace((*c).x2, (*e).y1, (*e).z1, (*e).x2, (*e).y2, (*e).z2));

						// Espacios (*e)n y

						if ((*c).y1 > (*e).y1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*c).y1, (*e).z2));
						if ((*e).y2 > (*c).y2) espacios.push_back(MaximalSpace((*e).x1, (*c).y2, (*e).z1, (*e).x2, (*e).y2, (*e).z2));

						// Espacios (*e)n z

						if ((*c).z1 > (*e).z1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*e).y2, (*c).z1));
						if ((*e).z2 > (*c).z2) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*c).z2, (*e).x2, (*e).y2, (*e).z2));
						if (e != resp) {
							e = list<MaximalSpace>::reverse_iterator(espacios.erase(next(e).base()));
						}
						else {
							e = list<MaximalSpace>::reverse_iterator(espacios.erase(next(e).base()));
							resp = e;
						}
					}
					else {
						++e;
					}
				}
				else {
					if (!((*e).x1 >= (*c).x2 || (*e).y1 >= (*c).y2 || (*e).z1 >= (*c).z2 || (*c).x1 >= (*e).x2 || (*c).y1 >= (*e).y2 || (*c).z1 >= (*e).z2)) {
						int nE = (int)espacios.size();
						bool seguir = true;

						// Espacios (*e)n x

						if ((*c).x1 > (*e).x1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*c).x1, (*e).y2, (*e).z2));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}
						if ((*e).x2 > (*c).x2) espacios.push_back(MaximalSpace((*c).x2, (*e).y1, (*e).z1, (*e).x2, (*e).y2, (*e).z2));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}

						// Espacios (*e)n y

						if ((*c).y1 > (*e).y1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*c).y1, (*e).z2));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}
						if ((*e).y2 > (*c).y2) espacios.push_back(MaximalSpace((*e).x1, (*c).y2, (*e).z1, (*e).x2, (*e).y2, (*e).z2));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}

						// Espacios (*e)n z

						if ((*c).z1 > (*e).z1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*e).y2, (*c).z1));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}
						if ((*e).z2 > (*c).z2) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*c).z2, (*e).x2, (*e).y2, (*e).z2));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}
						if (e != resp) {
							e = list<MaximalSpace>::reverse_iterator(espacios.erase(next(e).base()));
						}
						else {
							e = list<MaximalSpace>::reverse_iterator(espacios.erase(next(e).base()));
							resp = e;
						}
					}
					else {
						++e;
					}
				}
			}
		}
		else {
			for (list<MaximalSpace>::reverse_iterator e = espacios.rbegin(); e != espacios.rend(); ) {
				if (e != espacios.rbegin()) {
					if (!((*e).x1 >= (*c).x2 || (*e).y1 >= (*c).y2 || (*e).z1 >= (*c).z2 || (*c).x1 >= (*e).x2 || (*c).y1 >= (*e).y2 || (*c).z1 >= (*e).z2)) {

						// Espacios en x

						if ((*c).x1 > (*e).x1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*c).x1, (*e).y2, (*e).z2));
						if ((*e).x2 > (*c).x2) espacios.push_back(MaximalSpace((*c).x2, (*e).y1, (*e).z1, (*e).x2, (*e).y2, (*e).z2));

						// Espacios en y

						if ((*c).y1 > (*e).y1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*c).y1, (*e).z2));
						if ((*e).y2 > (*c).y2) espacios.push_back(MaximalSpace((*e).x1, (*c).y2, (*e).z1, (*e).x2, (*e).y2, (*e).z2));

						// Espacios en z

						if ((*e).z2 > (*c).z2 && min((*c).x2, (*e).x2) > max((*c).x1, (*e).x1) && min((*c).y2, (*e).y2) > max((*c).y1, (*e).y1)) espacios.push_back(MaximalSpace(max((*c).x1, (*e).x1), max((*c).y1, (*e).y1), (*c).z2, min((*c).x2, (*e).x2), min((*c).y2, (*e).y2), (*e).z2));
						//if ((*e).z2 > (*c).z2) espacios.push_back(MaximalSpace((*c).x1, (*c).y1, (*c).z2, (*c).x2, (*c).y2, (*e).z2));
						if (e != resp) {
							e = list<MaximalSpace>::reverse_iterator(espacios.erase(next(e).base()));
						}
						else {
							e = list<MaximalSpace>::reverse_iterator(espacios.erase(next(e).base()));
							resp = e;
						}
					}
					else {
						++e;
					}
				}
				else {
					if (!((*e).x1 >= (*c).x2 || (*e).y1 >= (*c).y2 || (*e).z1 >= (*c).z2 || (*c).x1 >= (*e).x2 || (*c).y1 >= (*e).y2 || (*c).z1 >= (*e).z2)) {
						int nE = (int)espacios.size();
						bool seguir = true;

						// Espacios en x

						if ((*c).x1 > (*e).x1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*c).x1, (*e).y2, (*e).z2));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}
						if ((*e).x2 > (*c).x2) espacios.push_back(MaximalSpace((*c).x2, (*e).y1, (*e).z1, (*e).x2, (*e).y2, (*e).z2));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}

						// Espacios en y

						if ((*c).y1 > (*e).y1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*c).y1, (*e).z2));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}
						if ((*e).y2 > (*c).y2) espacios.push_back(MaximalSpace((*e).x1, (*c).y2, (*e).z1, (*e).x2, (*e).y2, (*e).z2));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}

						// Espacios en z

						if ((*e).z2 > (*c).z2 && min((*c).x2, (*e).x2) > max((*c).x1, (*e).x1) && min((*c).y2, (*e).y2) > max((*c).y1, (*e).y1)) espacios.push_back(MaximalSpace(max((*c).x1, (*e).x1), max((*c).y1, (*e).y1), (*c).z2, min((*c).x2, (*e).x2), min((*c).y2, (*e).y2), (*e).z2));
						//if ((*e).z2 > (*c).z2) espacios.push_back(MaximalSpace((*c).x1, (*c).y1, (*c).z2, (*c).x2, (*c).y2, (*e).z2));
						if (espacios.size() != nE && seguir) {
							++nE;
							e = next(e);
							seguir = false;
						}
						if (e != resp) {
							e = list<MaximalSpace>::reverse_iterator(espacios.erase(next(e).base()));
						}
						else {
							e = list<MaximalSpace>::reverse_iterator(espacios.erase(next(e).base()));
							resp = e;
						}
					}
					else {
						++e;
					}
				}
			}
		}
		return resp;
	}
	list<MaximalSpace>::reverse_iterator ActualizarEspaciosFast(const BloqueEmpacado* c) {
		list<MaximalSpace>::reverse_iterator resp = espaciosFast.rbegin();
		for (list<MaximalSpace>::reverse_iterator e = espaciosFast.rbegin(); e != espaciosFast.rend(); ) {
			if (e != espaciosFast.rbegin()) {
				if (!((*e).x1 >= (*c).x2 || (*e).y1 >= (*c).y2 || (*e).z1 >= (*c).z2 || (*c).x1 >= (*e).x2 || (*c).y1 >= (*e).y2 || (*c).z1 >= (*e).z2)) {

					// espaciosFast en x

					if ((*c).x1 > (*e).x1) espaciosFast.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*c).x1, (*e).y2, (*e).z2));
					if ((*e).x2 > (*c).x2) espaciosFast.push_back(MaximalSpace((*c).x2, (*e).y1, (*e).z1, (*e).x2, (*e).y2, (*e).z2));

					// espaciosFast en y

					if ((*c).y1 > (*e).y1) espaciosFast.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*c).y1, (*e).z2));
					if ((*e).y2 > (*c).y2) espaciosFast.push_back(MaximalSpace((*e).x1, (*c).y2, (*e).z1, (*e).x2, (*e).y2, (*e).z2));

					// espaciosFast en z

					if ((*c).z1 > (*e).z1) espaciosFast.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*e).y2, (*c).z1));
					if ((*e).z2 > (*c).z2 && min((*c).x2, (*e).x2) > max((*c).x1, (*e).x1) && min((*c).y2, (*e).y2) > max((*c).y1, (*e).y1)) espaciosFast.push_back(MaximalSpace(max((*c).x1, (*e).x1), max((*c).y1, (*e).y1), (*c).z2, min((*c).x2, (*e).x2), min((*c).y2, (*e).y2), (*e).z2));
					if (e != resp) {
						e = list<MaximalSpace>::reverse_iterator(espaciosFast.erase(next(e).base()));
					}
					else {
						e = list<MaximalSpace>::reverse_iterator(espaciosFast.erase(next(e).base()));
						resp = e;
					}
				}
				else {
					++e;
				}
			}
			else {
				if (!((*e).x1 >= (*c).x2 || (*e).y1 >= (*c).y2 || (*e).z1 >= (*c).z2 || (*c).x1 >= (*e).x2 || (*c).y1 >= (*e).y2 || (*c).z1 >= (*e).z2)) {
					int nE = (int)espaciosFast.size();
					bool seguir = true;

					// espaciosFast en x

					if ((*c).x1 > (*e).x1) espaciosFast.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*c).x1, (*e).y2, (*e).z2));
					if (espaciosFast.size() != nE && seguir) {
						++nE;
						e = next(e);
						seguir = false;
					}
					if ((*e).x2 > (*c).x2) espaciosFast.push_back(MaximalSpace((*c).x2, (*e).y1, (*e).z1, (*e).x2, (*e).y2, (*e).z2));
					if (espaciosFast.size() != nE && seguir) {
						++nE;
						e = next(e);
						seguir = false;
					}

					// espaciosFast en y

					if ((*c).y1 > (*e).y1) espaciosFast.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*c).y1, (*e).z2));
					if (espaciosFast.size() != nE && seguir) {
						++nE;
						e = next(e);
						seguir = false;
					}
					if ((*e).y2 > (*c).y2) espaciosFast.push_back(MaximalSpace((*e).x1, (*c).y2, (*e).z1, (*e).x2, (*e).y2, (*e).z2));
					if (espaciosFast.size() != nE && seguir) {
						++nE;
						e = next(e);
						seguir = false;
					}

					// espaciosFast en z

					if ((*c).z1 > (*e).z1) espaciosFast.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*e).y2, (*c).z1));
					if (espaciosFast.size() != nE && seguir) {
						++nE;
						e = next(e);
						seguir = false;
					}
					if ((*e).z2 > (*c).z2 && min((*c).x2, (*e).x2) > max((*c).x1, (*e).x1) && min((*c).y2, (*e).y2) > max((*c).y1, (*e).y1)) espaciosFast.push_back(MaximalSpace(max((*c).x1, (*e).x1), max((*c).y1, (*e).y1), (*c).z2, min((*c).x2, (*e).x2), min((*c).y2, (*e).y2), (*e).z2));
					if (espaciosFast.size() != nE && seguir) {
						++nE;
						e = next(e);
						seguir = false;
					}
					if (e != resp) {
						e = list<MaximalSpace>::reverse_iterator(espaciosFast.erase(next(e).base()));
					}
					else {
						e = list<MaximalSpace>::reverse_iterator(espaciosFast.erase(next(e).base()));
						resp = e;
					}
				}
				else {
					++e;
				}
			}
		}
		return resp;
	}
	void JuntarEspacios(list<MaximalSpace>::reverse_iterator desde, list<MaximalSpace>& le) {

		// Dimención en z de los espacios

		for (list<MaximalSpace>::iterator e = le.begin(); e != le.end(); ++e) {
			if ((*e).z2 < ContenedorDimz) {
				bool disminuir = false;
				for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
					if ((*e).z2 == (*p).z1 && !((*e).x1 >= (*p).x2 || (*p).x1 >= (*e).x2 || (*e).y1 >= (*p).y2 || (*p).y1 >= (*e).y2)) {
						disminuir = true;
						break;
					}
				}
				if (disminuir) {
					(*e).dz -= 1;
					(*e).z2 -= 1;
				}
			}
		}

		// Eliminar espacios contenidos

		for (list<MaximalSpace>::iterator e2 = le.begin(); e2 != le.end(); ++e2) {
			for (list<MaximalSpace>::iterator e1 = next(e2); e1 != le.end();) {

				bool siguiente = true;

				// Ver si (*e1) contiene a (*e2)

				if ((*e1).z1 <= (*e2).z1 && (*e2).z2 <= (*e1).z2) {
					if ((*e1).y1 <= (*e2).y1 && (*e2).y2 <= (*e1).y2) {
						if ((*e1).x1 <= (*e2).x1 && (*e2).x2 <= (*e1).x2) {
							bool actualizarDesde = false;
							if (desde != le.rend()) {
								if (e2 == prev(desde.base())) {
									desde = next(desde); // Avanza el reverse_iterator
								}
							}
							else if (e2 == le.begin()) actualizarDesde = true;
							e2 = le.erase(e2);
							if (actualizarDesde) desde = le.rend();
							if (e2 != le.end() && e2 != prev(le.end())) {
								e1 = next(e2);
								siguiente = false;
							}
							else {
								break;
							}
						}
					}
				}

				// Ver si (*e2) contiene a (*e1)

				if ((*e2).z1 <= (*e1).z1 && (*e1).z2 <= (*e2).z2) {
					if ((*e2).y1 <= (*e1).y1 && (*e1).y2 <= (*e2).y2) {
						if ((*e2).x1 <= (*e1).x1 && (*e1).x2 <= (*e2).x2) {
							bool actualizarDesde = false;
							if (desde != le.rend()) {
								if (e1 == prev(desde.base())) {
									desde = next(desde);
									if (e1 == prev(le.end())) actualizarDesde = true;
								}
							}
							e1 = le.erase(e1);
							if (actualizarDesde) desde = le.rbegin();
							siguiente = false;
						}
					}
				}

				if (siguiente) ++e1;
			}
		}

		// Juntar y expandir

		if (r_juntarEspacios == 1) { // Se juntan espacios pero no se expanden
			bool seguir = true;
			while (seguir) {
				seguir = false;
				for (list<MaximalSpace>::reverse_iterator e2 = le.rbegin(); e2 != desde; ++e2) {
					for (list<MaximalSpace>::reverse_iterator e1 = next(e2); e1 != le.rend(); ++e1) {

						// Ver si se pueden juntar

						if ((*e1).z1 == (*e2).z1 && (*e1).z2 == (*e2).z2) {
							if ((*e1).y1 == (*e2).y1 && (*e1).y2 == (*e2).y2) {
								if (!((*e1).x1 > (*e2).x2 || (*e2).x1 > (*e1).x2)) {
									(*e1).x1 = min((*e1).x1, (*e2).x1);
									(*e1).x2 = max((*e1).x2, (*e2).x2);
									(*e1).dx = (*e1).x2 - (*e1).x1;
									(*e1).cambio = true;
									e2 = list<MaximalSpace>::reverse_iterator(le.erase(next(e2).base()));
									if (e2 != le.rend()) {
										e1 = e2;
										seguir = true;
									}
									else {
										break;
									}
								}
							}
							else if ((*e1).x1 == (*e2).x1 && (*e1).x2 == (*e2).x2) {
								if (!((*e1).y1 > (*e2).y2 || (*e2).y1 > (*e1).y2)) {
									(*e1).y1 = min((*e1).y1, (*e2).y1);
									(*e1).y2 = max((*e1).y2, (*e2).y2);
									(*e1).dy = (*e1).y2 - (*e1).y1;
									(*e1).cambio = true;
									e2 = list<MaximalSpace>::reverse_iterator(le.erase(next(e2).base()));
									if (e2 != le.rend()) {
										e1 = e2;
										seguir = true;
									}
									else {
										break;
									}
								}
							}
						}
					}
				}
			}
		}
		else if (r_juntarEspacios == 2) { // Se juntan y se expanden espacios
			bool seguir = true;
			while (seguir) {
				seguir = false;
				for (list<MaximalSpace>::reverse_iterator e2 = le.rbegin(); e2 != desde; ++e2) {
					for (list<MaximalSpace>::reverse_iterator e1 = next(e2); e1 != le.rend(); ++e1) {

						// Ver si se pueden juntar

						if ((*e1).z1 == (*e2).z1 && (*e1).z2 == (*e2).z2) {
							if ((*e1).y1 == (*e2).y1 && (*e1).y2 == (*e2).y2) {
								if (!((*e1).x1 > (*e2).x2 || (*e2).x1 > (*e1).x2)) {
									(*e1).x1 = min((*e1).x1, (*e2).x1);
									(*e1).x2 = max((*e1).x2, (*e2).x2);
									(*e1).dx = (*e1).x2 - (*e1).x1;
									(*e1).cambio = true;
									e2 = list<MaximalSpace>::reverse_iterator(le.erase(next(e2).base()));
									seguir = true;
									break;
								}
							}
							else if ((*e1).x1 == (*e2).x1 && (*e1).x2 == (*e2).x2) {
								if (!((*e1).y1 > (*e2).y2 || (*e2).y1 > (*e1).y2)) {
									(*e1).y1 = min((*e1).y1, (*e2).y1);
									(*e1).y2 = max((*e1).y2, (*e2).y2);
									(*e1).dy = (*e1).y2 - (*e1).y1;
									(*e1).cambio = true;
									e2 = list<MaximalSpace>::reverse_iterator(le.erase(next(e2).base()));
									seguir = true;
									break;
								}
							}

							// Ver si e1 se puede expandir con e2

							if ((*e2).y1 <= (*e1).y1 && (*e1).y2 <= (*e2).y2) {
								if (!((*e1).x1 > (*e2).x2 || (*e2).x1 > (*e1).x2)) {
									int mm = min((*e1).x1, (*e2).x1);
									int MM = max((*e1).x2, (*e2).x2);
									if ((*e1).x1 != mm || (*e1).x2 != MM) {
										(*e1).x1 = mm;
										(*e1).x2 = MM;
										(*e1).dx = MM - mm;
										(*e1).cambio = true;
										seguir = true;
									}
								}
							}
							if ((*e2).x1 <= (*e1).x1 && (*e1).x2 <= (*e2).x2) {
								if (!((*e1).y1 > (*e2).y2 || (*e2).y1 > (*e1).y2)) {
									int mm = min((*e1).y1, (*e2).y1);
									int MM = max((*e1).y2, (*e2).y2);
									if ((*e1).y1 != mm || (*e1).y2 != MM) {
										(*e1).y1 = mm;
										(*e1).y2 = MM;
										(*e1).dy = MM - mm;
										(*e1).cambio = true;
										seguir = true;
									}
								}
							}

							// Ver si e2 se puede expandir con e1

							if ((*e1).y1 <= (*e2).y1 && (*e2).y2 <= (*e1).y2) {
								if (!((*e2).x1 > (*e1).x2 || (*e1).x1 > (*e2).x2)) {
									int mm = min((*e2).x1, (*e1).x1);
									int MM = max((*e2).x2, (*e1).x2);
									if ((*e2).x1 != mm || (*e2).x2 != MM) {
										(*e2).x1 = mm;
										(*e2).x2 = MM;
										(*e2).dx = MM - mm;
										(*e2).cambio = true;
										seguir = true;
									}
								}
							}
							if ((*e1).x1 <= (*e2).x1 && (*e2).x2 <= (*e1).x2) {
								if (!((*e2).y1 > (*e1).y2 || (*e1).y1 > (*e2).y2)) {
									int mm = min((*e2).y1, (*e1).y1);
									int MM = max((*e2).y2, (*e1).y2);
									if ((*e2).y1 != mm || (*e2).y2 != MM) {
										(*e2).y1 = mm;
										(*e2).y2 = MM;
										(*e2).dy = MM - mm;
										(*e2).cambio = true;
										seguir = true;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	void DeterminarIndicesCajasUnEspacio(MaximalSpace* e_it) {
		(*e_it).cambio = false;
		if ((*e_it).z1 > 0) {
			(*e_it).indCajas.clear();
			if (r_juntarEspacios == 0) {
				for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
					if ((*p).z2 == (*e_it).z1) {
						if (!((*p).x1 >= (*e_it).x2 || (*e_it).x1 >= (*p).x2)) {
							if (!((*p).y1 >= (*e_it).y2 || (*e_it).y1 >= (*p).y2)) {
								(*e_it).indCajas.push_back((*p).id);
								DeterminarEsquina(e_it);
								return;
							}
						}
					}
				}
			}
			else {
				for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
					if ((*p).z2 == (*e_it).z1) {
						if (!((*p).x1 >= (*e_it).x2 || (*e_it).x1 >= (*p).x2)) {
							if (!((*p).y1 >= (*e_it).y2 || (*e_it).y1 >= (*p).y2)) {
								(*e_it).indCajas.push_back((*p).id);
							}
						}
					}
				}
			}

		}
		DeterminarEsquina(e_it);
	}
	void ActualizarIndicesCajasDeEspacios(list<MaximalSpace>& le) {
		for (list<MaximalSpace>::iterator e_it = le.begin(); e_it != le.end();) {
			bool siguiente = true;
			if ((*e_it).cambio) {
				DeterminarIndicesCajasUnEspacio(&*e_it);
				if ((*e_it).z1 > 0 && (*e_it).indCajas.size() == 0) {
					e_it = le.erase(e_it);
					siguiente = false;
				}
			}
			if (siguiente) ++e_it;
		}
	}
	void ActualizarEspaciosCapas(list<MaximalSpace>& le) {

		// Determinar el x

		int x0 = empacados.back().x2;
		int ultimoCliente = (*empacados.back().bt).cliente;
		for (vector<PackedBox>::const_reverse_iterator it = empacados.rbegin(); it != empacados.rend(); ++it) {
			if (ultimoCliente != (*(*it).bt).cliente) {
				break;
			}
			else if ((*it).x2 > x0) {
				x0 = (*it).x2;
			}
		}

		// Sería un solo espacio maximal

		le = list<MaximalSpace>(1, MaximalSpace(x0, 0, 0, ContenedorDimx, ContenedorDimy, ContenedorDimz));
		le.back().indEsquina = 1;
	}
	void ActualizarEspaciosVisibilidad() {
		if (r_estabilidad == 1) {
			for (list<MaximalSpace>::iterator e = espacios.begin(); e != espacios.end();) {
				if ((*e).x2 < ContenedorDimx) {
					e = espacios.erase(e);
				}
				else if ((*e).z2 < ContenedorDimz) {
					// esto se hace para que no se disminuya innecesariamente un espacio ya disminuido
					bool disminuir = false;
					for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
						if ((*e).z2 == (*p).z1 && !((*e).x1 >= (*p).x2 || (*p).x1 >= (*e).x2 || (*e).y1 >= (*p).y2 || (*p).y1 >= (*e).y2)) {
							disminuir = true;
							break;
						}
					}
					if (disminuir) {
						if ((*e).dz > 1) {
							(*e).dz -= 1;
							(*e).z2 -= 1;
							++e;
						}
						else {
							e = espacios.erase(e);
						}
					}
					else {
						++e;
					}
				}
				else {
					++e;
				}
			}
		}
		else {
			for (list<MaximalSpace>::iterator e = espacios.begin(); e != espacios.end();) {
				if ((*e).z1 > 0) (*e).cambio = true;
				if ((*e).x2 < ContenedorDimx) {
					bool avanzar = true;
					for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
						if ((*p).x2 > (*e).x1 && !((*p).y1 >= (*e).y2 || (*e).y1 >= (*p).y2 || (*p).z1 >= (*e).z2 || (*e).z1 >= (*p).z2)) {

							if ((*p).y1 > (*e).y1) espacios.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*p).y1, (*e).z2));
							if ((*e).y2 > (*p).y2) espacios.push_back(MaximalSpace((*e).x1, (*p).y2, (*e).z1, (*e).x2, (*e).y2, (*e).z2));

							avanzar = false;
							e = espacios.erase(e);
							break;
						}
					}
					if (avanzar) ++e;
				}
				else {
					++e;
				}				
			}
		}
	}
	void ActualizarEspaciosVisibilidadFast() {
		for (list<MaximalSpace>::iterator e = espaciosFast.begin(); e != espaciosFast.end();) {
			if ((*e).z1 > 0) (*e).cambio = true;
			if ((*e).z2 < ContenedorDimz) {
				// esto se hace para que no se disminuya innecesariamente un espacio ya disminuido
				bool disminuir = false;
				for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
					if ((*e).z2 == (*p).z1 && !((*e).x1 >= (*p).x2 || (*p).x1 >= (*e).x2 || (*e).y1 >= (*p).y2 || (*p).y1 >= (*e).y2)) {
						disminuir = true;
						break;
					}
				}
				if (disminuir) {
					(*e).dz -= 1;
					(*e).z2 -= 1;
				}
			}
			if ((*e).x2 < ContenedorDimx) {
				bool avanzar = true;
				for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
					if ((*p).x2 > (*e).x1 && !((*p).y1 >= (*e).y2 || (*e).y1 >= (*p).y2 || (*p).z1 >= (*e).z2 || (*e).z1 >= (*p).z2)) {

						if ((*p).y1 > (*e).y1) espaciosFast.push_back(MaximalSpace((*e).x1, (*e).y1, (*e).z1, (*e).x2, (*p).y1, (*e).z2));
						if ((*e).y2 > (*p).y2) espaciosFast.push_back(MaximalSpace((*e).x1, (*p).y2, (*e).z1, (*e).x2, (*e).y2, (*e).z2));

						avanzar = false;
						e = espaciosFast.erase(e);
						break;
					}
				}
				if (avanzar) ++e;
			}
			else {
				++e;
			}
		}
	}
	void DeterminarEsquina(MaximalSpace* e_it) {
		if (r_multidrop > 0) { // Se consideran las dos esquinas traseras-inferiores
			if ((*e_it).z1 == 0) {
				(*e_it).maxSopx = ContenedorDimx;
				if ((*e_it).y1 <= ContenedorDimy - (*e_it).y2) {
					(*e_it).indEsquina = 1;
				}
				else {
					(*e_it).indEsquina = 2;
				}
			}
			else {
				double areay = 0;
				double areaTotal = 0;
				(*e_it).maxSopx = 0;
				for (list<int>::iterator it = (*e_it).indCajas.begin(); it != (*e_it).indCajas.end(); ++it) {
					PackedBox* pit = &empacados[*it];
					vector<int> interx({ (*e_it).x1, (*pit).x1, (*pit).x2, (*e_it).x2 });
					sort(interx.begin(), interx.end());
					vector<int> intery({ (*e_it).y1, (*pit).y1, (*pit).y2, (*e_it).y2 });
					sort(intery.begin(), intery.end());
					double nuevaArea = (interx[2] - interx[1]) * (intery[2] - intery[1]);
					double py = (intery[2] + intery[1]) / 2.0;
					areay = areay * areaTotal + py * nuevaArea;
					areaTotal += nuevaArea;
					areay /= areaTotal;
					(*e_it).maxSopx = max((*e_it).maxSopx, (*pit).x2);
				}
				double centroEy = (double)((*e_it).y2 + (*e_it).y1) / 2.0;
				if (abs(areay - centroEy) < error) {
					if ((*e_it).y1 <= ContenedorDimy - (*e_it).y2) {
						(*e_it).indEsquina = 1;
					}
					else {
						(*e_it).indEsquina = 2;
					}
				}
				else if (areay < centroEy) {
					(*e_it).indEsquina = 1;
				}
				else {
					(*e_it).indEsquina = 2;
				}
			}
		}
		else { // Se consideran las 4 esquinas inferiores
			(*e_it).maxSopx = ContenedorDimx;
			if ((*e_it).z1 == 0) {
				int v1 = (*e_it).x1 + (*e_it).y1;
				int v2 = (*e_it).x1 + ContenedorDimy - (*e_it).y2;
				int v3 = ContenedorDimx - (*e_it).x2 + (*e_it).y1;
				int v4 = ContenedorDimx - (*e_it).x2 + ContenedorDimy - (*e_it).y2;
				if (v1 <= v2 && v1 <= v3 && v1 <= v4) {
					(*e_it).indEsquina = 1;
				}
				else if (v2 <= v1 && v2 <= v3 && v2 <= v4) {
					(*e_it).indEsquina = 2;
				}
				else if (v3 <= v1 && v3 <= v2 && v3 <= v4) {
					(*e_it).indEsquina = 3;
				}
				else {
					(*e_it).indEsquina = 4;
				}
			}
			else {
				double areax = 0;
				double areay = 0;
				double areaTotal = 0;
				for (list<int>::iterator it = (*e_it).indCajas.begin(); it != (*e_it).indCajas.end(); ++it) {
					PackedBox* pit = &empacados[*it];
					vector<int> interx({ (*e_it).x1, (*pit).x1, (*pit).x2, (*e_it).x2 });
					sort(interx.begin(), interx.end());
					vector<int> intery({ (*e_it).y1, (*pit).y1, (*pit).y2, (*e_it).y2 });
					sort(intery.begin(), intery.end());
					double nuevaArea = (interx[2] - interx[1]) * (intery[2] - intery[1]);
					double px = (interx[2] + interx[1]) / 2.0;
					double py = (intery[2] + intery[1]) / 2.0;
					areax = areax * areaTotal + px * nuevaArea;
					areay = areay * areaTotal + py * nuevaArea;
					areaTotal += nuevaArea;
					areax /= areaTotal;
					areay /= areaTotal;
				}
				double centroEy = (double)((*e_it).y2 + (*e_it).y1) / 2.0;
				double centroEx = (double)((*e_it).x2 + (*e_it).x1) / 2.0;
				if (abs(areax - centroEx) < error && abs(areay - centroEy) < error) {
					int v1 = (*e_it).x1 + (*e_it).y1;
					int v2 = (*e_it).x1 + ContenedorDimy - (*e_it).y2;
					int v3 = ContenedorDimx - (*e_it).x2 + (*e_it).y1;
					int v4 = ContenedorDimx - (*e_it).x2 + ContenedorDimy - (*e_it).y2;
					if (v1 <= v2 && v1 <= v3 && v1 <= v4) {
						(*e_it).indEsquina = 1;
					}
					else if (v2 <= v1 && v2 <= v3 && v2 <= v4) {
						(*e_it).indEsquina = 2;
					}
					else if (v3 <= v1 && v3 <= v2 && v3 <= v4) {
						(*e_it).indEsquina = 3;
					}
					else {
						(*e_it).indEsquina = 4;
					}
				}
				else if (abs(areax - centroEx) < error) {
					if (areay < centroEy) { // 1 o 3
						if ((*e_it).x1 <= (ContenedorDimx - (*e_it).x2)) {
							(*e_it).indEsquina = 1;
						}
						else {
							(*e_it).indEsquina = 3;
						}
					}
					else { // 2 o 4
						if ((*e_it).x1 <= (ContenedorDimx - (*e_it).x2)) {
							(*e_it).indEsquina = 2;
						}
						else {
							(*e_it).indEsquina = 4;
						}
					}
				}
				else if (abs(areay - centroEy) < error) {
					if (areax < centroEx) { // 1 o 2
						if ((*e_it).y1 <= (ContenedorDimy - (*e_it).y2)) {
							(*e_it).indEsquina = 1;
						}
						else {
							(*e_it).indEsquina = 2;
						}
					}
					else { // 3 o 4
						if ((*e_it).y1 <= (ContenedorDimy - (*e_it).y2)) {
							(*e_it).indEsquina = 3;
						}
						else {
							(*e_it).indEsquina = 4;
						}
					}
				}
				else if (areax < centroEx && areay < centroEy) {
					(*e_it).indEsquina = 1;
				}
				else if (areax < centroEx && areay > centroEy) {
					(*e_it).indEsquina = 2;
				}
				else if (areax > centroEx && areay < centroEy) {
					(*e_it).indEsquina = 3;
				}
				else {
					(*e_it).indEsquina = 4;
				}
			}
		}
	}
	/*
	void ValidarSolapamientoEspaciosMaximales(list<MaximalSpace>& le) {
		for (list<MaximalSpace>::iterator e = le.begin(); e != le.end(); ++e) {
			for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
				if (!((*e).x1 >= (*p).x2 || (*p).x1 >= (*e).x2 || (*e).y1 >= (*p).y2 || (*p).y1 >= (*e).y2 || (*e).z1 >= (*p).z2 || (*p).z1 >= (*e).z2)) {
					cout << "ERROR AQUI" << endl;
					return;
				}
			}
		}
	}
	*/
	/*
	void ValidarIndicesEspaciosMaximales(list<MaximalSpace>& le) {
		for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
			if ((*p).id != distance(empacados.begin(), p)) {
				cout << "ERROR Parte" << endl;
				return;
			}
		}
		int maxId = empacados.size();
		for (list<MaximalSpace>::iterator e = le.begin(); e != le.end(); ++e) {
			for (list<int>::iterator ind = (*e).indCajas.begin(); ind != (*e).indCajas.end(); ++ind) {
				if (*ind >= maxId) {
					cout << "ERROR AQUI" << endl;
				}
			}
		}
	}
	*/
	// Selección Espacios Maximales

	void SeleccionarEspacio_Fondo_Altura(list<MaximalSpace>::iterator& MejorEspacio, vector<list<MaximalSpace>::iterator> const& esp, list<MaximalSpace>& le) {
		if (esp.size() == 0) {
			for (list<MaximalSpace>::iterator e_it = next(le.begin()); e_it != le.end(); ++e_it) {
				if ((*e_it).x1 < (*MejorEspacio).x1) MejorEspacio = e_it;
				else if ((*e_it).x1 == (*MejorEspacio).x1) {
					if ((*e_it).z1 > (*MejorEspacio).z1) MejorEspacio = e_it;
					else if ((*e_it).z1 == (*MejorEspacio).z1) {
						if ((*e_it).metrica < (*MejorEspacio).metrica) MejorEspacio = e_it;
					}
				}
			}
		}
		else {

			// Primera asignación

			list<MaximalSpace>::iterator e_it = le.begin();
			for (; e_it != le.end(); ++e_it) {
				bool sirve = true;
				for (vector<list<MaximalSpace>::iterator>::const_iterator ie = esp.begin(); ie != esp.end(); ++ie) {
					if (*ie == e_it) {
						sirve = false;
						break;
					}
				}
				if (sirve) {
					MejorEspacio = e_it;
					break;
				}
			}

			// Siguientes

			if (e_it != le.end()) {
				for (e_it = next(e_it); e_it != le.end(); ++e_it) {
					bool sirve = true;
					for (vector<list<MaximalSpace>::iterator>::const_iterator ie = esp.begin(); ie != esp.end(); ++ie) {
						if (*ie == e_it) {
							sirve = false;
							break;
						}
					}
					if (sirve) {
						if ((*e_it).x1 < (*MejorEspacio).x1) MejorEspacio = e_it;
						else if ((*e_it).x1 == (*MejorEspacio).x1) {
							if ((*e_it).z1 > (*MejorEspacio).z1) MejorEspacio = e_it;
							else if ((*e_it).z1 == (*MejorEspacio).z1) {
								if ((*e_it).metrica < (*MejorEspacio).metrica) MejorEspacio = e_it;
							}
						}
					}
				}
			}
		}
	}
	void SeleccionarEspacio_Fondo_Bajo(list<MaximalSpace>::iterator& MejorEspacio, vector<list<MaximalSpace>::iterator> const& esp) {
		if (esp.size() == 0) {
			for (list<MaximalSpace>::iterator e_it = next(espacios.begin()); e_it != espacios.end(); ++e_it) {
				if ((*e_it).x1 < (*MejorEspacio).x1) MejorEspacio = e_it;
				else if ((*e_it).x1 == (*MejorEspacio).x1) {
					if ((*e_it).z1 < (*MejorEspacio).z1) MejorEspacio = e_it;
					else if ((*e_it).z1 == (*MejorEspacio).z1) {
						if ((*e_it).metrica < (*MejorEspacio).metrica) MejorEspacio = e_it;
					}
				}
			}
		}
		else {

			// Primera asignación

			list<MaximalSpace>::iterator e_it = espacios.begin();
			for (; e_it != espacios.end(); ++e_it) {
				bool sirve = true;
				for (vector<list<MaximalSpace>::iterator>::const_iterator ie = esp.begin(); ie != esp.end(); ++ie) {
					if (*ie == e_it) {
						sirve = false;
						break;
					}
				}
				if (sirve) {
					MejorEspacio = e_it;
					break;
				}
			}

			// Siguientes

			if (e_it != espacios.end()) {
				for (e_it = next(e_it); e_it != espacios.end(); ++e_it) {
					bool sirve = true;
					for (vector<list<MaximalSpace>::iterator>::const_iterator ie = esp.begin(); ie != esp.end(); ++ie) {
						if (*ie == e_it) {
							sirve = false;
							break;
						}
					}
					if (sirve) {
						if ((*e_it).x1 < (*MejorEspacio).x1) MejorEspacio = e_it;
						else if ((*e_it).x1 == (*MejorEspacio).x1) {
							if ((*e_it).z1 < (*MejorEspacio).z1) MejorEspacio = e_it;
							else if ((*e_it).z1 == (*MejorEspacio).z1) {
								if ((*e_it).metrica < (*MejorEspacio).metrica) MejorEspacio = e_it;
							}
						}
					}
				}
			}
		}
	}
	void SeleccionarEspacio_Altura_Fondo(list<MaximalSpace>::iterator& MejorEspacio, vector<list<MaximalSpace>::iterator> const& esp) {
		if (esp.size() == 0) {
			for (list<MaximalSpace>::iterator e_it = next(espacios.begin()); e_it != espacios.end(); ++e_it) {
				if ((*e_it).z1 > (*MejorEspacio).z1) MejorEspacio = e_it;
				else if ((*e_it).z1 == (*MejorEspacio).z1) {
					if ((*e_it).x1 < (*MejorEspacio).x1) MejorEspacio = e_it;
					else if ((*e_it).x1 == (*MejorEspacio).x1) {
						if ((*e_it).metrica < (*MejorEspacio).metrica) MejorEspacio = e_it;
					}
				}
			}
		}
		else {

			// Primera asignación

			list<MaximalSpace>::iterator e_it = espacios.begin();
			for (; e_it != espacios.end(); ++e_it) {
				bool sirve = true;
				for (vector<list<MaximalSpace>::iterator>::const_iterator ie = esp.begin(); ie != esp.end(); ++ie) {
					if (*ie == e_it) {
						sirve = false;
						break;
					}
				}
				if (sirve) {
					MejorEspacio = e_it;
					break;
				}
			}

			// Siguientes

			if (e_it != espacios.end()) {
				for (e_it = next(e_it); e_it != espacios.end(); ++e_it) {
					bool sirve = true;
					for (vector<list<MaximalSpace>::iterator>::const_iterator ie = esp.begin(); ie != esp.end(); ++ie) {
						if (*ie == e_it) {
							sirve = false;
							break;
						}
					}
					if (sirve) {
						if ((*e_it).z1 > (*MejorEspacio).z1) MejorEspacio = e_it;
						else if ((*e_it).z1 == (*MejorEspacio).z1) {
							if ((*e_it).x1 < (*MejorEspacio).x1) MejorEspacio = e_it;
							else if ((*e_it).x1 == (*MejorEspacio).x1) {
								if ((*e_it).metrica < (*MejorEspacio).metrica) MejorEspacio = e_it;
							}
						}
					}
				}
			}
		}
	}

	// Selección Items

	void DeterminarArboles(const MaximalSpace* e) {

		// Determinar el árbol de bajada con los soportes inferiores

		arbol.clear();
		arbolTop.clear();
		if ((*e).indCajas.size() == 0) return;
		for (list<int>::const_iterator p = (*e).indCajas.begin(); p != (*e).indCajas.end(); ++p) {
			arbol.push_back(*p);
			arbolTop.push_back(empacados[*p]);
		}
		list<int>::iterator desde = arbol.begin();
		list<int>::iterator hasta = prev(arbol.end());
		while (desde != arbol.end()) {
			for (list<int>::const_iterator p_i = desde; p_i != next(hasta); ++p_i) {
				PackedBox* p = &empacados[*p_i];
				for (vector<SoporteAbajo>::const_iterator s = (*p).soportes.begin(); s != (*p).soportes.end(); ++s) {
					arbol.push_back((*s).indPB);
				}
			}
			desde = next(hasta);
			hasta = prev(arbol.end());
		}

		// Determinar los elementos únicos del árbol de bajada

		arbol.sort([&](const int p1i, const int p2i) {
			PackedBox* p1 = &empacados[p1i];
			PackedBox* p2 = &empacados[p2i];
			if ((*p1).z2 == (*p2).z2) {
				return (*p1).id < (*p2).id;
			}
			return (*p1).z2 > (*p2).z2;
			});
		arbol.unique();
	}
	int miBusquedaBinariaBF(double const& val) {
		if (opciones.back().bf1 < val) return (int)opciones.size() - 1;
		int l = 0;
		int r = (int)opciones.size() - 1;
		while (l < r) {
			int m = (l + r) / 2;
			if (opciones[m].bf1 > val) r = m - 1;
			else l = m + 1;
		}
		return r;
	}
	int miBusquedaBinariaVol(double const& val) {
		if ((*opciones.back().b).vol < val) return (int)opciones.size() - 1;
		int l = 0;
		int r = (int)opciones.size() - 1;
		while (l < r) {
			int m = (l + r) / 2;
			if ((*opciones[m].b).vol > val) r = m - 1;
			else l = m + 1;
		}
		return r;
	}
	bool BloqueSoportado(BloqueEmpacado* b, const MaximalSpace* e) {
		if ((*e).indCajas.size() == 0) return true;

		// Reestablecer las cajas top

		for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
			empacados[(*p).id] = *p;
		}

		// Se evalúa si existe soporte para cada caja inferior del bloque

		(*b).soportesAbajo = list<vector<SoporteAbajo>>(0);
		(*b).soportesArriba = list<list<SoporteArriba>>(0);
		for (vector<PackedBox>::const_iterator p = (*(*b).b).cajas.begin(); p != (*(*b).b).cajas.end(); ++p) {
			if ((*p).z1 == 0) {
				(*b).soportesAbajo.push_back(vector<SoporteAbajo>(0));
				(*b).soportesArriba.push_back(list<SoporteArriba>(0));
				vector<Point> intersecciones;
				for (list<int>::const_iterator pp_i = (*e).indCajas.begin(); pp_i != (*e).indCajas.end(); ++pp_i) {
					PackedBox* pp = &empacados[*pp_i];
					if (!((*p).x1 + (*b).x1 >= (*pp).x2 || (*pp).x1 >= (*p).x2 + (*b).x1)) {
						if (!((*p).y1 + (*b).y1 >= (*pp).y2 || (*pp).y1 >= (*p).y2 + (*b).y1)) {

							// Se encuentran los putnos de intersección

							vector<int> misx({ (*p).x1 + (*b).x1, (*pp).x1, (*pp).x2, (*p).x2 + (*b).x1 });
							sort(misx.begin(), misx.end());
							vector<int> misy({ (*p).y1 + (*b).y1, (*pp).y1, (*pp).y2, (*p).y2 + (*b).y1 });
							sort(misy.begin(), misy.end());
							intersecciones.push_back(Point(misx[1], misy[1]));
							intersecciones.push_back(Point(misx[2], misy[1]));
							intersecciones.push_back(Point(misx[1], misy[2]));
							intersecciones.push_back(Point(misx[2], misy[2]));

							// Se crea el soporte

							(*b).soportesAbajo.back().push_back(SoporteAbajo((*pp).id, (misx[2] - misx[1]) * (misy[2] - misy[1]), (double)(misx[2] + misx[1]) / 2.0, (double)(misy[2] + misy[1]) / 2.0, (*(*pp).b).presion));
							(*b).soportesArriba.back().push_back(SoporteArriba(empacados.size() + (*p).id, (*b).soportesAbajo.back().size() - 1));
						}
					}
				}

				// Determinar polígono

				if (intersecciones.size() > 0) {
					(*b).poligonos.push_back(Poligono(intersecciones));
					if (!(*b).poligonos.back().puntoDentroPoligono((*p).centroMasax + (double)(*b).x1, (*p).centroMasay + (double)(*b).y1)) {
						if (!(*b).poligonos.back().puntoDentroPoligono((*p).cx + (double)(*b).x1, (*p).cy + (double)(*b).y1)) {
							return false;
						}
					}
				}
				else {
					return false;
				}
			}
		}

		// Se empacan las cajas inferiores del bloque

		int nCajas = empacados.size();
		empacados.insert(empacados.end(), (*(*b).b).cajas.begin(), (*(*b).b).cajas.end());
		list<vector<SoporteAbajo>>::iterator s1 = (*b).soportesAbajo.begin();
		list<list<SoporteArriba>>::iterator s2 = (*b).soportesArriba.begin();
		for (vector<PackedBox>::iterator p = empacados.begin() + nCajas; p != empacados.end(); ++p) {
			if ((*p).z1 == 0) {
				(*p).soportes = *s1;
				vector<SoporteAbajo>::iterator s11 = (*s1).begin();
				for (list<SoporteArriba>::iterator s22 = (*s2).begin(); s22 != (*s2).end(); ++s22, ++s11) {
					empacados[(*s11).indPB].soportesArriba.push_back(*s22);
				}
				++s1;
				++s2;
				(*p).centroMasax += (*b).x1;
				(*p).cx += (*b).x1;
				(*p).x1 += (*b).x1;
				(*p).x2 += (*b).x1;
				(*p).centroMasay += (*b).y1;
				(*p).cy += (*b).y1;
				(*p).y1 += (*b).y1;
				(*p).y2 += (*b).y1;
				(*p).z1 += (*b).z1;
				(*p).z2 += (*b).z1;
				(*p).DistribuirPesoEntreSoportes();
			}
		}

		// Se actualiza el árbol

		list<int>::iterator it = arbol.begin();
		PackedBox* pit = nullptr;
		int actualz = 0;
		if (it != arbol.end()) {
			pit = &empacados[*it];
			actualz = (*pit).z2;
		}
		while (it != arbol.end()) {
			if ((*pit).z2 == actualz) {
				(*pit).ActualizarCentroMasaYPeso(&empacados);
				if (!(*pit).soportada) {
					empacados.resize(nCajas);
					return false;
				}
				++it;
				if (it != arbol.end()) {
					pit = &empacados[*it];
				}
			}
			else {

				// Distribuir los pesos

				for (list<int>::reverse_iterator it2 = list<int>::reverse_iterator(it); it2 != arbol.rend(); ++it2) {
					PackedBox* pit2 = &empacados[*it2];
					if ((*pit2).z2 != actualz) break;
					(*pit2).DistribuirPesoEntreSoportes();
				}

				// Actualización

				actualz = (*pit).z2;
			}
		}

		// Fin

		empacados.resize(nCajas);
		return true;
	}
	bool BloqueSoportadoPresion(BloqueEmpacado* b, const MaximalSpace* e) {
		if ((*e).indCajas.size() == 0) return true;

		// Reestablecer las cajas top

		for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
			empacados[(*p).id] = *p;
		}

		// Se evalúa si existe soporte para cada caja inferior del bloque

		(*b).soportesAbajo = list<vector<SoporteAbajo>>(0);
		(*b).soportesArriba = list<list<SoporteArriba>>(0);
		for (vector<PackedBox>::const_iterator p = (*(*b).b).cajas.begin(); p != (*(*b).b).cajas.end(); ++p) {
			if ((*p).z1 == 0) {
				(*b).soportesAbajo.push_back(vector<SoporteAbajo>(0));
				(*b).soportesArriba.push_back(list<SoporteArriba>(0));
				vector<Point> intersecciones;
				for (list<int>::const_iterator pp_i = (*e).indCajas.begin(); pp_i != (*e).indCajas.end(); ++pp_i) {
					PackedBox* pp = &empacados[*pp_i];
					if (!((*p).x1 + (*b).x1 >= (*pp).x2 || (*pp).x1 >= (*p).x2 + (*b).x1)) {
						if (!((*p).y1 + (*b).y1 >= (*pp).y2 || (*pp).y1 >= (*p).y2 + (*b).y1)) {

							// Se encuentran los putnos de intersección

							vector<int> misx({ (*p).x1 + (*b).x1, (*pp).x1, (*pp).x2, (*p).x2 + (*b).x1 });
							sort(misx.begin(), misx.end());
							vector<int> misy({ (*p).y1 + (*b).y1, (*pp).y1, (*pp).y2, (*p).y2 + (*b).y1 });
							sort(misy.begin(), misy.end());
							intersecciones.push_back(Point(misx[1], misy[1]));
							intersecciones.push_back(Point(misx[2], misy[1]));
							intersecciones.push_back(Point(misx[1], misy[2]));
							intersecciones.push_back(Point(misx[2], misy[2]));

							// Se crea el soporte

							(*b).soportesAbajo.back().push_back(SoporteAbajo((*pp).id, (misx[2] - misx[1]) * (misy[2] - misy[1]), (double)(misx[2] + misx[1]) / 2.0, (double)(misy[2] + misy[1]) / 2.0, (*(*pp).b).presion));
							(*b).soportesArriba.back().push_back(SoporteArriba(empacados.size() + (*p).id, (*b).soportesAbajo.back().size() - 1));
						}
					}
				}

				// Determinar polígono

				if (intersecciones.size() > 0) {
					(*b).poligonos.push_back(Poligono(intersecciones));
					if (!(*b).poligonos.back().puntoDentroPoligono((*p).centroMasax + (double)(*b).x1, (*p).centroMasay + (double)(*b).y1)) {
						if (!(*b).poligonos.back().puntoDentroPoligono((*p).cx + (double)(*b).x1, (*p).cy + (double)(*b).y1)) {
							return false;
						}
					}
				}
				else {
					return false;
				}
			}
		}

		// Se empacan las cajas inferiores del bloque

		int nCajas = (int)empacados.size();
		empacados.insert(empacados.end(), (*(*b).b).cajas.begin(), (*(*b).b).cajas.end());
		list<vector<SoporteAbajo>>::iterator s1 = (*b).soportesAbajo.begin();
		list<list<SoporteArriba>>::iterator s2 = (*b).soportesArriba.begin();
		for (vector<PackedBox>::iterator p = empacados.begin() + nCajas; p != empacados.end(); ++p) {
			if ((*p).z1 == 0) {
				//(*p).id += nCajas;
				(*p).soportes = *s1;
				vector<SoporteAbajo>::iterator s11 = (*s1).begin();
				for (list<SoporteArriba>::iterator s22 = (*s2).begin(); s22 != (*s2).end(); ++s22, ++s11) {
					empacados[(*s11).indPB].soportesArriba.push_back(*s22);
				}
				++s1;
				++s2;
				(*p).centroMasax += (*b).x1;
				(*p).cx += (*b).x1;
				(*p).x1 += (*b).x1;
				(*p).x2 += (*b).x1;
				(*p).centroMasay += (*b).y1;
				(*p).cy += (*b).y1;
				(*p).y1 += (*b).y1;
				(*p).y2 += (*b).y1;
				(*p).z1 += (*b).z1;
				(*p).z2 += (*b).z1;
				(*p).DistribuirPesoEntreSoportes();
				if (!(*p).soportada) {
					empacados.resize(nCajas);
					return false;
				}
			}
		}

		// Se actualiza el árbol

		list<int>::iterator it = arbol.begin();
		PackedBox* pit = nullptr;
		int actualz = 0;
		if (it != arbol.end()) {
			pit = &empacados[*it];
			actualz = (*pit).z2;
		}
		while (it != arbol.end()) {
			if ((*pit).z2 == actualz) {
				(*pit).ActualizarCentroMasaYPeso(&empacados);
				if (!(*pit).soportada) {
					empacados.resize(nCajas);
					return false;
				}
				++it;
				if (it != arbol.end()) {
					pit = &empacados[*it];
				}
			}
			else {

				// Distribuir los pesos

				for (list<int>::reverse_iterator it2 = list<int>::reverse_iterator(it); it2 != arbol.rend(); ++it2) {
					PackedBox* pit2 = &empacados[*it2];
					if ((*pit2).z2 != actualz) break;
					(*pit2).DistribuirPesoEntreSoportes();
					if (!(*pit2).soportada) {
						empacados.resize(nCajas);
						return false;
					}
				}

				// Actualización

				actualz = (*pit).z2;
			}
		}

		// Fin

		empacados.resize(nCajas);
		return true;
	}
	void GenerarOpcionesBasicas(const MaximalSpace* e) {
		if (r_multidrop > 0) {
			for (list<Bloque*>::const_iterator b = bloques[clienteActual].begin(); b != bloques[clienteActual].end(); ++b) {
				if ((**b).dx <= (*e).dx && (**b).dy <= (*e).dy && (**b).dz <= (*e).dz) {
					bool agregar = true;
					for (vector<PackedBox>::const_iterator p = (**b).cajas.begin(); p != (**b).cajas.end(); ++p) {
						if ((*p).z1 == 0) {
							if ((*e).x1 + (double)((*p).x1 + (*p).x2) / 2.0 > (*e).maxSopx) {
								agregar = false;
								break;
							}
						}	
					}
					if (agregar) opciones.push_back(BloqueEmpacado(*b, e));
				}
			}
		}
		else {
			for (list<Bloque*>::const_iterator b = bloques[clienteActual].begin(); b != bloques[clienteActual].end(); ++b) {
				if ((**b).dx <= (*e).dx && (**b).dy <= (*e).dy && (**b).dz <= (*e).dz) {
					opciones.push_back(BloqueEmpacado(*b, e));
				}
			}
		}
	}

	void SeleccionarBloque_BestFit_Deterministico(vector<BloqueEmpacado>::iterator& resp, const MaximalSpace* e) {
		sort(opciones.begin(), opciones.end(), Orden_BestFit_MaxVol);
		while (opciones.size() > 0) {
			resp = opciones.begin();

			// Se evalúa soporte si es el caso

			if (r_maxPresionItems) {
				DeterminarArboles(e);
				if (BloqueSoportadoPresion(&*resp, e)) {
					// Reestablecer cajas top

					for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
						empacados[(*p).id] = *p;
					}
					break;
				}
				else {
					opciones.erase(resp);
				}

				// Reestablecer cajas top

				for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
					empacados[(*p).id] = *p;
				}
			}
			else {
				if ((*e).z1 == 0) {
					break;
				}
				else {
					DeterminarArboles(e);
					if (BloqueSoportado(&*resp, e)) {
						// Reestablecer cajas top

						for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
							empacados[(*p).id] = *p;
						}
						break;
					}
					else {
						opciones.erase(resp);
					}

					// Reestablecer cajas top

					for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
						empacados[(*p).id] = *p;
					}
				}
			}
		}
		if (opciones.size() == 0) {
			resp = opciones.end();
		}
	}
	void SeleccionarBloque_BestFit(vector<BloqueEmpacado>::iterator& resp, const MaximalSpace* e) {
		sort(opciones.begin(), opciones.end(), Orden_BestFit_MaxVol);
		while (opciones.size() > 0) {
			uniform_int_distribution<int> distri(0, miBusquedaBinariaBF(opciones.front().bf1 + (double)(opciones.back().bf1 - opciones.front().bf1) * alpha + error));
			resp = opciones.begin() + distri(generator);

			// Se evalúa soporte si es el caso

			if (r_maxPresionItems) {
				DeterminarArboles(e);
				if (BloqueSoportadoPresion(&*resp, e)) {

					// Reestablecer cajas top

					for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
						empacados[(*p).id] = *p;
					}
					break;
				}
				else {
					opciones.erase(resp);
				}

				// Reestablecer cajas top

				for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
					empacados[(*p).id] = *p;
				}
			}
			else {
				if ((*e).z1 == 0) {
					break;
				}
				else {
					DeterminarArboles(e);
					if (BloqueSoportado(&*resp, e)) {
						// Reestablecer cajas top

						for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
							empacados[(*p).id] = *p;
						}
						break;
					}
					else {
						opciones.erase(resp);
					}

					// Reestablecer cajas top

					for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
						empacados[(*p).id] = *p;
					}
				}
			}
		}
		if (opciones.size() == 0) {
			resp = opciones.end();
		}
	}
	void SeleccionarBloque_MaxVol(vector<BloqueEmpacado>::iterator& resp, const MaximalSpace* e) {
		sort(opciones.begin(), opciones.end(), Orden_MaxVol_BestFit);
		while (opciones.size() > 0) {
			uniform_int_distribution<int> distri(0, miBusquedaBinariaVol((*opciones.front().b).vol + (double)((*opciones.back().b).vol - (*opciones.front().b).vol) * alpha + error));
			resp = opciones.begin() + distri(generator);

			// Se evalúa soporte si es el caso

			if (r_maxPresionItems) {
				DeterminarArboles(e);
				if (BloqueSoportadoPresion(&*resp, e)) {
					// Reestablecer cajas top

					for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
						empacados[(*p).id] = *p;
					}
					break;
				}
				else {
					opciones.erase(resp);
				}

				// Reestablecer cajas top

				for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
					empacados[(*p).id] = *p;
				}
			}
			else {
				if ((*e).z1 == 0) {
					break;
				}
				else {
					DeterminarArboles(e);
					if (BloqueSoportado(&*resp, e)) {
						// Reestablecer cajas top

						for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
							empacados[(*p).id] = *p;
						}
						break;
					}
					else {
						opciones.erase(resp);
					}

					// Reestablecer cajas top

					for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
						empacados[(*p).id] = *p;
					}
				}
			}
		}
		if (opciones.size() == 0) {
			resp = opciones.end();
		}
	}
	void SeleccionarBloque_FAST(vector<BloqueEmpacado>::iterator& resp, const MaximalSpace* e) {
		opciones.clear();

		// A cada bloque relacionarlo con la métrica best fit
		
		list<list<Bloque*>::iterator> bb;
		for (list<Bloque*>::iterator b = bloques[clienteActual].begin(); b != bloques[clienteActual].end(); ++b) {
			if ((**b).dx <= (*e).dx && (**b).dy <= (*e).dy && (**b).dz <= (*e).dz) {
				(**b).DeterminarEspacioMaximal_BS((*e).dx, (*e).dy, (*e).dz);
				bb.push_back(b);
			}
		}
		if (bb.size() != 0) {

			// Ordenarlo por best fit

			bb.sort([](list<Bloque*>::iterator it1, list<Bloque*>::iterator it2)->bool {
				if ((**it1).bf1 == (**it2).bf1) {
					if ((**it1).bf2 == (**it2).bf2) {
						return (**it1).bf3 < (**it2).bf3;
					}
					else return (**it1).bf2 < (**it2).bf2;
				}
				else return (**it1).bf1 < (**it2).bf1;
				});

			// Escoger la primera opción válida

			for (list<list<Bloque*>::iterator>::iterator b = bb.begin(); b != bb.end(); ++b) {
				BloqueEmpacado be(**b, e);
				if ((*e).z1 == 0) {
					opciones.push_back(be);
					resp = opciones.begin();
					return;
				}
				else {

					vector<PackedBox> nuevasCajas;
					nuevasCajas.insert(nuevasCajas.end(), (*be.b).cajas.begin(), (*be.b).cajas.end());

					// Ver arbol superior de las nuevas cajas

					be.soportesAbajo = list<vector<SoporteAbajo>>(0);
					be.soportesArriba = list<list<SoporteArriba>>(0);
					bool sirve = true;
					vector<vector<PackedBox>::iterator> cajasSuperioresBloqueSuperior;
					cajasSuperioresBloqueSuperior.reserve(nuevasCajas.size());
					for (vector<PackedBox>::iterator p = nuevasCajas.begin(); p != nuevasCajas.end(); ++p) {
						if ((*p).z1 == 0) {
							be.soportesAbajo.push_back(vector<SoporteAbajo>(0));
							be.soportesArriba.push_back(list<SoporteArriba>(0));
							vector<Point> intersecciones;
							for (list<int>::const_iterator pp_i = (*e).indCajas.begin(); pp_i != (*e).indCajas.end(); ++pp_i) {
								PackedBox* pp = &empacados[*pp_i];
								if (!((*p).x1 + be.x1 >= (*pp).x2 || (*pp).x1 >= (*p).x2 + be.x1)) {
									if (!((*p).y1 + be.y1 >= (*pp).y2 || (*pp).y1 >= (*p).y2 + be.y1)) {

										// Se encuentran los puntos de intersección

										vector<int> misx({ (*p).x1 + be.x1, (*pp).x1, (*pp).x2, (*p).x2 + be.x1 });
										sort(misx.begin(), misx.end());
										vector<int> misy({ (*p).y1 + be.y1, (*pp).y1, (*pp).y2, (*p).y2 + be.y1 });
										sort(misy.begin(), misy.end());
										intersecciones.push_back(Point(misx[1], misy[1]));
										intersecciones.push_back(Point(misx[2], misy[1]));
										intersecciones.push_back(Point(misx[1], misy[2]));
										intersecciones.push_back(Point(misx[2], misy[2]));

										// Se crea el soporte

										be.soportesAbajo.back().push_back(SoporteAbajo((*pp).id, (misx[2] - misx[1]) * (misy[2] - misy[1]), (double)(misx[2] + misx[1]) / 2.0, (double)(misy[2] + misy[1]) / 2.0, (*(*pp).b).presion));
										be.soportesArriba.back().push_back(SoporteArriba(empacados.size() + (*p).id, be.soportesAbajo.back().size() - 1));

										// Soporte Fast

										(*p).maxSoporteFast = min((*p).maxSoporteFast, (*pp).maxSoporteFast - (*(*p).b).soporteNecesario);
									}
								}
							}

							// Determinar si es full support o no

							sirve = false;
							if (intersecciones.size() > 0) {

								// x1, y1

								bool esta = false;
								for (vector<Point>::iterator it = intersecciones.begin(); it != intersecciones.end(); ++it) {
									if ((*it).x == ((*p).x1 + be.x1) && (*it).y == ((*p).y1 + be.y1)) {
										esta = true;
										break;
									}
								}
								if (!esta) break;

								// x2, y1

								esta = false;
								for (vector<Point>::iterator it = intersecciones.begin(); it != intersecciones.end(); ++it) {
									if ((*it).x == ((*p).x2 + be.x1) && (*it).y == ((*p).y1 + be.y1)) {
										esta = true;
										break;
									}
								}
								if (!esta) break;

								// x2, y2

								esta = false;
								for (vector<Point>::iterator it = intersecciones.begin(); it != intersecciones.end(); ++it) {
									if ((*it).x == ((*p).x2 + be.x1) && (*it).y == ((*p).y2 + be.y1)) {
										esta = true;
										break;
									}
								}
								if (!esta) break;

								// x1, y2

								esta = false;
								for (vector<Point>::iterator it = intersecciones.begin(); it != intersecciones.end(); ++it) {
									if ((*it).x == ((*p).x1 + be.x1) && (*it).y == ((*p).y2 + be.y1)) {
										esta = true;
										break;
									}
								}
								if (!esta) break;

								sirve = true;
							}
							if (!sirve) {
								break;
							}
						}
						else {
							cajasSuperioresBloqueSuperior.push_back(p);
						}
					}
					if (sirve) {

						// Actualizar árbol superior con soporte fast

						sort(cajasSuperioresBloqueSuperior.begin(), cajasSuperioresBloqueSuperior.end(), [](vector<PackedBox>::iterator p1, vector<PackedBox>::iterator p2)->bool {return (*p1).z1 < (*p2).z1; });
						for (vector<vector<PackedBox>::iterator>::iterator it = cajasSuperioresBloqueSuperior.begin(); it != cajasSuperioresBloqueSuperior.end() && sirve; ++it) {
							(**it).maxSoporteFast = (*(**it).b).presion;
							for (vector<SoporteAbajo>::iterator s = (**it).soportes.begin(); s != (**it).soportes.end(); ++s) {
								(**it).maxSoporteFast = min((**it).maxSoporteFast, nuevasCajas[(*s).indPB].maxSoporteFast - (*(**it).b).soporteNecesario);
								if ((**it).maxSoporteFast < -error) {
									sirve = false;
									break;
								}
							}
						}
						if (sirve) {
							be.cajasEmpacadas = nuevasCajas;
							opciones.push_back(be);
							resp = opciones.begin();
							return;
						}
					}
				}
			}
		}
		resp = opciones.end();
	}

	// Empacar

	void Empacar0Fast(BloqueEmpacado* b) {

		// Utilización

		utilizacion += (*(*b).b).volContenedor;

		// Actualizar Cantidades de los Tipos de Cajas

		hayCajasPorEmpacar = false;
		vector<int>::const_iterator c1 = (*(*b).b).cantidades.begin();
		for (vector<int>::iterator c2 = cantidadesBoxType.begin(); c2 < cantidadesBoxType.end(); ++c1, ++c2) {
			(*c2) -= (*c1);
			if ((*c2) > 0) hayCajasPorEmpacar = true;
			else if ((*c2) < 0) {
				cout << "ErrorCantidades" << endl;
			}
		}
		c1 = (*(*b).b).cantidades.begin();
		int i = 0;
		for (vector<int>::iterator c2 = cantidadesBoxType.begin(); c2 < cantidadesBoxType.end(); ++c1, ++c2, ++i) {
			if ((*c2) == 0 && (*c1) > 0) {
				BoxType* bt = &BoxTypes0[i];
				if ((*bt).minDx == minDx || (*bt).minDy == minDy || (*bt).minDz == minDz) {
					ActualizarMinD();
					break;
				}
			}
		}

		// Se empacan las cajas del bloque

		int nCajas = (int)empacados.size();
		int idBloque = 0;
		if (nCajas > 0) {
			idBloque = empacados.back().idBloque + 1;
		}
		if ((*b).z1 == 0) {
			empacados.insert(empacados.end(), (*(*b).b).cajas.begin(), (*(*b).b).cajas.end());
		}
		else {
			empacados.insert(empacados.end(), (*b).cajasEmpacadas.begin(), (*b).cajasEmpacadas.end());
		}
		list<vector<SoporteAbajo>>::iterator s1 = (*b).soportesAbajo.begin();
		list<list<SoporteArriba>>::iterator s2 = (*b).soportesArriba.begin();
		for (vector<PackedBox>::iterator p = empacados.begin() + nCajas; p != empacados.end(); ++p) {
			(*p).idBloque = idBloque;
			(*p).id += nCajas;
			(*p).centroMasax += (*b).x1;
			(*p).cx += (*b).x1;
			(*p).x1 += (*b).x1;
			(*p).x2 += (*b).x1;
			(*p).centroMasay += (*b).y1;
			(*p).cy += (*b).y1;
			(*p).y1 += (*b).y1;
			(*p).y2 += (*b).y1;
			(*p).z1 += (*b).z1;
			(*p).z2 += (*b).z1;
			for (list<SoporteArriba>::iterator s = (*p).soportesArriba.begin(); s != (*p).soportesArriba.end(); ++s) {
				(*s).indPB += nCajas;
			}
			if ((*p).z1 == (*b).z1 && (*b).z1 > 0) {
				(*p).soportes = *s1;
				vector<SoporteAbajo>::iterator s11 = (*s1).begin();
				for (list<SoporteArriba>::iterator s22 = (*s2).begin(); s22 != (*s2).end(); ++s22, ++s11) {
					empacados[(*s11).indPB].soportesArriba.push_back(*s22);
				}
				++s1;
				++s2;
			}
			else {
				for (vector<SoporteAbajo>::iterator s = (*p).soportes.begin(); s != (*p).soportes.end(); ++s) {
					(*s).indPB += nCajas;
				}
			}
		}
	}
	void EmpacarYActualizarArbol(BloqueEmpacado* b) {
		
		// Utilización

		utilizacion += (*(*b).b).volContenedor;

		// Actualizar Cantidades de los Tipos de Cajas

		hayCajasPorEmpacar = false;
		vector<int>::const_iterator c1 = (*(*b).b).cantidades.begin();
		for (vector<int>::iterator c2 = cantidadesBoxType.begin(); c2 < cantidadesBoxType.end(); ++c1, ++c2) {
			(*c2) -= (*c1);
			if ((*c2) > 0) hayCajasPorEmpacar = true;
			else if ((*c2) < 0) {
				cout << "ErrorCantidades" << endl;
			}
		}
		c1 = (*(*b).b).cantidades.begin();
		int i = 0;
		for (vector<int>::iterator c2 = cantidadesBoxType.begin(); c2 < cantidadesBoxType.end(); ++c1, ++c2, ++i) {
			if ((*c2) == 0 && (*c1) > 0) {
				BoxType* bt = &BoxTypes0[i];
				if ((*bt).minDx == minDx || (*bt).minDy == minDy || (*bt).minDz == minDz) {
					ActualizarMinD();
					break;
				}
			}
		}

		// Se empacan las cajas del bloque

		int nCajas = (int)empacados.size();
		int idBloque = 0;
		if (nCajas > 0) {
			idBloque = empacados.back().idBloque + 1;
		}
		empacados.insert(empacados.end(), (*(*b).b).cajas.begin(), (*(*b).b).cajas.end());
		list<vector<SoporteAbajo>>::iterator s1 = (*b).soportesAbajo.begin();
		list<list<SoporteArriba>>::iterator s2 = (*b).soportesArriba.begin();
		list<Poligono>::iterator pol = (*b).poligonos.begin();
		vector<vector<PackedBox>::iterator> cajasSuperioresBloqueSuperior;
		cajasSuperioresBloqueSuperior.reserve(empacados.size() - nCajas);
		for (vector<PackedBox>::iterator p = empacados.begin() + nCajas; p != empacados.end(); ++p) {
			(*p).idBloque = idBloque;
			(*p).id += nCajas;
			(*p).centroMasax += (*b).x1;
			(*p).cx += (*b).x1;
			(*p).x1 += (*b).x1;
			(*p).x2 += (*b).x1;
			(*p).centroMasay += (*b).y1;
			(*p).cy += (*b).y1;
			(*p).y1 += (*b).y1;
			(*p).y2 += (*b).y1;
			(*p).z1 += (*b).z1;
			(*p).z2 += (*b).z1;
			for (list<SoporteArriba>::iterator s = (*p).soportesArriba.begin(); s != (*p).soportesArriba.end(); ++s) {
				(*s).indPB += nCajas;
			}
			if ((*p).z1 == (*b).z1 && (*b).z1 > 0) {
				(*p).soportes = *s1;
				(*p).poligono = *pol;
				vector<SoporteAbajo>::iterator s11 = (*s1).begin();
				for (list<SoporteArriba>::iterator s22 = (*s2).begin(); s22 != (*s2).end(); ++s22, ++s11) {
					empacados[(*s11).indPB].soportesArriba.push_back(*s22);
				}
				++s1;
				++s2;
				++pol;
				(*p).DistribuirPesoEntreSoportes();
			}
			else {
				for (vector<SoporteAbajo>::iterator s = (*p).soportes.begin(); s != (*p).soportes.end(); ++s) {
					(*s).indPB += nCajas;
					(*s).centrox += (*b).x1;
					(*s).centroy += (*b).y1;
				}
				(*p).poligono.ActualizarPoligono((*b).x1, (*b).y1);
			}

			// Adicionar iteradores de cajas nuevas

			cajasSuperioresBloqueSuperior.push_back(p);
		}

		// Se actualiza el árbol

		if (arbol.size() > 0) {
			list<int>::iterator it = arbol.begin();
			PackedBox* pit = nullptr;
			int actualz = 0;
			if (it != arbol.end()) {
				pit = &empacados[*it];
				actualz = (*pit).z2;
			}
			while (it != arbol.end()) {
				if ((*pit).z2 == actualz) {
					(*pit).ActualizarCentroMasaYPeso(&empacados);
					++it;
					if (it != arbol.end()) {
						pit = &empacados[*it];
					}
				}
				else {

					// Distribuir los pesos

					for (list<int>::reverse_iterator it2 = list<int>::reverse_iterator(it); it2 != arbol.rend(); ++it2) {
						PackedBox* pit2 = &empacados[*it2];
						if ((*pit2).z2 != actualz) break;
						(*pit2).DistribuirPesoEntreSoportes();
					}

					// Actualización

					actualz = (*pit).z2;
					it = prev(it);
				}
			}
		}

		// Soporte Rápido

		if ((*b).z1 > 0) {
			sort(cajasSuperioresBloqueSuperior.begin(), cajasSuperioresBloqueSuperior.end(), [](vector<PackedBox>::iterator p1, vector<PackedBox>::iterator p2)->bool {return (*p1).z1 < (*p2).z1; });
			for (vector<vector<PackedBox>::iterator>::iterator it = cajasSuperioresBloqueSuperior.begin(); it != cajasSuperioresBloqueSuperior.end(); ++it) {
				(**it).maxSoporteFast = (*(**it).b).presion;
				for (vector<SoporteAbajo>::iterator s = (**it).soportes.begin(); s != (**it).soportes.end(); ++s) {
					(**it).maxSoporteFast = min((**it).maxSoporteFast, empacados[(*s).indPB].maxSoporteFast - (*(**it).b).soporteNecesario);
				}
			}
		}
	}
	void EliminarBloques() {
		list<Bloque*>::iterator it = bloques[clienteActual].begin();
		while (it != bloques[clienteActual].end()) {
			bool eliminar = false;
			vector<int>::const_iterator c1 = (**it).cantidades.begin();
			for (vector<int>::const_iterator c2 = cantidadesBoxType.begin(); c2 < cantidadesBoxType.end(); ++c2, ++c1) {
				if ((*c1) > (*c2)) {
					eliminar = true;
					break;
				}
			}
			if (eliminar) {
				it = bloques[clienteActual].erase(it);
			}
			else {
				++it;
			}
		}
	}
	bool DeterminarCliente() {
		bool cambiarCliente = true;
		vector<int>::const_iterator c_it = cantidadesBoxType.begin();
		for (vector<BoxType>::const_iterator bt_it = BoxTypes0.begin(); bt_it < BoxTypes0.end(); ++bt_it, ++c_it) {
			if ((*bt_it).cliente < clienteActual) {
				continue;
			}
			else if ((*bt_it).cliente == clienteActual) {
				if (*c_it > 0) {
					cambiarCliente = false;
					break;
				}
			}
			else if ((*bt_it).cliente > clienteActual) {
				break;
			}
		}
		if (cambiarCliente) {
			++clienteActual;
			
		}
		return cambiarCliente;
	}

public:

	void Empacar(BloqueEmpacado* b) {
		EmpacarYActualizarArbol(b);
		if (!hayCajasPorEmpacar) return;
		JuntarEspacios(ActualizarEspacios(b), espacios);
		EliminarEspaciosMinD(espacios);
		ActualizarIndicesCajasDeEspacios(espacios);
		JuntarEspacios(ActualizarEspaciosFast(b), espaciosFast);
		EliminarEspaciosMinD(espaciosFast);
		ActualizarIndicesCajasDeEspacios(espaciosFast);
		EliminarBloques();
	}
	void Empacar_Multidrop(BloqueEmpacado* b) {
		EmpacarYActualizarArbol(b);
		if (!hayCajasPorEmpacar) return;
		JuntarEspacios(ActualizarEspacios(b), espacios);
		JuntarEspacios(ActualizarEspaciosFast(b), espaciosFast);
		if (DeterminarCliente()) {
			if (r_multidrop == 1) {
				ActualizarEspaciosVisibilidad();
				ActualizarEspaciosVisibilidadFast();
			}
			else if (r_multidrop == 2) {
				ActualizarEspaciosCapas(espacios);
				ActualizarEspaciosCapas(espaciosFast);
			}
			JuntarEspacios(espacios.rend(), espacios);
			JuntarEspacios(espaciosFast.rend(), espaciosFast);
		}
		EliminarEspaciosMinD(espacios);
		EliminarEspaciosMinD(espaciosFast);
		ActualizarIndicesCajasDeEspacios(espacios);
		ActualizarIndicesCajasDeEspacios(espaciosFast);
		EliminarBloques();
		
	}
	void EmpacarFast(BloqueEmpacado* b) {
		Empacar0Fast(b);
		if (!hayCajasPorEmpacar) return;
		JuntarEspacios(ActualizarEspaciosFast(b), espaciosFast);
		EliminarEspaciosMinD(espaciosFast);
		ActualizarIndicesCajasDeEspacios(espaciosFast);
		EliminarBloques();
	}
	void Empacar_MultidropFast(BloqueEmpacado* b) {
		Empacar0Fast(b);
		if (!hayCajasPorEmpacar) return;
		JuntarEspacios(ActualizarEspaciosFast(b), espaciosFast);
		if (DeterminarCliente()) {
			if (r_multidrop == 1) {
				ActualizarEspaciosVisibilidadFast();
			}
			else if (r_multidrop == 2) {
				ActualizarEspaciosCapas(espaciosFast);
			}
			JuntarEspacios(espaciosFast.rend(), espaciosFast);
		}
		EliminarEspaciosMinD(espaciosFast);
		ActualizarIndicesCajasDeEspacios(espaciosFast);
		EliminarBloques();
	}

	// Constructivo

	void Constructivo() {
		while ((int)espacios.size() > 0 && hayCajasPorEmpacar) {

			// Se selecciona el espacio maximal

			list<MaximalSpace>::iterator mejorEspacio = espacios.begin();
			double myRand1 = dist01(generator);
			if (myRand1 < 0.5) SeleccionarEspacio_Fondo_Altura(mejorEspacio, vector<list<MaximalSpace>::iterator>(0), espacios);
			else SeleccionarEspacio_Altura_Fondo(mejorEspacio, vector<list<MaximalSpace>::iterator>(0));

			// Se selecciona la pieza a empacar

			opciones.clear();
			opciones.reserve(bloques[clienteActual].size());
			GenerarOpcionesBasicas(&*mejorEspacio);
			if (opciones.size() > 0) {
				vector<BloqueEmpacado>::iterator mejorBloque = opciones.end();
				if (dist01(generator) < 0.5) SeleccionarBloque_BestFit(mejorBloque, &*mejorEspacio);
				else SeleccionarBloque_MaxVol(mejorBloque, &*mejorEspacio);
				if (mejorBloque == opciones.end()) {
					espacios.erase(mejorEspacio);
				}
				else {
					Empacar(&*mejorBloque);
				}
			}
			else espacios.erase(mejorEspacio);
		}
	}
	void Constructivo_Multidrop() {
		while ((int)espacios.size() > 0 && hayCajasPorEmpacar) { // Falta poner restricción si hay algún tipo con caja

			// Se selecciona el espacio maximal

			double myRand1 = dist01(generator);
			if (myRand1 < 0.5) espacios.sort(Orden_Fondo_Altura);
			else espacios.sort(Orden_Altura_Fondo);
			double myRand2 = (double)dist01(generator);

			// Se recorren los espacios en orden

			if (myRand2 < 0.5f) {
				bool terminar = true;
				for (list<MaximalSpace>::iterator e = espacios.begin(); e != espacios.end(); ++e) {
					opciones.clear();
					opciones.reserve(bloques[clienteActual].size());
					GenerarOpcionesBasicas(&*e);
					if (opciones.size() > 0) {
						vector<BloqueEmpacado>::iterator mejorBloque = opciones.begin();
						SeleccionarBloque_BestFit(mejorBloque, &*e);
						if (mejorBloque != opciones.end()) {
							Empacar_Multidrop(&*mejorBloque);
							terminar = false;
							break;
						}
					}
				}
				if (terminar) break;
			}
			else {
				bool terminar = true;
				for (list<MaximalSpace>::iterator e = espacios.begin(); e != espacios.end(); ++e) {
					opciones.clear();
					opciones.reserve(bloques[clienteActual].size());
					GenerarOpcionesBasicas(&*e);
					if (opciones.size() > 0) {
						vector<BloqueEmpacado>::iterator mejorBloque = opciones.begin();
						SeleccionarBloque_MaxVol(mejorBloque, &*e);
						if (mejorBloque != opciones.end()) {
							Empacar_Multidrop(&*mejorBloque);
							terminar = false;
							break;
						}
					}
				}
				if (terminar) break;
			}
		}
	}

	// Constructivo Deterministico

	void Constructivo_Deterministico() {
		while ((int)espacios.size() > 0 && hayCajasPorEmpacar) {

			// Se selecciona el espacio maximal

			list<MaximalSpace>::iterator mejorEspacio = espacios.begin();
			double myRand1 = dist01(generator);
			if (myRand1 < 0.33) SeleccionarEspacio_Fondo_Altura(mejorEspacio, vector<list<MaximalSpace>::iterator>(0), espacios);
			else if (myRand1 < 0.67) SeleccionarEspacio_Fondo_Bajo(mejorEspacio, vector<list<MaximalSpace>::iterator>(0));
			else SeleccionarEspacio_Altura_Fondo(mejorEspacio, vector<list<MaximalSpace>::iterator>(0));

			// Se selecciona la pieza a empacar

			opciones.clear();
			opciones.reserve(bloques[clienteActual].size());
			GenerarOpcionesBasicas(&*mejorEspacio);
			if (opciones.size() > 0) {
				vector<BloqueEmpacado>::iterator mejorBloque = opciones.begin();
				SeleccionarBloque_BestFit_Deterministico(mejorBloque, &*mejorEspacio);
				if (mejorBloque == opciones.end()) {
					espacios.erase(mejorEspacio);
				}
				else {
					Empacar(&*mejorBloque);
				}
			}
			else espacios.erase(mejorEspacio);
		}
	}
	void Constructivo_Multidrop_Deterministico() {
		while ((int)espacios.size() > 0 && hayCajasPorEmpacar) { // Falta poner restricción si hay algún tipo con caja

			// Se selecciona el espacio maximal

			double myRand1 = dist01(generator);
			if (myRand1 < 0.33) espacios.sort(Orden_Fondo_Altura);
			else if (myRand1 < 0.67) espacios.sort(Orden_Fondo_Bajo);
			else espacios.sort(Orden_Altura_Fondo);

			// Se recorren los espacios maximales en orden

			bool terminar = true;
			for (list<MaximalSpace>::iterator e = espacios.begin(); e != espacios.end(); ++e) {
				opciones.clear();
				opciones.reserve(bloques[clienteActual].size());
				GenerarOpcionesBasicas(&*e);
				if (opciones.size() > 0) {
					vector<BloqueEmpacado>::iterator mejorBloque = opciones.begin();
					SeleccionarBloque_BestFit_Deterministico(mejorBloque, &*e);
					if (mejorBloque != opciones.end()) {
						Empacar_Multidrop(&*mejorBloque);
						terminar = false;
						break;
					}
				}
			}
			if (terminar) break;
		}
	}

	// Constructivo FAST
	
	void ConstructivoFast() {
		while ((int)espaciosFast.size() > 0 && hayCajasPorEmpacar) {

			// Se selecciona el espacio maximal

			list<MaximalSpace>::iterator mejorEspacio = espaciosFast.begin();
			SeleccionarEspacio_Fondo_Altura(mejorEspacio, vector<list<MaximalSpace>::iterator>(0), espaciosFast);
			
			// Se selecciona la pieza a empacar

			vector<BloqueEmpacado>::iterator mejorBloque = opciones.end();
			SeleccionarBloque_FAST(mejorBloque, &*mejorEspacio);
			if (mejorBloque != opciones.end()) {
				EmpacarFast(&*mejorBloque);
			}
			else {
				espaciosFast.erase(mejorEspacio);
			}
		}
	}
	void Constructivo_MultidropFast() {
		while ((int)espaciosFast.size() > 0 && hayCajasPorEmpacar) { // Falta poner restricción si hay algún tipo con caja
			
			// Se selecciona el espacio maximal

			espaciosFast.sort(Orden_Fondo_Altura);
			
			// Se recorren los espacios en orden

			bool terminar = true;
			for (list<MaximalSpace>::iterator e = espaciosFast.begin(); e != espaciosFast.end(); ++e) {
				vector<BloqueEmpacado>::iterator mejorBloque = opciones.end();
				SeleccionarBloque_FAST(mejorBloque, &*e);
				if (mejorBloque != opciones.end()) {
					Empacar_MultidropFast(&*mejorBloque);
					terminar = false;
					break;
				}
			}
			if (terminar) break;
		}
	}

	// Quitar piezas

	void RemoverPiezas(int const& hasta) {
		empacados.resize(hasta);

		// Actualizar soportes de las cajas empacadas (pueden haber soportes inexistentes)

		for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
			list<SoporteArriba>::iterator s = (*p).soportesArriba.begin();
			while (s != (*p).soportesArriba.end()) {
				if ((*s).indPB >= hasta) {
					s = (*p).soportesArriba.erase(s);
				}
				else {
					++s;
				}
			}
		}

		// Reestablecer cantidades de las piezas y los minD

		cantidadesBoxType = cantidadesBoxTypes0;
		for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
			--cantidadesBoxType[(*(*p).bt).id];
		}
		ActualizarMinD();

		// Utilización

		utilizacion = 0.0;
		vector<BoxType>::iterator bt_it0 = BoxTypes0.begin();
		for (vector<int>::iterator c_it = cantidadesBoxType.begin(); c_it != cantidadesBoxType.end(); ++c_it, ++bt_it0) {
			if (*c_it < (*bt_it0).cantidad) {
				utilizacion += (*bt_it0).volumenContenedor * (double)((*bt_it0).cantidad - *c_it);
			}
		}

		// Determinar el cliente actual

		if (empacados.size() == 0) {
			clienteActual = 0;
		}
		else {
			clienteActual = (*empacados.back().bt).cliente;
			bool cambiarCliente = true;
			vector<int>::const_iterator c_it = cantidadesBoxType.begin();
			for (vector<BoxType>::const_iterator bt_it = BoxTypes0.begin(); bt_it < BoxTypes0.end(); ++bt_it, ++c_it) {
				if ((*bt_it).cliente < clienteActual) {
					continue;
				}
				else if ((*bt_it).cliente == clienteActual) {
					if (*c_it > 0) {
						cambiarCliente = false;
						break;
					}
				}
				else if ((*bt_it).cliente > clienteActual) {
					break;
				}
			}
			if (cambiarCliente) {
				++clienteActual;
			}
		}

		// Determinar los espacios maximales

		if (r_multidrop == 1 && clienteActual > 0) {
			if (clienteActual != (*empacados.back().bt).cliente) {

				// Se hace un solo espacio maximal

				espacios = list<MaximalSpace>(1);
				espacios.back().indEsquina = 1;
				espaciosFast = list<MaximalSpace>(1);
				espaciosFast.back().indEsquina = 1;

				// Se actualizan los espacios maximales con todos los bloques

				vector<PackedBox>::const_iterator it2 = empacados.begin();
				int x1 = (*it2).x1;
				int y1 = (*it2).y1;
				int z1 = (*it2).z1;
				int x2 = (*it2).x2;
				int y2 = (*it2).y2;
				int z2 = (*it2).z2;
				int idBloque = (*it2).idBloque;
				for (it2 + 1; it2 != empacados.end(); ++it2) {
					if ((*it2).idBloque == idBloque) {
						if (x1 > (*it2).x1) x1 = (*it2).x1;
						if (y1 > (*it2).y1) y1 = (*it2).y1;
						if (z1 > (*it2).z1) z1 = (*it2).z1;
						if (x2 < (*it2).x2) x2 = (*it2).x2;
						if (y2 < (*it2).y2) y2 = (*it2).y2;
						if (z2 < (*it2).z2) z2 = (*it2).z2;
					}
					else {

						// Empacar bloque ficticio

						BloqueEmpacado ficticio(x1, y1, z1, x2, y2, z2);
						JuntarEspacios(ActualizarEspacios(&ficticio), espacios);
						JuntarEspacios(ActualizarEspaciosFast(&ficticio), espaciosFast);
						x1 = (*it2).x1;
						y1 = (*it2).y1;
						z1 = (*it2).z1;
						x2 = (*it2).x2;
						y2 = (*it2).y2;
						z2 = (*it2).z2;
						idBloque = (*it2).idBloque;
					}
				}
				BloqueEmpacado ficticio(x1, y1, z1, x2, y2, z2);
				JuntarEspacios(ActualizarEspacios(&ficticio), espacios);
				ActualizarEspaciosVisibilidad();
				JuntarEspacios(ActualizarEspaciosFast(&ficticio), espaciosFast);
				ActualizarEspaciosVisibilidadFast();
			}
			else {

				// Se hace un solo espacio maximal

				espacios = list<MaximalSpace>(1);
				espacios.back().indEsquina = 1;
				espaciosFast = list<MaximalSpace>(1);
				espaciosFast.back().indEsquina = 1;

				// Se actualizan los espacios maximales con todos los bloques de hasta el cliente anterior al cliente actual

				vector<PackedBox>::const_iterator it2 = empacados.begin();
				int x1 = (*it2).x1;
				int y1 = (*it2).y1;
				int z1 = (*it2).z1;
				int x2 = (*it2).x2;
				int y2 = (*it2).y2;
				int z2 = (*it2).z2;
				int idBloque = (*it2).idBloque;
				for (it2 + 1; it2 != empacados.end(); ++it2) {
					if ((*it2).idBloque == idBloque) {
						if (x1 > (*it2).x1) x1 = (*it2).x1;
						if (y1 > (*it2).y1) y1 = (*it2).y1;
						if (z1 > (*it2).z1) z1 = (*it2).z1;
						if (x2 < (*it2).x2) x2 = (*it2).x2;
						if (y2 < (*it2).y2) y2 = (*it2).y2;
						if (z2 < (*it2).z2) z2 = (*it2).z2;
					}
					else if ((*(*it2).bt).cliente >= clienteActual) {

						// Empacar bloque ficticio

						BloqueEmpacado ficticio(x1, y1, z1, x2, y2, z2);
						JuntarEspacios(ActualizarEspacios(&ficticio), espacios);
						JuntarEspacios(ActualizarEspaciosFast(&ficticio), espaciosFast);
						x1 = (*it2).x1;
						y1 = (*it2).y1;
						z1 = (*it2).z1;
						x2 = (*it2).x2;
						y2 = (*it2).y2;
						z2 = (*it2).z2;
						idBloque = (*it2).idBloque;
						break;
					}
					else {

						// Empacar bloque ficticio

						BloqueEmpacado ficticio(x1, y1, z1, x2, y2, z2);
						JuntarEspacios(ActualizarEspacios(&ficticio), espacios);
						JuntarEspacios(ActualizarEspaciosFast(&ficticio), espaciosFast);
						x1 = (*it2).x1;
						y1 = (*it2).y1;
						z1 = (*it2).z1;
						x2 = (*it2).x2;
						y2 = (*it2).y2;
						z2 = (*it2).z2;
					}
				}
				ActualizarEspaciosVisibilidad();
				ActualizarEspaciosVisibilidadFast();
				for (; it2 != empacados.end(); ++it2) {
					if ((*it2).idBloque == idBloque) {
						if (x1 > (*it2).x1) x1 = (*it2).x1;
						if (y1 > (*it2).y1) y1 = (*it2).y1;
						if (z1 > (*it2).z1) z1 = (*it2).z1;
						if (x2 < (*it2).x2) x2 = (*it2).x2;
						if (y2 < (*it2).y2) y2 = (*it2).y2;
						if (z2 < (*it2).z2) z2 = (*it2).z2;
					}
					else {

						// Empacar bloque ficticio

						BloqueEmpacado ficticio(x1, y1, z1, x2, y2, z2);
						JuntarEspacios(ActualizarEspacios(&ficticio), espacios);
						JuntarEspacios(ActualizarEspaciosFast(&ficticio), espaciosFast);
						x1 = (*it2).x1;
						y1 = (*it2).y1;
						z1 = (*it2).z1;
						x2 = (*it2).x2;
						y2 = (*it2).y2;
						z2 = (*it2).z2;
						idBloque = (*it2).idBloque;
					}
				}
				BloqueEmpacado ficticio(x1, y1, z1, x2, y2, z2);
				JuntarEspacios(ActualizarEspacios(&ficticio), espacios);
				JuntarEspacios(ActualizarEspaciosFast(&ficticio), espaciosFast);
			}
		}
		else if (r_multidrop == 2 && clienteActual > 0) {
			if (clienteActual != (*empacados.back().bt).cliente) {
				ActualizarEspaciosCapas(espacios);
				ActualizarEspaciosCapas(espaciosFast);
			}
			else {

				// Determinar el x

				int x0 = empacados.back().x1;
				vector<PackedBox>::const_reverse_iterator it = empacados.rbegin();
				for (; it != empacados.rend(); ++it) {
					if (clienteActual != (*(*it).bt).cliente) {
						break;
					}
					else if ((*it).x1 < x0) {
						x0 = (*it).x1;
					}
				}

				// Se hace un solo espacio maximal

				espacios = list<MaximalSpace>(1, MaximalSpace(x0, 0, 0, ContenedorDimx, ContenedorDimy, ContenedorDimz));
				espacios.back().indEsquina = 1;
				espaciosFast = list<MaximalSpace>(1, MaximalSpace(x0, 0, 0, ContenedorDimx, ContenedorDimy, ContenedorDimz));
				espaciosFast.back().indEsquina = 1;

				// Se actualizan los espacios maximales con los bloques del último cliente

				vector<PackedBox>::const_iterator it2 = it.base();
				int x1 = (*it2).x1;
				int y1 = (*it2).y1;
				int z1 = (*it2).z1;
				int x2 = (*it2).x2;
				int y2 = (*it2).y2;
				int z2 = (*it2).z2;
				int idBloque = (*it2).idBloque;
				for (it2 + 1; it2 != empacados.end(); ++it2) {
					if ((*it2).idBloque == idBloque) {
						if (x1 > (*it2).x1) x1 = (*it2).x1;
						if (y1 > (*it2).y1) y1 = (*it2).y1;
						if (z1 > (*it2).z1) z1 = (*it2).z1;
						if (x2 < (*it2).x2) x2 = (*it2).x2;
						if (y2 < (*it2).y2) y2 = (*it2).y2;
						if (z2 < (*it2).z2) z2 = (*it2).z2;
					}
					else {

						// Empacar bloque ficticio

						BloqueEmpacado ficticio(x1, y1, z1, x2, y2, z2);
						JuntarEspacios(ActualizarEspacios(&ficticio), espacios);
						JuntarEspacios(ActualizarEspaciosFast(&ficticio), espaciosFast);
						x1 = (*it2).x1;
						y1 = (*it2).y1;
						z1 = (*it2).z1;
						x2 = (*it2).x2;
						y2 = (*it2).y2;
						z2 = (*it2).z2;
						idBloque = (*it2).idBloque;
					}
				}
				BloqueEmpacado ficticio(x1, y1, z1, x2, y2, z2);
				JuntarEspacios(ActualizarEspacios(&ficticio), espacios);
				JuntarEspacios(ActualizarEspaciosFast(&ficticio), espaciosFast);
			}
		}
		else {

			// Se hace un solo espacio maximal

			espacios = list<MaximalSpace>(1);
			espacios.back().indEsquina = 1;
			espaciosFast = list<MaximalSpace>(1);
			espaciosFast.back().indEsquina = 1;

			// Se actualizan los espacios maximales con todos los bloques

			if (empacados.size() > 0) {
				vector<PackedBox>::const_iterator it2 = empacados.begin();
				int x1 = (*it2).x1;
				int y1 = (*it2).y1;
				int z1 = (*it2).z1;
				int x2 = (*it2).x2;
				int y2 = (*it2).y2;
				int z2 = (*it2).z2;
				int idBloque = (*it2).idBloque;
				for (it2 + 1; it2 != empacados.end(); ++it2) {
					if ((*it2).idBloque == idBloque) {
						if (x1 > (*it2).x1) x1 = (*it2).x1;
						if (y1 > (*it2).y1) y1 = (*it2).y1;
						if (z1 > (*it2).z1) z1 = (*it2).z1;
						if (x2 < (*it2).x2) x2 = (*it2).x2;
						if (y2 < (*it2).y2) y2 = (*it2).y2;
						if (z2 < (*it2).z2) z2 = (*it2).z2;
					}
					else {

						// Empacar bloque ficticio

						BloqueEmpacado ficticio(x1, y1, z1, x2, y2, z2);
						JuntarEspacios(ActualizarEspacios(&ficticio), espacios);
						JuntarEspacios(ActualizarEspaciosFast(&ficticio), espaciosFast);
						x1 = (*it2).x1;
						y1 = (*it2).y1;
						z1 = (*it2).z1;
						x2 = (*it2).x2;
						y2 = (*it2).y2;
						z2 = (*it2).z2;
						idBloque = (*it2).idBloque;
					}
				}
				BloqueEmpacado ficticio(x1, y1, z1, x2, y2, z2);
				JuntarEspacios(ActualizarEspacios(&ficticio), espacios);
				JuntarEspacios(ActualizarEspaciosFast(&ficticio), espaciosFast);
			}
		}
		EliminarEspaciosMinD(espacios);
		ActualizarIndicesCajasDeEspacios(espacios);
		EliminarEspaciosMinD(espaciosFast);
		ActualizarIndicesCajasDeEspacios(espaciosFast);

		// Reestablecer los bloques válidos

		vector<list<Bloque*>>::iterator b1 = bloques.begin() + clienteActual;
		for (vector<list<Bloque*>>::iterator b2 = Bloques00.begin() + clienteActual; b2 != Bloques00.end(); ++b2, ++b1) {
			*b1 = *b2;
		}
		list<Bloque*>::iterator b_it = bloques[clienteActual].begin();
		while (b_it != bloques[clienteActual].end()) {
			bool siguiente = true;
			vector<int>::const_iterator c1 = cantidadesBoxType.begin();
			for (vector<int>::const_iterator c2 = (**b_it).cantidades.begin(); c2 != (**b_it).cantidades.end(); ++c1, ++c2) {
				if (*c2 > *c1) {
					b_it = bloques[clienteActual].erase(b_it);
					siguiente = false;
					break;
				}
			}
			if (siguiente) {
				++b_it;
			}
		}

		// Actualizar el arbol de distribución de pesos y centros de masa de todas las cajas empacadas

		arbol.clear();
		vector<vector<PackedBox>::iterator> cajasSuperioresBloqueSuperior;
		cajasSuperioresBloqueSuperior.reserve(empacados.size());
		for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
			arbol.push_back((*p).id);
			cajasSuperioresBloqueSuperior.push_back(p);
			if ((*p).soportesArriba.size() == 0) {
				(*p).centroMasax = (*p).cx;
				(*p).centroMasay = (*p).cy;
				(*p).masaTotal = (*(*p).bt).masa;
			}
		}
		arbol.sort([&](const int p1i, const int p2i) {
			PackedBox* p1 = &empacados[p1i];
			PackedBox* p2 = &empacados[p2i];
			return (*p1).z2 > (*p2).z2; });
		list<int>::iterator it = arbol.begin();
		PackedBox* pit = nullptr;
		int actualz = 0;
		if (it != arbol.end()) {
			pit = &empacados[*it];
			actualz = (*pit).z2;
		}
		while (it != arbol.end()) {
			if ((*pit).z2 == actualz) {
				(*pit).ActualizarCentroMasaYPeso(&empacados);
				++it;
				if (it != arbol.end()) {
					pit = &empacados[*it];
				}
			}
			else {

				// Distribuir los pesos

				for (list<int>::reverse_iterator it2 = list<int>::reverse_iterator(it); it2 != arbol.rend(); ++it2) {
					PackedBox* pit2 = &empacados[*it2];
					if ((*pit2).z2 != actualz) break;
					(*pit2).DistribuirPesoEntreSoportes();
				}

				// Actualización

				actualz = (*pit).z2;
			}
		}

		// Actualizar árbol superior con sopore fast

		sort(cajasSuperioresBloqueSuperior.begin(), cajasSuperioresBloqueSuperior.end(), [](vector<PackedBox>::iterator p1, vector<PackedBox>::iterator p2)->bool {return (*p1).z1 < (*p2).z1; });
		for (vector<vector<PackedBox>::iterator>::iterator it = cajasSuperioresBloqueSuperior.begin(); it != cajasSuperioresBloqueSuperior.end(); ++it) {
			(**it).maxSoporteFast = (*(**it).b).presion;
			for (vector<SoporteAbajo>::iterator s = (**it).soportes.begin(); s != (**it).soportes.end(); ++s) {
				(**it).maxSoporteFast = min((**it).maxSoporteFast, empacados[(*s).indPB].maxSoporteFast - (*(**it).b).soporteNecesario);
			}
		}
	}

	// Beam Search

	list<Container> ObtenerOpcionesBeam(Container* const idC) {
		list<Container> resp;

		// Se determinar los espacios maximales y opciones

		if (espacios.size() > 0) {

			// Primer espacio

			vector<list<MaximalSpace>::iterator> esp;
			esp.reserve(2);
			int agregados = 0;
			while (agregados == 0 && espacios.size() > 0) {
				list<MaximalSpace>::iterator nuevoEspacio = espacios.begin();
				SeleccionarEspacio_Altura_Fondo(nuevoEspacio, vector<list<MaximalSpace>::iterator>(0));

				// ordenar bloques según best fit

				list<Bloque*> candidatos;
				for (list<Bloque*>::iterator b = bloques[clienteActual].begin(); b != bloques[clienteActual].end(); ++b) {
					if ((**b).dx <= (*nuevoEspacio).dx && (**b).dy <= (*nuevoEspacio).dy && (**b).dz <= (*nuevoEspacio).dz) {
						(**b).DeterminarEspacioMaximal_BS((*nuevoEspacio).dx, (*nuevoEspacio).dy, (*nuevoEspacio).dz);
						candidatos.push_back(*b);
					}
				}
				candidatos.sort([](Bloque* c1, Bloque* c2)->bool {
					if ((*c1).bf1 < (*c2).bf1) return true;
					else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 < (*c2).bf2) return true;
					else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 == (*c2).bf2 && (*c1).bf3 < (*c2).bf3) return true;
					return false; });

				// Opciones

				if (r_maxPresionItems) {
					DeterminarArboles(&*nuevoEspacio);
					for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
						BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
						if (BloqueSoportadoPresion(&nuevo, &*nuevoEspacio)) {
							
							// Reestablecer cajas top

							for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
								empacados[(*p).id] = *p;
							}
							resp.push_back(Container(*idC, true));
							resp.back().Empacar(&nuevo);
							agregados += 1;
							if (agregados == beamWidth) {
								break;
							}
						}
					}

					// Reestablecer cajas top

					for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
						empacados[(*p).id] = *p;
					}
				}
				else {
					if ((*nuevoEspacio).z1 == 0) {
						for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
							resp.push_back(Container(*idC, true));
							BloqueEmpacado be(&**b, &*nuevoEspacio);
							resp.back().Empacar(&be);
							agregados += 1;
							if (agregados == beamWidth) {
								break;
							}
						}
					}
					else {
						DeterminarArboles(&*nuevoEspacio);
						for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
							BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
							if (BloqueSoportado(&nuevo, &*nuevoEspacio)) {
								// Reestablecer cajas top

								for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
									empacados[(*p).id] = *p;
								}
								resp.push_back(Container(*idC, true));
								resp.back().Empacar(&nuevo);
								agregados += 1;
								if (agregados == beamWidth) {
									break;
								}
							}
						}

						// Reestablecer cajas top

						for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
							empacados[(*p).id] = *p;
						}
					}
				}

				// Ciclo

				if (agregados == 0) {
					espacios.erase(nuevoEspacio);
				}
				else {
					esp.push_back(nuevoEspacio);
				}
			}

			// Segundo espacio

			agregados = 0;
			while (agregados == 0 && espacios.size() > esp.size()) {
				list<MaximalSpace>::iterator nuevoEspacio = espacios.begin();
				SeleccionarEspacio_Fondo_Altura(nuevoEspacio, vector<list<MaximalSpace>::iterator>(0), espacios);

				// Determinar si se agrega o no el espacio maximal

				if ((*nuevoEspacio).x1 != (*esp.back()).x1 || (*nuevoEspacio).y1 != (*esp.back()).y1 || (*nuevoEspacio).z1 != (*esp.back()).z1 || (*nuevoEspacio).x2 != (*esp.back()).x2 || (*nuevoEspacio).y2 != (*esp.back()).y2 || (*nuevoEspacio).z2 != (*esp.back()).z2) {
					// ordenar bloques según best fit

					list<Bloque*> candidatos;
					for (list<Bloque*>::iterator b = bloques[clienteActual].begin(); b != bloques[clienteActual].end(); ++b) {
						if ((**b).dx <= (*nuevoEspacio).dx && (**b).dy <= (*nuevoEspacio).dy && (**b).dz <= (*nuevoEspacio).dz) {
							(**b).DeterminarEspacioMaximal_BS((*nuevoEspacio).dx, (*nuevoEspacio).dy, (*nuevoEspacio).dz);
							candidatos.push_back(*b);
						}
					}
					candidatos.sort([](Bloque* c1, Bloque* c2)->bool {
						if ((*c1).bf1 < (*c2).bf1) return true;
						else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 < (*c2).bf2) return true;
						else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 == (*c2).bf2 && (*c1).bf3 < (*c2).bf3) return true;
						return false; });

					// Opciones

					if (r_maxPresionItems) {
						DeterminarArboles(&*nuevoEspacio);
						for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
							BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
							if (BloqueSoportadoPresion(&nuevo, &*nuevoEspacio)) {

								// Reestablecer cajas top

								for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
									empacados[(*p).id] = *p;
								}
								resp.push_back(Container(*idC, true));
								resp.back().Empacar(&nuevo);
								agregados += 1;
								if (agregados == beamWidth) {
									break;
								}
							}
						}

						// Reestablecer cajas top

						for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
							empacados[(*p).id] = *p;
						}
					}
					else {
						if ((*nuevoEspacio).z1 == 0) {
							for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
								resp.push_back(Container(*idC, true));
								BloqueEmpacado be(&**b, &*nuevoEspacio);
								resp.back().Empacar(&be);
								agregados += 1;
								if (agregados == beamWidth) {
									break;
								}
							}
						}
						else {
							DeterminarArboles(&*nuevoEspacio);
							for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
								BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
								if (BloqueSoportado(&nuevo, &*nuevoEspacio)) {

									// Reestablecer cajas top

									for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
										empacados[(*p).id] = *p;
									}
									resp.push_back(Container(*idC, true));
									resp.back().Empacar(&nuevo);
									agregados += 1;
									if (agregados == beamWidth) {
										break;
									}
								}
							}

							// Reestablecer cajas top

							for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
								empacados[(*p).id] = *p;
							}
						}
					}

					// Ciclo

					if (agregados == 0) {
						espacios.erase(nuevoEspacio);
					}
					else {
						esp.push_back(nuevoEspacio);
					}
				}
				else {
					break;
				}
			}

			// Tercer espacio

			agregados = 0;
			while (agregados == 0 && espacios.size() > esp.size()) {
				list<MaximalSpace>::iterator nuevoEspacio = espacios.begin();
				SeleccionarEspacio_Fondo_Bajo(nuevoEspacio, vector<list<MaximalSpace>::iterator>(0));

				// Determinar si se debe agregar

				bool agregar = true;
				for (vector<list<MaximalSpace>::iterator>::iterator e_it = esp.begin(); e_it != esp.end(); ++e_it) {
					if ((*nuevoEspacio).x1 == (**e_it).x1 && (*nuevoEspacio).y1 == (**e_it).y1 && (*nuevoEspacio).z1 == (**e_it).z1 && (*nuevoEspacio).x2 == (**e_it).x2 && (*nuevoEspacio).y2 == (**e_it).y2 && (*nuevoEspacio).z2 == (**e_it).z2) {
						agregar = false;
						break;
					}
				}
				if (agregar) {

					// ordenar bloques según best fit

					list<Bloque*> candidatos;
					for (list<Bloque*>::iterator b = bloques[clienteActual].begin(); b != bloques[clienteActual].end(); ++b) {
						if ((**b).dx <= (*nuevoEspacio).dx && (**b).dy <= (*nuevoEspacio).dy && (**b).dz <= (*nuevoEspacio).dz) {
							(**b).DeterminarEspacioMaximal_BS((*nuevoEspacio).dx, (*nuevoEspacio).dy, (*nuevoEspacio).dz);
							candidatos.push_back(*b);
						}
					}
					candidatos.sort([](Bloque* c1, Bloque* c2)->bool {
						if ((*c1).bf1 < (*c2).bf1) return true;
						else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 < (*c2).bf2) return true;
						else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 == (*c2).bf2 && (*c1).bf3 < (*c2).bf3) return true;
						return false; });

					// Opciones

					if (r_maxPresionItems) {
						DeterminarArboles(&*nuevoEspacio);
						for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
							BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
							if (BloqueSoportadoPresion(&nuevo, &*nuevoEspacio)) {

								// Reestablecer cajas top

								for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
									empacados[(*p).id] = *p;
								}
								resp.push_back(Container(*idC, true));
								resp.back().Empacar(&nuevo);
								agregados += 1;
								if (agregados == beamWidth) {
									break;
								}
							}
						}

						// Reestablecer cajas top

						for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
							empacados[(*p).id] = *p;
						}
					}
					else {
						if ((*nuevoEspacio).z1 == 0) {
							for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
								resp.push_back(Container(*idC, true));
								BloqueEmpacado be(&**b, &*nuevoEspacio);
								resp.back().Empacar(&be);
								agregados += 1;
								if (agregados == beamWidth) {
									break;
								}
							}
						}
						else {
							DeterminarArboles(&*nuevoEspacio);
							for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
								BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
								if (BloqueSoportado(&nuevo, &*nuevoEspacio)) {

									// Reestablecer cajas top

									for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
										empacados[(*p).id] = *p;
									}
									resp.push_back(Container(*idC, true));
									resp.back().Empacar(&nuevo);
									agregados += 1;
									if (agregados == beamWidth) {
										break;
									}
								}
							}

							// Reestablecer cajas top

							for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
								empacados[(*p).id] = *p;
							}
						}
					}

					// Ciclo

					if (agregados == 0) {
						espacios.erase(nuevoEspacio);
					}
				}
				else {
					break;
				}
			}
		}
		return resp;
	}
	list<Container> ObtenerOpcionesBeam_Multidrop(Container* const idC) {
		list<Container> resp;

		// Se determinar los espacios maximales y opciones

		if (espacios.size() > 0) {

			// Primer espacio

			vector<list<MaximalSpace>::iterator> esp;
			esp.reserve(espacios.size());
			int agregados = 0;
			list<MaximalSpace>::iterator espacio1;
			while (agregados == 0 && espacios.size() > esp.size()) {
				list<MaximalSpace>::iterator nuevoEspacio = espacios.begin();
				espacio1 = nuevoEspacio;
				SeleccionarEspacio_Altura_Fondo(nuevoEspacio, esp);
				esp.push_back(nuevoEspacio);

				// ordenar bloques según best fit

				list<Bloque*> candidatos;
				for (list<Bloque*>::iterator b = bloques[clienteActual].begin(); b != bloques[clienteActual].end(); ++b) {
					if ((**b).dx <= (*nuevoEspacio).dx && (**b).dy <= (*nuevoEspacio).dy && (**b).dz <= (*nuevoEspacio).dz) {
						(**b).DeterminarEspacioMaximal_BS((*nuevoEspacio).dx, (*nuevoEspacio).dy, (*nuevoEspacio).dz);
						candidatos.push_back(*b);
					}
				}
				candidatos.sort([](Bloque* c1, Bloque* c2)->bool {
					if ((*c1).bf1 < (*c2).bf1) return true;
					else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 < (*c2).bf2) return true;
					else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 == (*c2).bf2 && (*c1).bf3 < (*c2).bf3) return true;
					return false; });

				// Opciones

				if (r_maxPresionItems) {
					DeterminarArboles(&*nuevoEspacio);
					for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
						BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
						if (BloqueSoportadoPresion(&nuevo, &*nuevoEspacio)) {
						
							// Reestablecer cajas top

							for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
								empacados[(*p).id] = *p;
							}
							resp.push_back(Container(*idC, true));
							resp.back().Empacar_Multidrop(&nuevo);
							agregados += 1;
							if (agregados == beamWidth) {
								break;
							}
						}
					}

					// Reestablecer cajas top

					for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
						empacados[(*p).id] = *p;
					}
				}
				else {
					if ((*nuevoEspacio).z1 == 0) {
						for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
							resp.push_back(Container(*idC, true));
							BloqueEmpacado be(&**b, &*nuevoEspacio);
							resp.back().Empacar_Multidrop(&be);
							agregados += 1;
							if (agregados == beamWidth) {
								break;
							}
						}
					}
					else {
						DeterminarArboles(&*nuevoEspacio);
						for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
							BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
							if (BloqueSoportado(&nuevo, &*nuevoEspacio)) {

								// Reestablecer cajas top

								for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
									empacados[(*p).id] = *p;
								}
								resp.push_back(Container(*idC, true));
								resp.back().Empacar_Multidrop(&nuevo);
								agregados += 1;
								if (agregados == beamWidth) {
									break;
								}
							}
						}

						// Reestablecer cajas top

						for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
							empacados[(*p).id] = *p;
						}
					}
				}
			}

			// Segundo espacio

			agregados = 0;
			esp.clear();
			list<MaximalSpace>::iterator espacio2;
			while (agregados == 0 && espacios.size() > esp.size()) {
				list<MaximalSpace>::iterator nuevoEspacio = espacios.begin();
				espacio2 = nuevoEspacio;
				SeleccionarEspacio_Fondo_Altura(nuevoEspacio, esp, espacios);
				esp.push_back(nuevoEspacio);

				if ((*nuevoEspacio).x1 != (*espacio1).x1 || (*nuevoEspacio).y1 != (*espacio1).y1 || (*nuevoEspacio).z1 != (*espacio1).z1 || (*nuevoEspacio).x2 != (*espacio1).x2 || (*nuevoEspacio).y2 != (*espacio1).y2 || (*nuevoEspacio).z2 != (*espacio1).z2) {

					// ordenar bloques según best fit

					list<Bloque*> candidatos;
					for (list<Bloque*>::iterator b = bloques[clienteActual].begin(); b != bloques[clienteActual].end(); ++b) {
						if ((**b).dx <= (*nuevoEspacio).dx && (**b).dy <= (*nuevoEspacio).dy && (**b).dz <= (*nuevoEspacio).dz) {
							(**b).DeterminarEspacioMaximal_BS((*nuevoEspacio).dx, (*nuevoEspacio).dy, (*nuevoEspacio).dz);
							candidatos.push_back(*b);
						}
					}
					candidatos.sort([](Bloque* c1, Bloque* c2)->bool {
						if ((*c1).bf1 < (*c2).bf1) return true;
						else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 < (*c2).bf2) return true;
						else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 == (*c2).bf2 && (*c1).bf3 < (*c2).bf3) return true;
						return false; });

					// Opciones

					if (r_maxPresionItems) {
						DeterminarArboles(&*nuevoEspacio);
						for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
							BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
							if (BloqueSoportadoPresion(&nuevo, &*nuevoEspacio)) {

								// Reestablecer cajas top

								for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
									empacados[(*p).id] = *p;
								}
								resp.push_back(Container(*idC, true));
								resp.back().Empacar_Multidrop(&nuevo);
								agregados += 1;
								if (agregados == beamWidth) {
									break;
								}
							}
						}

						// Reestablecer cajas top

						for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
							empacados[(*p).id] = *p;
						}
					}
					else {
						if ((*nuevoEspacio).z1 == 0) {
							for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
								resp.push_back(Container(*idC, true));
								BloqueEmpacado be(&**b, &*nuevoEspacio);
								resp.back().Empacar_Multidrop(&be);
								agregados += 1;
								if (agregados == beamWidth) {
									break;
								}
							}
						}
						else {
							DeterminarArboles(&*nuevoEspacio);
							for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
								BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
								if (BloqueSoportado(&nuevo, &*nuevoEspacio)) {

									// Reestablecer cajas top

									for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
										empacados[(*p).id] = *p;
									}
									resp.push_back(Container(*idC, true));
									resp.back().Empacar_Multidrop(&nuevo);
									agregados += 1;
									if (agregados == beamWidth) {
										break;
									}
								}
							}

							// Reestablecer cajas top

							for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
								empacados[(*p).id] = *p;
							}
						}
					}
				}
				else {
					break;
				}
			}

			// Tercer espacio

			agregados = 0;
			esp.clear();
			while (agregados == 0 && espacios.size() > esp.size()) {
				list<MaximalSpace>::iterator nuevoEspacio = espacios.begin();
				SeleccionarEspacio_Fondo_Bajo(nuevoEspacio, esp);
				esp.push_back(nuevoEspacio);

				if ((*nuevoEspacio).x1 != (*espacio1).x1 || (*nuevoEspacio).y1 != (*espacio1).y1 || (*nuevoEspacio).z1 != (*espacio1).z1 || (*nuevoEspacio).x2 != (*espacio1).x2 || (*nuevoEspacio).y2 != (*espacio1).y2 || (*nuevoEspacio).z2 != (*espacio1).z2) {
					if ((*nuevoEspacio).x1 != (*espacio2).x1 || (*nuevoEspacio).y1 != (*espacio2).y1 || (*nuevoEspacio).z1 != (*espacio2).z1 || (*nuevoEspacio).x2 != (*espacio2).x2 || (*nuevoEspacio).y2 != (*espacio2).y2 || (*nuevoEspacio).z2 != (*espacio2).z2) {

						// ordenar bloques según best fit

						list<Bloque*> candidatos;
						for (list<Bloque*>::iterator b = bloques[clienteActual].begin(); b != bloques[clienteActual].end(); ++b) {
							if ((**b).dx <= (*nuevoEspacio).dx && (**b).dy <= (*nuevoEspacio).dy && (**b).dz <= (*nuevoEspacio).dz) {
								(**b).DeterminarEspacioMaximal_BS((*nuevoEspacio).dx, (*nuevoEspacio).dy, (*nuevoEspacio).dz);
								candidatos.push_back(*b);
							}
						}
						candidatos.sort([](Bloque* c1, Bloque* c2)->bool {
							if ((*c1).bf1 < (*c2).bf1) return true;
							else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 < (*c2).bf2) return true;
							else if ((*c1).bf1 == (*c2).bf1 && (*c1).bf2 == (*c2).bf2 && (*c1).bf3 < (*c2).bf3) return true;
							return false; });

						// Opciones

						if (r_maxPresionItems) {
							DeterminarArboles(&*nuevoEspacio);
							for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
								BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
								if (BloqueSoportadoPresion(&nuevo, &*nuevoEspacio)) {

									// Reestablecer cajas top

									for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
										empacados[(*p).id] = *p;
									}
									resp.push_back(Container(*idC, true));
									resp.back().Empacar_Multidrop(&nuevo);
									agregados += 1;
									if (agregados == beamWidth) {
										break;
									}
								}
							}

							// Reestablecer cajas top

							for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
								empacados[(*p).id] = *p;
							}
						}
						else {
							if ((*nuevoEspacio).z1 == 0) {
								for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
									resp.push_back(Container(*idC, true));
									BloqueEmpacado be(&**b, &*nuevoEspacio);
									resp.back().Empacar_Multidrop(&be);
									agregados += 1;
									if (agregados == beamWidth) {
										break;
									}
								}
							}
							else {
								DeterminarArboles(&*nuevoEspacio);
								for (list<Bloque*>::const_iterator b = candidatos.begin(); b != candidatos.end(); ++b) {
									BloqueEmpacado nuevo(&**b, &*nuevoEspacio);
									if (BloqueSoportado(&nuevo, &*nuevoEspacio)) {

										// Reestablecer cajas top

										for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
											empacados[(*p).id] = *p;
										}
										resp.push_back(Container(*idC, true));
										resp.back().Empacar_Multidrop(&nuevo);
										agregados += 1;
										if (agregados == beamWidth) {
											break;
										}
									}
								}

								// Reestablecer cajas top

								for (list<PackedBox>::iterator p = arbolTop.begin(); p != arbolTop.end(); ++p) {
									empacados[(*p).id] = *p;
								}
							}
						}
					}
					else {
						break;
					}
				}
				else {
					break;
				}
			}
		}		
		return resp;
	}
	double DeterminarAreaBase(int const& hasta) {
		list<AreaBase> areas(1, AreaBase(0, 0, ContenedorDimx, ContenedorDimy));
		list<AreaBase>::iterator a_it = areas.begin();
		while (a_it != areas.end()) {
			for (vector<PackedBox>::iterator it = empacados.begin(); it != empacados.begin() + hasta; ++it) {
				if (!((*a_it).x1 >= (*it).x2 || (*a_it).y1 >= (*it).y2 || (*a_it).x2 <= (*it).x1 || (*a_it).y2 <= (*it).y1)) {

					// x amplias

					if ((*a_it).x2 > (*it).x2) areas.push_back(AreaBase((*it).x2, (*a_it).y1, (*a_it).x2, (*a_it).y2));
					if ((*it).x1 > (*a_it).x1) areas.push_back(AreaBase((*a_it).x1, (*a_it).y1, (*it).x1, (*a_it).y2));

					// y controladas

					if ((*a_it).y2 > (*it).y2) areas.push_back(AreaBase(max((*it).x1, (*a_it).x1), (*it).y2, min((*it).x2, (*a_it).x2), (*a_it).y2));
					if ((*it).y1 > (*a_it).y1) areas.push_back(AreaBase(max((*it).x1, (*a_it).x1), (*a_it).y1, min((*it).x2, (*a_it).x2), (*it).y1));

					// actuliazación

					a_it = areas.erase(a_it);
					break;
				}
			}
			++a_it;
		}
		int resp = ContenedorDimx * ContenedorDimy;
		for (list<AreaBase>::iterator it = areas.begin(); it != areas.end(); ++it) {
			resp -= ((*it).x2 - (*it).x1) * ((*it).y2 - (*it).y1);
		}
		return (double)resp;
	}
	pair<int, double> MetricaIncumbente(double const& porcentaje) {

		// Determinar Hasta

		double factor = 0.9;
		int miVol = (int)((double)volEmpacado * porcentaje);
		int areaB = 0;
		int areaTotal = 0;
		int volB = 0;
		int volTotal = 0;
		vector<PackedBox>::const_iterator it = empacados.begin();
		int idBloque = 0;
		while (it != empacados.end()) {
			if ((*it).idBloque == idBloque) {
				volB += (*(*it).bt).volumenReal;
				if ((*it).z1 == 0) areaB += (*(*it).b).area;
				++it;
			}
			else {
				if (volB + volTotal >= miVol) {
					if (miVol - volTotal <= volB + volTotal - miVol) {
						return{ volTotal, factor * (double)volTotal / (double)areaTotal };
					}
					else {
						return{ volB + volTotal, factor * (double)(volB + volTotal) / (double)(areaB + areaTotal) };
					}
				}
				volTotal += volB;
				volB = 0;
				areaTotal += areaB;
				areaB = 0;
				idBloque = (*it).idBloque;
			}
		}
		if (miVol - volTotal <= volB + volTotal - miVol) {
			return{ volTotal, factor * (double)volTotal / (double)areaTotal };
		}
		else {
			return{ volB + volTotal, factor * (double)(volB + volTotal) / (double)(areaB + areaTotal) };
		}
	}
	pair<int, double> MetricaIncumbente2(double const& porcentaje) {

		// Determinar Hasta

		double factor = 0.9;
		int miVol = (int)((double)volEmpacado * porcentaje);
		int volB = 0;
		int volTotal = 0;
		int hastaB = 0;
		int hastaTotal = 0;
		vector<PackedBox>::const_iterator it = empacados.begin();
		int idBloque = 0;
		while (it != empacados.end()) {
			if ((*it).idBloque == idBloque) {
				volB += (*(*it).bt).volumenReal;
				++hastaB;
				++it;
			}
			else {
				if (volB + volTotal >= miVol) {
					if (miVol - volTotal <= volB + volTotal - miVol) {
						return{ volTotal, factor * (double)volTotal / DeterminarAreaBase(hastaTotal) };
					}
					else {
						return{ volB + volTotal, factor * (double)(volB + volTotal) / DeterminarAreaBase(hastaTotal + hastaB) };
					}
				}
				volTotal += volB;
				volB = 0;
				hastaTotal += hastaB;
				hastaB = 0;
				idBloque = (*it).idBloque;
			}
		}
		if (miVol - volTotal <= volB + volTotal - miVol) {
			return{ volTotal, factor * (double)volTotal / DeterminarAreaBase(hastaTotal) };
		}
		else {
			return{ volB + volTotal, factor * (double)(volB + volTotal) / DeterminarAreaBase(hastaTotal + hastaB) };
		}
	}
	pair<double, int> MetricaNoIncumente(int const& miVol) {
		int volB = 0;
		int volTotal = 0;
		int areaB = 0;
		int areaTotal = 0;
		int hastaB = 0;
		int hastaTotal = 0;
		int idBloque = 0;
		vector<PackedBox>::const_iterator it = empacados.begin();
		while (it != empacados.end()) {
			if (idBloque == (*it).idBloque) {
				volB += (*(*it).bt).volumenReal;
				if ((*it).z1 == 0) areaB += (*(*it).b).area;
				++hastaB;
				++it;
			}
			else {
				if (volB + volTotal >= miVol) {
					if (miVol - volTotal <= volB + volTotal - miVol) {
						return{ (double)volTotal / (double)areaTotal, hastaTotal };
					}
					else {
						return{ (double)(volB + volTotal) / (double)(areaB + areaTotal), hastaB + hastaTotal };
					}
				}
				hastaTotal += hastaB;
				hastaB = 0;
				volTotal += volB;
				volB = 0;
				areaTotal += areaB;
				areaB = 0;
				idBloque = (*it).idBloque;
			}
		}
		return { 0,0 };
	}
	pair<double, int> MetricaNoIncumente2(int const& miVol) {
		int volB = 0;
		int volTotal = 0;
		int hastaB = 0;
		int hastaTotal = 0;
		int idBloque = 0;
		vector<PackedBox>::const_iterator it = empacados.begin();
		while (it != empacados.end()) {
			if (idBloque == (*it).idBloque) {
				volB += (*(*it).bt).volumenReal;
				++hastaB;
				++it;
			}
			else {
				if (volB + volTotal >= miVol) {
					if (miVol - volTotal <= volB + volTotal - miVol) {
						return{ (double)volTotal / DeterminarAreaBase(hastaTotal), hastaTotal };
					}
					else {
						return{ (double)(volB + volTotal) / DeterminarAreaBase(hastaTotal + hastaB), hastaTotal + hastaB };
					}
				}
				hastaTotal += hastaB;
				hastaB = 0;
				volTotal += volB;
				volB = 0;
				idBloque = (*it).idBloque;
			}
		}
		return { 0,0 };
	}

	// Verificador

	void Verificador(string const& ins) {
		ofstream archivo("Reporte.txt", ios::app);
		cout << "VERIFICADOR" << endl;

		// Contención

		for (vector<PackedBox>::iterator p_it = empacados.begin(); p_it != empacados.end(); ++p_it) {
			if ((*p_it).x1 < 0 || (*p_it).x1 > ContenedorDimx || (*p_it).y1 < 0 || (*p_it).y1 > ContenedorDimy || (*p_it).z1 < 0 || (*p_it).z1 > ContenedorDimz) {
				archivo << "Contención. Ins = " << ins << " id = " << to_string((*p_it).id) << endl;
			}
		}

		// Solapamiento

		for (vector<PackedBox>::iterator p1 = empacados.begin(); p1 != empacados.end(); ++p1) {
			for (vector<PackedBox>::iterator p2 = p1 + 1; p2 != empacados.end(); ++p2) {
				if (!((*p1).x1 >= (*p2).x2 || (*p2).x1 >= (*p1).x2 || (*p1).y1 >= (*p2).y2 || (*p2).y1 >= (*p1).y2 || (*p1).z1 >= (*p2).z2 || (*p2).z1 >= (*p1).z2)) {
					archivo << "Solapamiento. Ins = " << ins << " id1 = " << to_string((*p1).id) << " id2 = " << to_string((*p2).id) << endl;
				}
			}
		}

		// Cantidades para cada cliente

		int ultimocliente = (*empacados.back().bt).cliente;
		for (vector<BoxType>::iterator b = BoxTypes0.begin(); b != BoxTypes0.end(); ++b) {
			int c = (*b).cantidad;
			for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
				if ((*b).id == (*(*p).bt).id) {
					--c;
				}
			}
			if ((*b).cliente == ultimocliente) {
				if (c < 0) {
					archivo << "Cantidad. Ins = " << ins << " Boxtype = " << to_string((*b).id) << " Cantidad = " << to_string(c) << endl;
				}
			}
			else if ((*b).cliente < ultimocliente) {
				if (c != 0) {
					cout << "cliente = " << (*b).cliente << endl;
					archivo << "Cantidad. Ins = " << ins << " Boxtype = " << to_string((*b).id) << " Cantidad = " << to_string(c) << endl;
				}
			}
		}

		// Multidrop

		if (r_multidrop == 1) {
			vector<list<int>> indices((*empacados.back().bt).cliente + 1);
			for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
				indices[(*(*p).bt).cliente].push_back((*p).id);
			}
			for (vector<list<int>>::reverse_iterator ind1 = indices.rbegin(); ind1 != indices.rend(); ++ind1) {
				for (list<int>::iterator it1 = (*ind1).begin(); it1 != (*ind1).end(); ++it1) {
					PackedBox& p1 = empacados[*it1];

					// Que su proyeccion no coincida con otras cajas de otros clientes

					for (vector<list<int>>::reverse_iterator ind2 = ind1 + 1; ind2 != indices.rend(); ++ind2) {
						for (list<int>::iterator it2 = (*ind2).begin(); it2 != (*ind2).end(); ++it2) {
							PackedBox& p2 = empacados[*it2];
							if (p2.x2 > p1.x2) {
								if (!(p1.y1 >= p2.y2 || p2.y1 >= p1.y2 || p1.z1 >= p2.z2 || p2.z1 >= p1.z2)) {
									archivo << "Multi-DropVisibility. Ins = " << ins << " P = " << to_string(p1.id) << " de cliente " << to_string((*p1.bt).cliente) << " Tapada por P = " << to_string(p2.id) << " de cliente " << to_string((*p2.bt).cliente) << endl;
								}
							}
							if (p2.z1 == p1.z2) {
								if (!(p1.y1 >= p2.y2 || p2.y1 >= p1.y2 || p1.x1 >= p2.x2 || p2.x1 >= p1.x2)) {
									bool esSoporte = false;
									for (vector<SoporteAbajo>::iterator s = p2.soportes.begin(); s != p2.soportes.end(); ++s) {
										if ((*s).indPB == *it1) {
											esSoporte = true;
											break;
										}
									}
									if (esSoporte) {
										archivo << "Multi-DropVisibility. Ins = " << ins << " P = " << to_string(p1.id) << " de cliente " << to_string((*p1.bt).cliente) << " Debajo de P = " << to_string(p2.id) << " de cliente " << to_string((*p2.bt).cliente) << endl;
									}
								}
							}
						}
					}
				}
			}
		}
		else if (r_multidrop == 2) {
			vector<int> ini((*empacados.back().bt).cliente + 1, ContenedorDimx);
			vector<int> fin((*empacados.back().bt).cliente + 1, 0);
			for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
				const int c = (*(*p).bt).cliente;
				if (ini[c] > (*p).x1) ini[c] = (*p).x1;
				if (fin[c] < (*p).x2) fin[c] = (*p).x2;
			}
			vector<int>::iterator i1 = ini.begin();
			for (vector<int>::iterator f1 = fin.begin(); f1 != fin.end() - 1; ++i1, ++f1) {
				vector<int>::iterator i2 = i1 + 1;
				for (vector<int>::iterator f2 = f1 + 1; f2 != fin.end(); ++i2, ++f2) {
					if (*i2 < *f1 || *i1 >= *f1 || *i2 >= *f2) {
						archivo << "Multi-DropCapas. Ins = " << ins << " Cliente1 = " << to_string(distance(ini.begin(), i1)) << " x1i = " << to_string(*i1) << " x1f = " << to_string(*f1) << " Cliente2 = " << to_string(distance(ini.begin(), i2)) << " x2i = " << to_string(*i2) << " x2f = " << to_string(*f2) << endl;
					}
				}
			}
		}

		// Load Bearing
		/*
		list<int> arbol;
		for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
			arbol.push_back((*p).id);
		}
		arbol.sort([&](int const& p1, int const& p2)->bool {return empacados[p1].z2 > empacados[p2].z2; });
		list<int>::iterator it = arbol.begin();
		PackedBox* pit = nullptr;
		int actualz = 0;
		if (it != arbol.end()) {
			pit = &empacados[*it];
			actualz = (*pit).z2;
		}
		while (it != arbol.end()) {
			if ((*pit).z2 == actualz) {
				(*pit).ActualizarCentroMasaYPeso(&empacados);
				if (!(*pit).soportada) {
					archivo << "Soporte. ins = " << ins << " Box = " << to_string(*it) << endl;
				}
				++it;
				if (it != arbol.end()) {
					pit = &empacados[*it];
				}
			}
			else {

				// Distribuir los pesos

				for (list<int>::reverse_iterator it2 = list<int>::reverse_iterator(it); it2 != arbol.rend(); ++it2) {
					PackedBox* pit2 = &empacados[*it2];
					if ((*pit2).z2 != actualz) break;
					(*pit2).DistribuirPesoEntreSoportes();
					if (!(*pit2).soportada) {
						archivo << "Load-Bearing. ins = " << ins << " Box = " << to_string(*it2) << endl;
					}
				}

				// Actualización

				actualz = (*pit).z2;
			}
		}
		*/
		archivo.close();
	}
	/*
	void VerificarUtilizacion() {
		double miUt = 0.0;
		for (vector<PackedBox>::iterator p = empacados.begin(); p != empacados.end(); ++p) {
			miUt += (*(*p).bt).volumenReal;
		}
		miUt /= ContenedorVol;
		if (abs(miUt - utilizacion) > error) {
			cout << "MalUtilizacion" << endl;
		}
	}
	*/
	// Operadores

	const bool operator<(Container const& otro) {
		return utilizacion < otro.utilizacion;
	}
};
bool const Orden_ContainerUtilizacion(Container const& c1, Container const& c2) {
	return c1.utilizacion > c2.utilizacion;
}

// Variables globales 2

Container C0;
Container incumbente;

// Funciones

const bool ValidacionesPrevias() {

	// Inconsistencia de los parámetros globales

	if (r_maxPresionItems && r_estabilidad == 0) {
		cout << "Inconsistenacia: no se puede habilitar la restricción de presión sobre los ítems si no se tiene en cuenta la estabilidad." << endl;
		return true;
	}

	// Impresión de parámetros

	cout << "Los parámetros que se usan son los siguientes" << endl;
	if (r_multidrop == 1) cout << "Con MultiDrop - Visibilidad" << endl;
	else if (r_multidrop == 2) cout << "Con Multidrop - Capas" << endl;
	else cout << "Sin MultiDrop" << endl;
	if (r_maxPresionItems) cout << "Con presión items" << endl;
	else cout << "Sin presión items" << endl;
	if (r_estabilidad == 0) cout << "Sin soporte" << endl;
	else if (r_estabilidad == 1) cout << "Con soporte parcial" << endl;
	else cout << "Con soporte completo" << endl;
	if (r_juntarEspacios == 0) cout << "Sin juntar ni expandir espacios maximales" << endl;
	else if (r_juntarEspacios == 1) cout << "Con juntar pero sin expandir espacios maximales" << endl;
	else cout << "Con juntar y expandir espacios maximales" << endl;

	return false;
}
void EliminarCajasDimensiones() {
	for (int i = (int)BoxTypes0.size() - 1; i >= 0; --i) {
		BoxType& bt = BoxTypes0[i];
		int tamanioInicial = (int)bt.boxes.size();
		for (int j = (int)bt.boxes.size() - 1; j >= 0; --j) {
			Box& b = bt.boxes[j];
			if (b.x > ContenedorDimx || b.y > ContenedorDimy || b.z > ContenedorDimz) bt.boxes.erase(bt.boxes.begin() + j);
		}
		if ((int)bt.boxes.size() == 0) {
			BoxTypes0.erase(BoxTypes0.begin() + i);
			cout << "CUIDADO: Se eliminó completamente una pieza porque no alcanza en el contenedor" << endl;
		}
		else if (tamanioInicial != bt.boxes.size()) {
			bt.DetermineDmins();
		}
	}
}
void ReadData(string const& ins) {
	string filePath = "Instances/" + ins + ".txt";
	ifstream reader(filePath);
	char delimitador = ' ';

	// Leer contenedor

	string line, val;
	getline(reader, line, '\n');
	stringstream iss, convertor;
	iss = stringstream(line);
	getline(iss, val, delimitador); convertor = stringstream(val);
	convertor >> ContenedorDimx;
	getline(iss, val, delimitador); convertor = stringstream(val);
	convertor >> ContenedorDimy;
	getline(iss, val, delimitador); convertor = stringstream(val);
	convertor >> ContenedorDimz;
	ContenedorVol = ContenedorDimx * ContenedorDimy * ContenedorDimz;
	getline(reader, line, '\n'); // línea que no se lee

	// Leer cajas

	if (r_multidrop > 0) {
		if (r_maxPresionItems) {
			int idCaja = 0;
			while (getline(reader, line, '\n')) {
				iss = stringstream(line);
				getline(iss, val, delimitador); convertor = stringstream(val);
				int x; convertor >> x;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rx; convertor >> rx; if (rx == 1) rx = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int y; convertor >> y;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int ry; convertor >> ry; if (ry == 1) ry = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int z; convertor >> z;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rz; convertor >> rz; if (rz == 1) rz = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int q; convertor >> q;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double m; convertor >> m;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double px; convertor >> px;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double py; convertor >> py;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double pz; convertor >> pz;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int c; convertor >> c;

				// Agregar Caja

				BoxTypes0.push_back(BoxType(idCaja, q, x, y, z, rx, ry, rz, m, px, py, pz, c));
				++idCaja;
			}
		}
		else {
			int idCaja = 0;
			while (getline(reader, line, '\n')) {
				iss = stringstream(line);
				getline(iss, val, delimitador); convertor = stringstream(val);
				int x; convertor >> x;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rx; convertor >> rx; if (rx == 1) rx = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int y; convertor >> y;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int ry; convertor >> ry; if (ry == 1) ry = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int z; convertor >> z;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rz; convertor >> rz; if (rz == 1) rz = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int q; convertor >> q;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int c; convertor >> c;

				// Agregar Caja

				BoxTypes0.push_back(BoxType(idCaja, q, x, y, z, rx, ry, rz, 0, c));
				++idCaja;
			}
		}
		EliminarCajasDimensiones();
		sort(BoxTypes0.begin(), BoxTypes0.end(), [](BoxType const& bt1, BoxType const& bt2)->bool {return bt1.cliente < bt2.cliente; });

		// Se pone el id del cliente correcto y del tipo de caja

		int clienteReal = BoxTypes0.front().cliente;
		int clienteModificado = 0;
		int idV = 0;
		for (vector<BoxType>::iterator bt = BoxTypes0.begin(); bt < BoxTypes0.end(); ++bt, ++idV) {
			if ((*bt).cliente != clienteReal) {
				++clienteModificado;
				clienteReal = (*bt).cliente;
			}
			(*bt).cliente = clienteModificado;
			(*bt).ActualizarIndice(idV);
		}
	}
	else {
		if (r_maxPresionItems) {
			int idCaja = 0;
			while (getline(reader, line, '\n')) {
				iss = stringstream(line);
				getline(iss, val, delimitador); convertor = stringstream(val);
				int x; convertor >> x;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rx; convertor >> rx; if (rx == 1) rx = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int y; convertor >> y;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int ry; convertor >> ry; if (ry == 1) ry = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int z; convertor >> z;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rz; convertor >> rz; if (rz == 1) rz = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int q; convertor >> q;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double m; convertor >> m;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double px; convertor >> px;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double py; convertor >> py;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double pz; convertor >> pz;

				// Agregar Caja

				BoxTypes0.push_back(BoxType(idCaja, q, x, y, z, rx, ry, rz, m, px, py, pz));
				++idCaja;
			}
		}
		else {
			int idCaja = 0;
			while (getline(reader, line, '\n')) {
				iss = stringstream(line);
				getline(iss, val, delimitador); convertor = stringstream(val);
				int x; convertor >> x;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rx; convertor >> rx; if (rx == 1) rx = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int y; convertor >> y;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int ry; convertor >> ry; if (ry == 1) ry = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int z; convertor >> z;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rz; convertor >> rz; if (rz == 1) rz = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int q; convertor >> q;

				// Agregar Caja

				BoxTypes0.push_back(BoxType(idCaja, q, x, y, z, rx, ry, rz));
				++idCaja;
			}
		}
		EliminarCajasDimensiones();

		// Actualizar id del tipo de caja

		int idV = 0;
		for (vector<BoxType>::iterator bt = BoxTypes0.begin(); bt < BoxTypes0.end(); ++bt, ++idV) {
			(*bt).ActualizarIndice(idV);
		}
	}
	reader.close();
}
void ReadData2(string const& ins)
{
	if (g_inMemoryMode) {
		ContenedorDimx = g_inContDimx;
		ContenedorDimy = g_inContDimy;
		ContenedorDimz = g_inContDimz;
		ContenedorVol = ContenedorDimx * ContenedorDimy * ContenedorDimz;

		g_idRealATexto.clear();
		int idCaja = 0;
		for (vector<PiezaEntradaMemoria>::const_iterator p = g_inPiezas.begin(); p != g_inPiezas.end(); ++p, ++idCaja) {
			// cantidad=1 (una pieza física por ID), rotación libre en los 3 ejes (rx=ry=rz=3)
			BoxTypes0.push_back(BoxType(idCaja, 1, (*p).dimx, (*p).dimy, (*p).dimz, 3, 3, 3));
			g_idRealATexto.push_back((*p).idTexto);
		}

		EliminarCajasDimensiones();

		int idV = 0;
		for (vector<BoxType>::iterator bt = BoxTypes0.begin(); bt < BoxTypes0.end(); ++bt, ++idV) {
			(*bt).ActualizarIndice(idV);
		}
		return;
	}

	string filePath = "Instances/" + ins + ".txt";
	ifstream reader(filePath);
	char delimitador = '\t';

	// Leer contenedor

	string line, val;
	getline(reader, line, '\n');
	getline(reader, line, '\n');
	stringstream iss, convertor;
	iss = stringstream(line);
	getline(iss, val, delimitador); convertor = stringstream(val);
	convertor >> ContenedorDimx;
	getline(iss, val, delimitador); convertor = stringstream(val);
	convertor >> ContenedorDimy;
	getline(iss, val, delimitador); convertor = stringstream(val);
	convertor >> ContenedorDimz;
	ContenedorVol = ContenedorDimx * ContenedorDimy * ContenedorDimz;

	// Leer cajas

	if (r_multidrop > 0) {
		if (r_maxPresionItems) {
			int idCaja = 0;
			while (getline(reader, line, '\n')) {
				iss = stringstream(line);
				getline(iss, val, delimitador);
				getline(iss, val, delimitador); convertor = stringstream(val);
				int x; convertor >> x;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rx; convertor >> rx; if (rx == 1) rx = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int y; convertor >> y;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int ry; convertor >> ry; if (ry == 1) ry = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int z; convertor >> z;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rz; convertor >> rz; if (rz == 1) rz = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int q; convertor >> q;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double m; convertor >> m;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double px; convertor >> px;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double py; convertor >> py;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double pz; convertor >> pz;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int c; convertor >> c;
				/*
				int c;
				while (getline(iss, val, delimitador)) {
					convertor = stringstream(val);
					convertor >> c;
				}
				*/

				// Agregar Caja

				BoxTypes0.push_back(BoxType(idCaja, q, x, y, z, rx, ry, rz, m, px, py, pz, c));
				++idCaja;
			}
		}
		else {
			int idCaja = 0;
			while (getline(reader, line, '\n')) {
				iss = stringstream(line);
				getline(iss, val, delimitador);
				getline(iss, val, delimitador); convertor = stringstream(val);
				int x; convertor >> x;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rx; convertor >> rx; if (rx == 1) rx = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int y; convertor >> y;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int ry; convertor >> ry; if (ry == 1) ry = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int z; convertor >> z;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rz; convertor >> rz; if (rz == 1) rz = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int q; convertor >> q;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double m; convertor >> m;
				int c;
				while (getline(iss, val, delimitador)) {
					convertor = stringstream(val);
					convertor >> c;
				}

				// Agregar Caja

				BoxTypes0.push_back(BoxType(idCaja, q, x, y, z, rx, ry, rz, m, c));
				++idCaja;
			}
		}
		EliminarCajasDimensiones();
		sort(BoxTypes0.begin(), BoxTypes0.end(), [](BoxType const& bt1, BoxType const& bt2)->bool {return bt1.cliente < bt2.cliente; });

		// Actualizar id del cliente y del tipo de caja

		int clienteReal = BoxTypes0.front().cliente;
		int clienteModificado = 0;
		int idV = 0;
		for (vector<BoxType>::iterator bt = BoxTypes0.begin(); bt < BoxTypes0.end(); ++bt, ++idV) {
			if ((*bt).cliente != clienteReal) {
				++clienteModificado;
				clienteReal = (*bt).cliente;
			}
			(*bt).cliente = clienteModificado;
			(*bt).ActualizarIndice(idV);
		}
	}
	else {
		if (r_maxPresionItems) {
			int idCaja = 0;
			while (getline(reader, line, '\n')) {
				iss = stringstream(line);
				getline(iss, val, delimitador);
				getline(iss, val, delimitador); convertor = stringstream(val);
				int x; convertor >> x;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rx; convertor >> rx; if (rx == 1) rx = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int y; convertor >> y;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int ry; convertor >> ry; if (ry == 1) ry = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int z; convertor >> z;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rz; convertor >> rz; if (rz == 1) rz = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int q; convertor >> q;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double m; convertor >> m;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double px; convertor >> px;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double py; convertor >> py;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double pz; convertor >> pz;

				// Agregar Caja

				BoxTypes0.push_back(BoxType(idCaja, q, x, y, z, rx, ry, rz, m, px, py, pz));
				++idCaja;
			}
		}
		else {
			int idCaja = 0;
			while (getline(reader, line, '\n')) {
				iss = stringstream(line);
				getline(iss, val, delimitador);
				getline(iss, val, delimitador); convertor = stringstream(val);
				int x; convertor >> x;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rx; convertor >> rx; if (rx == 1) rx = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int y; convertor >> y;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int ry; convertor >> ry; if (ry == 1) ry = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int z; convertor >> z;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int rz; convertor >> rz; if (rz == 1) rz = 3;
				getline(iss, val, delimitador); convertor = stringstream(val);
				int q; convertor >> q;
				getline(iss, val, delimitador); convertor = stringstream(val);
				double m; convertor >> m;

				// Agregar Caja

				BoxTypes0.push_back(BoxType(idCaja, q, x, y, z, rx, ry, rz, m));
				++idCaja;
			}
		}
		EliminarCajasDimensiones();

		// Actualizar id del tipo de caja

		int idV = 0;
		for (vector<BoxType>::iterator bt = BoxTypes0.begin(); bt < BoxTypes0.end(); ++bt, ++idV) {
			(*bt).ActualizarIndice(idV);
		}
	}
	reader.close();
}
void HacerBloquesGenerales() {

	// Determinar cantidades 0

	cantidadesBoxTypes0.clear();
	for (vector<BoxType>::const_iterator it = BoxTypes0.begin(); it != BoxTypes0.end(); ++it) {
		cantidadesBoxTypes0.push_back((*it).cantidad);
	}

	// Bloques generales

	if (r_multidrop == 0 && r_maxPresionItems == false) {

		// Determinar número de piezas

		int nPiezas = 0;
		for (vector<BoxType>::iterator bt = BoxTypes0.begin(); bt < BoxTypes0.end(); ++bt) {
			nPiezas += (*bt).cantidad;
		}

		// Generar bloques de una sola caja

		int N = nPiezas * maxNBloques;
		Bloques0 = vector<list<Bloque>>(1);
		for (vector<BoxType>::iterator bt = BoxTypes0.begin(); bt < BoxTypes0.end(); ++bt) {
			for (vector<Box>::iterator b = (*bt).boxes.begin(); b < (*bt).boxes.end(); ++b) {
				Bloques0.front().push_back(Bloque(&*b));
			}
		}

		// Generar bloques generales

		list<Bloque>::reverse_iterator desde = Bloques0.front().rbegin();
		list<Bloque>::reverse_iterator hasta = Bloques0.front().rend();
		bool seguir = true;
		while (seguir) {
			for (list<Bloque>::reverse_iterator it1 = Bloques0.front().rbegin(); it1 != Bloques0.front().rend(); ++it1) {
				for (list<Bloque>::reverse_iterator it2 = desde; it2 != hasta; ++it2) {
					for (int i = 1; i <= 6; ++i) {
						Bloque nuevoBloque(*it1, *it2, i);
						if (nuevoBloque.sirve) {

							// Verificar que el nuevo bloque no se haya adicionado antes

							bool agregar = true;
							for (list<Bloque>::reverse_iterator it3 = Bloques0.front().rbegin(); it3 != Bloques0.front().rend(); ++it3) {
								if ((*it3) == nuevoBloque) {
									agregar = false;
									break;
								}
							}
							if (agregar) {
								Bloques0.front().push_back(nuevoBloque);
								if (Bloques0.front().size() == N) {
									seguir = false;
									break;
								}
							}
						}
					}
					if (!seguir) break;
				}
				if (!seguir) break;
			}
			if (desde == Bloques0.front().rbegin()) {
				seguir = false;
			}
			else {
				hasta = desde;
				desde = Bloques0.front().rbegin();
			}
		}
	}
	else {
		Bloques0.reserve(BoxTypes0.back().cliente - BoxTypes0.front().cliente + 1);
		int clienteActual = BoxTypes0.front().cliente;
		vector<BoxType>::iterator ini = BoxTypes0.begin();
		vector<BoxType>::iterator fin = BoxTypes0.begin();
		bool clienteDiferente = false;
		for (vector<BoxType>::iterator it = BoxTypes0.begin(); it < BoxTypes0.end(); ++it) {
			if (it + 1 == BoxTypes0.end()) {
				clienteDiferente = true;
				fin = it + 1;
			}
			else if ((*(it + 1)).cliente != clienteActual) {
				clienteDiferente = true;
				fin = it + 1;
			}
			if (clienteDiferente) {

				// Determinar Número de bloques a generar

				int N = 0;
				for (vector<BoxType>::iterator bt = ini; bt < fin; ++bt) {
					N += (*bt).cantidad;
				}
				N *= maxNBloques;

				// Generar bloques de 1 sola caja

				Bloques0.push_back(list<Bloque>(0));
				for (vector<BoxType>::iterator bt = ini; bt < fin; ++bt) {
					for (vector<Box>::iterator b = (*bt).boxes.begin(); b < (*bt).boxes.end(); ++b) {
						Bloques0.back().push_back(Bloque(&*b));
					}
				}

				// Generar bloques generales

				list<Bloque>::reverse_iterator desde = Bloques0.back().rbegin();
				list<Bloque>::reverse_iterator hasta = Bloques0.back().rend();
				bool seguir = true;
				while (seguir) {
					for (list<Bloque>::reverse_iterator it1 = Bloques0.back().rbegin(); it1 != Bloques0.back().rend(); ++it1) {
						for (list<Bloque>::reverse_iterator it2 = desde; it2 != hasta; ++it2) {
							for (int i = 1; i <= 6; ++i) {
								Bloque nuevoBloque(*it1, *it2, i);
								if (nuevoBloque.sirve) {

									// Verificar que el nuevo bloque no se haya adicionado antes

									bool agregar = true;
									for (list<Bloque>::reverse_iterator it3 = Bloques0.back().rbegin(); it3 != Bloques0.back().rend(); ++it3) {
										if ((*it3) == nuevoBloque) {
											agregar = false;
											break;
										}
									}
									if (agregar) {

										Bloques0.back().push_back(nuevoBloque);
										if (Bloques0.back().size() == N) {
											seguir = false;
											break;
										}
									}
								}
							}
							if (!seguir) break;
						}
						if (!seguir) break;
					}
					if (desde == Bloques0.back().rbegin()) {
						seguir = false;
					}
					else {
						hasta = desde;
						desde = Bloques0.back().rbegin();
					}
				}

				// Actualización

				clienteDiferente = false;
				ini = fin;
				if (ini != BoxTypes0.end()) {
					clienteActual = (*ini).cliente;
				}
			}
		}
	}

	// Crear los apuntadores y Asignar índice del bloque a cada packed box para que al eliminar piezas se eliminen todas las del bloque

	Bloques00.reserve(Bloques0.size());
	int i = 0;
	for (vector<list<Bloque>>::iterator b1 = Bloques0.begin(); b1 < Bloques0.end(); ++b1) {
		Bloques00.push_back(list<Bloque*>(0));
		for (list<Bloque>::iterator b2 = (*b1).begin(); b2 != (*b1).end(); ++b2, ++i) {
			Bloques00.back().push_back(&*b2);
		}
	}
}
void UpdateTime() {
	auto miDuracion = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - tIni);
	duracion = (double)miDuracion.count() / 1000.0;
}
void WriteData(string const& ins, int const& repeticion, int const& totalIteraciones, int const& totalIterLS, int dur)
{
	// Tiempo

	UpdateTime();

	if (g_inMemoryMode) {
		g_outPiezas.clear();
		g_outUtilizacion = incumbente.utilizacion;
		g_outTotalIter = totalIteraciones;
		g_outTotalIterLS = totalIterLS;
		for (vector<PackedBox>::iterator p = incumbente.empacados.begin(); p < incumbente.empacados.end(); ++p) {
			int idReal = (*(*p).bt).idReal;
			PiezaColocadaMemoria pc;
			pc.idTexto = (idReal >= 0 && idReal < (int)g_idRealATexto.size()) ? g_idRealATexto[idReal] : to_string(idReal);
			pc.indiceEntrada = idReal;
			pc.x1 = (*p).x1; pc.y1 = (*p).y1; pc.z1 = (*p).z1;
			pc.x2 = (*p).x2; pc.y2 = (*p).y2; pc.z2 = (*p).z2;
			g_outPiezas.push_back(pc);
		}
		return;
	}
	//cout << incumbente.utilizacion << " " << incumbente.volEmpacado << endl;

	// Escritura del archivo

	string filePath = "Solutions/R_" + ins;
	filePath += "_MD" + to_string(r_multidrop);
	if (r_maxPresionItems) filePath += "_P1";
	else filePath += "_P0";
	filePath += "_S" + to_string(r_estabilidad);
	filePath += "_EM" + to_string(r_juntarEspacios);
	filePath += "_st_rep" + to_string(repeticion);
	filePath += "_d" + to_string(dur);
	filePath += "_bw" + to_string(beamWidth);
	filePath += ".txt";
	ofstream writer(filePath);
	writer << "Utilizacion " << to_string(incumbente.utilizacion * 100.0) << " VolCajasEmpacado " << to_string((double)incumbente.volEmpacado / (double)totalVolumen) << " Duracion[s] " << to_string(duracion) << " Iteraciones " << to_string(totalIteraciones) << " IteracionesMejora " << to_string(totalIterLS) << " Semilla " << to_string(semilla) << endl;
	int id = 0;
	for (vector<PackedBox>::iterator p = incumbente.empacados.begin(); p < incumbente.empacados.end(); ++p, ++id) {
		writer << to_string(id) << " " << to_string((*(*p).bt).idReal) << " " << to_string((*(*p).bt).cliente) << " " + to_string((*p).x1) << " " << to_string((*p).y1) << " " << to_string((*p).z1) << " " << to_string((*p).x2) << " " << to_string((*p).y2) << " " << to_string((*p).z2) << endl;
	}
	writer.close();
	//cout << "ok" << endl;
	//incumbente.Verificador(filePath);
}
vector<Alpha>::iterator DeterminarAlpha() {
	sort(alphas.begin(), alphas.end());
	double val = dist01(generator);
	vector<double>::iterator p_it = pesos_acum.begin();
	for (vector<Alpha>::iterator a = alphas.begin(); a < alphas.end(); ++a, ++p_it) {
		if (val < *p_it) {
			return a;
		}
	}
	return alphas.end() - 1;
}
void ActualizarAlpha(Alpha& a, double const& utilizacion) {
	a.Actualizar(utilizacion);
}

// Main

void Ciclo_BS(string const& ins, int const& maxTime, int const& repeticion) {

	// Inicialización de variables

	BoxTypes0.clear();
	cantidadesBoxTypes0.clear();
	Bloques0.clear();
	Bloques00.clear();

	alphas = vector<Alpha>({ Alpha(0.1), Alpha(0.2), Alpha(0.3), Alpha(0.4), Alpha(0.5), Alpha(0.6), Alpha(0.7), Alpha(0.8), Alpha(0.9), Alpha(1.0) });
	pesos_acum = vector<double>(alphas.size(), 0);
	tIni = chrono::high_resolution_clock::now();
	duracion = 0;
	semilla = chrono::system_clock::now().time_since_epoch().count();
	//semilla = 1750876027670184799;
	//cout << "semilla = " << to_string(semilla) << endl;
	generator.seed(semilla);
	int totalIter = 0;
	int totalIterLS = 0;

	// Leer datos

	ReadData2(ins);
	errorArea = 0.5 / (double)(ContenedorDimx * ContenedorDimy);
	HacerBloquesGenerales();
	C0 = Container();
	incumbente = Container(C0, false);
	double totalPesoAcum = 0;
	int j = 1;
	for (vector<double>::iterator p_it = pesos_acum.begin(); p_it < pesos_acum.end(); ++p_it, ++j) {
		(*p_it) = 1.0 / (double)j;
		totalPesoAcum += *p_it;
	}
	for (vector<double>::iterator p_it = pesos_acum.begin(); p_it < pesos_acum.end(); ++p_it) {
		(*p_it) /= totalPesoAcum;
	}
	for (vector<double>::iterator p_it = pesos_acum.begin() + 1; p_it < pesos_acum.end(); ++p_it) {
		(*p_it) += *(p_it - 1);
	}

	// Volumen total

	totalVolumen = 0;
	for (vector<BoxType>::iterator b_it = BoxTypes0.begin(); b_it < BoxTypes0.end(); ++b_it) {
		totalVolumen += (*b_it).cantidad * (*b_it).volumenReal;
	}
	totalVolumen /= 100.0;

	// Adquisición de datos iniciales

	if (r_multidrop > 0) {
		for (vector<Alpha>::iterator a = alphas.begin(); a < alphas.end() && incumbente.hayCajasPorEmpacar && duracion < maxTime; ++a) {
			++totalIter;
			Container C(C0, true);
			bool bl = false;
			C.alpha = (*a).val;
			C.Constructivo_Multidrop();
			ActualizarAlpha(*a, C.utilizacion);
			double porcentaje = -0.3 + 1.0 - 0.6 * duracion / (double)maxTime;
			if (C.utilizacion > incumbente.utilizacion + epsilon) {
				incumbente = Container(C, false);
				if (!incumbente.hayCajasPorEmpacar) break;
				bl = true;
			}
			pair<int, double> mi = incumbente.MetricaIncumbente(porcentaje);
			pair<double, int> mni = C.MetricaNoIncumente(mi.first);
			if (bl || mi.second <= mni.first) {
				++totalIterLS;

				// Búsqueda local

				C.RemoverPiezas(mni.second);
				list<Container> Contenedores1(1, C);
				list<Container> Contenedores2;

				// Ciclo de búsqueda local

				while (Contenedores1.size() > 0 && duracion < maxTime && incumbente.hayCajasPorEmpacar) {

					// Generar Opciones bw
					
					Contenedores2.clear();
					int desde = 0;
					for (list<Container>::iterator c = Contenedores1.begin(); c != Contenedores1.end() && incumbente.hayCajasPorEmpacar; ++c) {
						Contenedores2.splice(Contenedores2.end(), (*c).ObtenerOpcionesBeam_Multidrop(&*c));
						
						// LLevar hasta el final

						for (list<Container>::iterator it = next(Contenedores2.begin(), desde); it != Contenedores2.end() && incumbente.hayCajasPorEmpacar; ++it) {
							Container newC(*it, true);
							newC.Constructivo_MultidropFast();
							(*it).utilizacionBW = newC.utilizacion;
							if (newC.utilizacion > incumbente.utilizacion) {
								incumbente = Container(newC, false);
							}
						}
						desde = Contenedores2.size();
					}

					// ordenar y seleccionar mejores bw contenedores

					Contenedores1.clear();
					if (incumbente.hayCajasPorEmpacar && Contenedores2.size() > 0) {
						Contenedores2.sort([](Container const& c1, Container const& c2)->bool {return c1.utilizacionBW > c2.utilizacionBW; });
						if (Contenedores2.size() > beamWidth) Contenedores2.resize(beamWidth);

						// Generar Opciones bw
						
						desde = 0;
						for (list<Container>::iterator c = Contenedores2.begin(); c != Contenedores2.end() && incumbente.hayCajasPorEmpacar; ++c) {
							Contenedores1.splice(Contenedores1.end(), (*c).ObtenerOpcionesBeam_Multidrop(&*c));

							// LLevar hasta el final

							for (list<Container>::iterator it = next(Contenedores1.begin(), desde); it != Contenedores1.end() && incumbente.hayCajasPorEmpacar; ++it) {
								Container newC(*it, true);
								newC.Constructivo_MultidropFast();
								(*it).utilizacionBW = newC.utilizacion;
								if (newC.utilizacion > incumbente.utilizacion) {
									incumbente = Container(newC, false);
								}
							}
							desde = Contenedores1.size();
						}

						// Ordenar y seleccionar mejores bw contenedores

						Contenedores1.sort([](Container const& c1, Container const& c2)->bool {return c1.utilizacionBW > c2.utilizacionBW; });
						if (Contenedores1.size() > beamWidth) Contenedores1.resize(beamWidth);
					}

					// Actualizar tiempo

					UpdateTime();
				}
			}

			// Parar por tiempo

			UpdateTime();
		}
	}
	else {
		for (vector<Alpha>::iterator a = alphas.begin(); a < alphas.end() && incumbente.hayCajasPorEmpacar && duracion < maxTime; ++a) {
			++totalIter;
			Container C(C0, true);
			bool bl = false;
			C.alpha = (*a).val;
			C.Constructivo();
			ActualizarAlpha(*a, C.utilizacion);
			double porcentaje = -0.3 + 1.0 - 0.6 * duracion / (double)maxTime;
			if (C.utilizacion > incumbente.utilizacion + epsilon) {
				incumbente = Container(C, false);
				if (!incumbente.hayCajasPorEmpacar) break;
				bl = true;
			}
			pair<int, double> mi = incumbente.MetricaIncumbente(porcentaje);
			pair<double, int> mni = C.MetricaNoIncumente(mi.first);
			if (bl || mi.second <= mni.first) {
				++totalIterLS;

				// Búsqueda local

				C.RemoverPiezas(mni.second);
				list<Container> Contenedores1(1, C);
				list<Container> Contenedores2;

				// Ciclo de búsqueda local

				while (Contenedores1.size() > 0 && duracion < maxTime && incumbente.hayCajasPorEmpacar) {

					// Generar Opciones bw

					Contenedores2.clear();
					int desde = 0;
					for (list<Container>::iterator c = Contenedores1.begin(); c != Contenedores1.end() && incumbente.hayCajasPorEmpacar; ++c) {
						Contenedores2.splice(Contenedores2.end(), (*c).ObtenerOpcionesBeam(&*c));

						// LLevar hasta el final

						for (list<Container>::iterator it = next(Contenedores2.begin(), desde); it != Contenedores2.end() && incumbente.hayCajasPorEmpacar; ++it) {
							Container newC(*it, true);
							newC.ConstructivoFast();
							(*it).utilizacionBW = newC.utilizacion;
							if (newC.utilizacion > incumbente.utilizacion) {
								incumbente = Container(newC, false);
							}
						}
						desde = Contenedores2.size();
					}

					// ordenar y seleccionar mejores bw contenedores

					Contenedores1.clear();
					if (incumbente.hayCajasPorEmpacar && Contenedores2.size() > 0) {
						Contenedores2.sort([](Container const& c1, Container const& c2)->bool {return c1.utilizacionBW > c2.utilizacionBW; });
						if (Contenedores2.size() > beamWidth) Contenedores2.resize(beamWidth);

						// Generar Opciones bw

						desde = 0;
						for (list<Container>::iterator c = Contenedores2.begin(); c != Contenedores2.end() && incumbente.hayCajasPorEmpacar; ++c) {
							Contenedores1.splice(Contenedores1.end(), (*c).ObtenerOpcionesBeam(&*c));

							// LLevar hasta el final

							for (list<Container>::iterator it = next(Contenedores1.begin(), desde); it != Contenedores1.end() && incumbente.hayCajasPorEmpacar; ++it) {
								Container newC(*it, true);
								newC.ConstructivoFast();
								(*it).utilizacionBW = newC.utilizacion;
								if (newC.utilizacion > incumbente.utilizacion) {
									incumbente = Container(newC, false);
								}
							}
							desde = Contenedores1.size();
						}

						// Ordenar y seleccionar mejores bw contenedores

						Contenedores1.sort([](Container const& c1, Container const& c2)->bool {return c1.utilizacionBW > c2.utilizacionBW; });
						if (Contenedores1.size() > beamWidth) Contenedores1.resize(beamWidth);
					}

					// Actualizar tiempo

					UpdateTime();
				}
			}

			// Parar por tiempo

			UpdateTime();
		}
	}

	// Resto de la metaheurística

	UpdateTime();
	if (duracion >= maxTime || !incumbente.hayCajasPorEmpacar) {
		WriteData(ins, repeticion, totalIter, totalIterLS, maxTime);
		return;
	}
	if (r_multidrop > 0) {
		while (duracion < maxTime && incumbente.hayCajasPorEmpacar) {
			++totalIter;
			Container C(C0, true);
			bool bl = false;
			vector<Alpha>::iterator a = DeterminarAlpha();
			C.alpha = (*a).val;
			C.Constructivo_Multidrop();
			ActualizarAlpha(*a, C.utilizacion);
			double porcentaje = -0.3 + 1.0 - 0.6 * duracion / (double)maxTime;
			if (C.utilizacion > incumbente.utilizacion + epsilon) {
				incumbente = Container(C, false);
				if (!incumbente.hayCajasPorEmpacar) break;
				bl = true;
			}
			pair<int, double> mi = incumbente.MetricaIncumbente(porcentaje);
			pair<double, int> mni = C.MetricaNoIncumente(mi.first);
			if (bl || mi.second <= mni.first) {
				++totalIterLS;

				// Búsqueda local

				C.RemoverPiezas(mni.second);
				list<Container> Contenedores1(1, C);
				list<Container> Contenedores2;

				// Ciclo de búsqueda local

				while (Contenedores1.size() > 0 && duracion < maxTime && incumbente.hayCajasPorEmpacar) {

					// Generar Opciones bw

					Contenedores2.clear();
					int desde = 0;
					for (list<Container>::iterator c = Contenedores1.begin(); c != Contenedores1.end() && incumbente.hayCajasPorEmpacar; ++c) {
						Contenedores2.splice(Contenedores2.end(), (*c).ObtenerOpcionesBeam_Multidrop(&*c));

						// LLevar hasta el final

						for (list<Container>::iterator it = next(Contenedores2.begin(), desde); it != Contenedores2.end() && incumbente.hayCajasPorEmpacar; ++it) {
							Container newC(*it, true);
							newC.Constructivo_MultidropFast();
							(*it).utilizacionBW = newC.utilizacion;
							if (newC.utilizacion > incumbente.utilizacion) {
								incumbente = Container(newC, false);
							}
						}
						desde = Contenedores2.size();
					}

					// ordenar y seleccionar mejores bw contenedores

					Contenedores1.clear();
					if (incumbente.hayCajasPorEmpacar && Contenedores2.size() > 0) {
						Contenedores2.sort([](Container const& c1, Container const& c2)->bool {return c1.utilizacionBW > c2.utilizacionBW; });
						if (Contenedores2.size() > beamWidth) Contenedores2.resize(beamWidth);

						// Generar Opciones bw

						desde = 0;
						for (list<Container>::iterator c = Contenedores2.begin(); c != Contenedores2.end() && incumbente.hayCajasPorEmpacar; ++c) {
							Contenedores1.splice(Contenedores1.end(), (*c).ObtenerOpcionesBeam_Multidrop(&*c));

							// LLevar hasta el final

							for (list<Container>::iterator it = next(Contenedores1.begin(), desde); it != Contenedores1.end() && incumbente.hayCajasPorEmpacar; ++it) {
								Container newC(*it, true);
								newC.Constructivo_MultidropFast();
								(*it).utilizacionBW = newC.utilizacion;
								if (newC.utilizacion > incumbente.utilizacion) {
									incumbente = Container(newC, false);
								}
							}
							desde = Contenedores1.size();
						}

						// Ordenar y seleccionar mejores bw contenedores

						Contenedores1.sort([](Container const& c1, Container const& c2)->bool {return c1.utilizacionBW > c2.utilizacionBW; });
						if (Contenedores1.size() > beamWidth) Contenedores1.resize(beamWidth);
					}

					// Actualizar tiempo

					UpdateTime();
				}
			}

			// Parar por tiempo

			UpdateTime();
		}
	}
	else {
		while (duracion < maxTime && incumbente.hayCajasPorEmpacar) {
			++totalIter;
			Container C(C0, true);
			bool bl = false;
			vector<Alpha>::iterator a = DeterminarAlpha();
			C.alpha = (*a).val;
			C.Constructivo();
			ActualizarAlpha(*a, C.utilizacion);
			double porcentaje = -0.3 + 1.0 - 0.6 * duracion / (double)maxTime;
			if (C.utilizacion > incumbente.utilizacion + epsilon) {
				incumbente = Container(C, false);
				if (!incumbente.hayCajasPorEmpacar) break;
				bl = true;
			}
			pair<int, double> mi = incumbente.MetricaIncumbente(porcentaje);
			pair<double, int> mni = C.MetricaNoIncumente(mi.first);
			if (bl || mi.second <= mni.first) {
				++totalIterLS;

				// Búsqueda local

				C.RemoverPiezas(mni.second);
				list<Container> Contenedores1(1, C);
				list<Container> Contenedores2;

				// Ciclo de búsqueda local

				while (Contenedores1.size() > 0 && duracion < maxTime && incumbente.hayCajasPorEmpacar) {

					// Generar Opciones bw

					Contenedores2.clear();
					int desde = 0;
					for (list<Container>::iterator c = Contenedores1.begin(); c != Contenedores1.end() && incumbente.hayCajasPorEmpacar; ++c) {
						Contenedores2.splice(Contenedores2.end(), (*c).ObtenerOpcionesBeam(&*c));

						// LLevar hasta el final

						for (list<Container>::iterator it = next(Contenedores2.begin(), desde); it != Contenedores2.end() && incumbente.hayCajasPorEmpacar; ++it) {
							Container newC(*it, true);
							newC.ConstructivoFast();
							(*it).utilizacionBW = newC.utilizacion;
							if (newC.utilizacion > incumbente.utilizacion) {
								incumbente = Container(newC, false);
							}
						}
						desde = Contenedores2.size();
					}

					// ordenar y seleccionar mejores bw contenedores

					Contenedores1.clear();
					if (incumbente.hayCajasPorEmpacar && Contenedores2.size() > 0) {
						Contenedores2.sort([](Container const& c1, Container const& c2)->bool {return c1.utilizacionBW > c2.utilizacionBW; });
						if (Contenedores2.size() > beamWidth) Contenedores2.resize(beamWidth);

						// Generar Opciones bw

						desde = 0;
						for (list<Container>::iterator c = Contenedores2.begin(); c != Contenedores2.end() && incumbente.hayCajasPorEmpacar; ++c) {
							Contenedores1.splice(Contenedores1.end(), (*c).ObtenerOpcionesBeam(&*c));

							// LLevar hasta el final

							for (list<Container>::iterator it = next(Contenedores1.begin(), desde); it != Contenedores1.end() && incumbente.hayCajasPorEmpacar; ++it) {
								Container newC(*it, true);
								newC.ConstructivoFast();
								(*it).utilizacionBW = newC.utilizacion;
								if (newC.utilizacion > incumbente.utilizacion) {
									incumbente = Container(newC, false);
								}
							}
							desde = Contenedores1.size();
						}

						// Ordenar y seleccionar mejores bw contenedores

						Contenedores1.sort([](Container const& c1, Container const& c2)->bool {return c1.utilizacionBW > c2.utilizacionBW; });
						if (Contenedores1.size() > beamWidth) Contenedores1.resize(beamWidth);
					}

					// Actualizar tiempo

					UpdateTime();
				}
			}

			// Parar por tiempo

			UpdateTime();
		}
	}

	// Escribir resultados

	WriteData(ins, repeticion, totalIter, totalIterLS, maxTime);
}
void Ciclo_LS(string const& ins, int const& maxTime, int const& repeticion) {

	// Inicialización de variables

	BoxTypes0.clear();
	cantidadesBoxTypes0.clear();
	Bloques0.clear();
	Bloques00.clear();

	alphas = vector<Alpha>({ Alpha(0.1), Alpha(0.2), Alpha(0.3), Alpha(0.4), Alpha(0.5), Alpha(0.6), Alpha(0.7), Alpha(0.8), Alpha(0.9), Alpha(1.0) });
	pesos_acum = vector<double>(alphas.size(), 0);
	tIni = chrono::high_resolution_clock::now();
	duracion = 0;
	semilla = chrono::system_clock::now().time_since_epoch().count();
	//semilla = 17411168985731814;
	//cout << "semilla = " << to_string(semilla) << endl;
	generator.seed(semilla);
	int totalIter = 0;
	int totalIterLS = 0;

	// Leer datos

	ReadData2(ins);
	errorArea = 0.5 / (double)(ContenedorDimx * ContenedorDimy);
	HacerBloquesGenerales();
	C0 = Container();
	incumbente = Container(C0, false);
	double totalPesoAcum = 0;
	int j = 1;
	for (vector<double>::iterator p_it = pesos_acum.begin(); p_it < pesos_acum.end(); ++p_it, ++j) {
		(*p_it) = 1.0 / (double)j;
		totalPesoAcum += *p_it;
	}
	for (vector<double>::iterator p_it = pesos_acum.begin(); p_it < pesos_acum.end(); ++p_it) {
		(*p_it) /= totalPesoAcum;
	}
	for (vector<double>::iterator p_it = pesos_acum.begin() + 1; p_it < pesos_acum.end(); ++p_it) {
		(*p_it) += *(p_it - 1);
	}

	// Volumen total

	totalVolumen = 0;
	for (vector<BoxType>::iterator b_it = BoxTypes0.begin(); b_it < BoxTypes0.end(); ++b_it) {
		totalVolumen += (*b_it).cantidad * (*b_it).volumenReal;
	}
	totalVolumen /= 100.0;

	// Adquisición de datos iniciales

	if (r_multidrop > 0) {
		for (vector<Alpha>::iterator a = alphas.begin(); a < alphas.end() && incumbente.hayCajasPorEmpacar && duracion < maxTime; ++a) {
			++totalIter;
			Container C(C0, true);
			bool bl = false;
			C.alpha = (*a).val;
			C.Constructivo_Multidrop();
			ActualizarAlpha(*a, C.utilizacion);
			double porcentaje = -0.3 + 1.0 - 0.6 * duracion / (double)maxTime;
			if (C.utilizacion > incumbente.utilizacion + epsilon) {
				incumbente = Container(C, false);
				if (!incumbente.hayCajasPorEmpacar) break;
				bl = true;
			}
			pair<int, double> mi = incumbente.MetricaIncumbente(porcentaje);
			pair<double, int> mni = C.MetricaNoIncumente(mi.first);
			if (bl || mi.second <= mni.first) {
				++totalIterLS;

				// Búsqueda local

				C.RemoverPiezas(mni.second);
				C.Constructivo_Multidrop_Deterministico();
				if (C.utilizacion > incumbente.utilizacion + epsilon) {
					incumbente = Container(C, false);
				}
			}

			// Parar por tiempo

			UpdateTime();
		}
	}
	else {
		for (vector<Alpha>::iterator a = alphas.begin(); a < alphas.end() && incumbente.hayCajasPorEmpacar && duracion < maxTime; ++a) {
			++totalIter;
			Container C(C0, true);
			bool bl = false;
			C.alpha = (*a).val;
			C.Constructivo();
			ActualizarAlpha(*a, C.utilizacion);
			double porcentaje = -0.3 + 1.0 - 0.6 * duracion / (double)maxTime;
			if (C.utilizacion > incumbente.utilizacion + epsilon) {
				incumbente = Container(C, false);
				if (!incumbente.hayCajasPorEmpacar) break;
				bl = true;
			}
			pair<int, double> mi = incumbente.MetricaIncumbente(porcentaje);
			pair<double, int> mni = C.MetricaNoIncumente(mi.first);
			if (bl || mi.second <= mni.first) {
				++totalIterLS;

				// Búsqueda local

				C.RemoverPiezas(mni.second);
				C.Constructivo_Deterministico();
				if (C.utilizacion > incumbente.utilizacion + epsilon) {
					incumbente = Container(C, false);
				}
			}

			// Parar por tiempo

			UpdateTime();
		}
	}

	// Resto de la metaheurística

	UpdateTime();
	if (duracion >= maxTime || !incumbente.hayCajasPorEmpacar) {
		WriteData(ins, repeticion, totalIter, totalIterLS, maxTime);
		return;
	}
	if (r_multidrop > 0) {
		while (duracion < maxTime && incumbente.hayCajasPorEmpacar) {
			++totalIter;
			Container C(C0, true);
			bool bl = false;
			vector<Alpha>::iterator a = DeterminarAlpha();
			C.alpha = (*a).val;
			C.Constructivo_Multidrop();
			ActualizarAlpha(*a, C.utilizacion);
			double porcentaje = -0.3 + 1.0 - 0.6 * duracion / (double)maxTime;
			if (C.utilizacion > incumbente.utilizacion + epsilon) {
				incumbente = Container(C, false);
				if (!incumbente.hayCajasPorEmpacar) break;
				bl = true;
			}
			pair<int, double> mi = incumbente.MetricaIncumbente(porcentaje);
			pair<double, int> mni = C.MetricaNoIncumente(mi.first);
			if (bl || mi.second <= mni.first) {
				++totalIterLS;

				// Búsqueda local

				C.RemoverPiezas(mni.second);
				C.Constructivo_Multidrop_Deterministico();
				if (C.utilizacion > incumbente.utilizacion + epsilon) {
					incumbente = Container(C, false);
				}
			}

			// Parar por tiempo

			UpdateTime();
		}
	}
	else {
		while (duracion < maxTime && incumbente.hayCajasPorEmpacar) {
			++totalIter;
			Container C(C0, true);
			bool bl = false;
			vector<Alpha>::iterator a = DeterminarAlpha();
			C.alpha = (*a).val;
			C.Constructivo();
			ActualizarAlpha(*a, C.utilizacion);
			double porcentaje = -0.3 + 1.0 - 0.6 * duracion / (double)maxTime;
			if (C.utilizacion > incumbente.utilizacion + epsilon) {
				incumbente = Container(C, false);
				if (!incumbente.hayCajasPorEmpacar) break;
				bl = true;
			}
			pair<int, double> mi = incumbente.MetricaIncumbente(porcentaje);
			pair<double, int> mni = C.MetricaNoIncumente(mi.first);
			if (bl || mi.second <= mni.first) {
				++totalIterLS;

				// Búsqueda local

				C.RemoverPiezas(mni.second);
				C.Constructivo_Deterministico();
				if (C.utilizacion > incumbente.utilizacion + epsilon) {
					incumbente = Container(C, false);
				}
			}

			// Parar por tiempo

			UpdateTime();
		}
	}

	// Escribir resultados

	WriteData(ins, repeticion, totalIter, totalIterLS, maxTime);
}
void Ciclo_Constructivo(string const& ins, int const& maxTime, int const& repeticion) {

	// Inicialización de variables

	BoxTypes0.clear();
	cantidadesBoxTypes0.clear();
	Bloques0.clear();
	Bloques00.clear();

	tIni = chrono::high_resolution_clock::now();
	duracion = 0;
	semilla = chrono::system_clock::now().time_since_epoch().count();
	//semilla = 17499173553437967;
	//cout << "semilla = " << to_string(semilla) << endl;
	generator.seed(semilla);
	int totalIter = 0;
	int totalIterLS = 0;

	// Leer datos

	ReadData2(ins);
	errorArea = 0.5 / (double)(ContenedorDimx * ContenedorDimy);
	HacerBloquesGenerales();
	C0 = Container();
	incumbente = Container(C0, false);

	// Volumen total

	totalVolumen = 0;
	for (vector<BoxType>::iterator b_it = BoxTypes0.begin(); b_it < BoxTypes0.end(); ++b_it) {
		totalVolumen += (*b_it).cantidad * (*b_it).volumenReal;
	}
	totalVolumen /= 100.0;

	// Ciclo de constructivo

	if (r_multidrop > 0) {
		while (duracion < maxTime && incumbente.hayCajasPorEmpacar) {
			++totalIter;
			Container C(C0, true);
			C.Constructivo_Multidrop();
			//C.Constructivo_MultidropFast();
			if (C.utilizacion > incumbente.utilizacion + epsilon) {
				incumbente = Container(C, false);
			}

			// Parar por tiempo

			UpdateTime();
		}
	}
	else {
		while (duracion < maxTime && incumbente.hayCajasPorEmpacar) {
			++totalIter;
			Container C(C0, true);
			C.Constructivo();
			//C.ConstructivoFast();
			if (C.utilizacion > incumbente.utilizacion + epsilon) {
				incumbente = Container(C, false);
			}

			// Parar por tiempo

			UpdateTime();
		}
	}

	// Escribir resultados

	WriteData(ins, repeticion, totalIter, totalIterLS, maxTime);
}

// ===== Punto de entrada in-memory (fase 1) =====
// Reusa Ciclo_BS tal cual; el modo in-memory hace que internamente use los parches
// de ReadData2/WriteData en vez de archivos. No hay E/S a disco en esta ruta.
vector<PiezaColocadaMemoria> SolveCubicajeInMemory(
	int contDimx, int contDimy, int contDimz,
	vector<PiezaEntradaMemoria> const& piezas,
	int maxTime)
{
	g_inMemoryMode = true;
	g_inContDimx = contDimx;
	g_inContDimy = contDimy;
	g_inContDimz = contDimz;
	g_inPiezas = piezas;

	Ciclo_BS("in_memory", maxTime, 1);

	g_inMemoryMode = false;
	return g_outPiezas;
}

// ===== API C exportada (fase 2) =====
// Ver CubicajeApi.h. Traduce los structs planos a los vectores de SolveCubicajeInMemory
// y de vuelta, para que Unity no tenga que lidiar con strings ni con std::vector.

double g_apiUltimaUtilizacion = 0;

// Revisa que los parámetros de entrada tengan sentido antes de tocar el motor.
static bool ParametrosValidos(int contDimx, int contDimy, int contDimz,
	const CubicajePiezaEntrada* piezas, int numPiezas,
	int maxTimeSeg, CubicajePiezaColocada* salida, int capacidadSalida)
{
	if (contDimx <= 0 || contDimy <= 0 || contDimz <= 0) return false;
	if (numPiezas < 0 || capacidadSalida < 0 || maxTimeSeg < 0) return false;
	if (numPiezas > 0 && (piezas == nullptr || salida == nullptr)) return false;
	for (int i = 0; i < numPiezas; ++i) {
		if (piezas[i].dimx <= 0 || piezas[i].dimy <= 0 || piezas[i].dimz <= 0) return false;
	}
	return true;
}

// Convierte el arreglo plano de entrada al formato del wrapper in-memory.
// El idTexto es el índice como texto: solo informativo, la API devuelve indiceEntrada.
static vector<PiezaEntradaMemoria> ConvertirEntrada(const CubicajePiezaEntrada* piezas, int numPiezas)
{
	vector<PiezaEntradaMemoria> entrada;
	entrada.reserve(numPiezas);
	for (int i = 0; i < numPiezas; ++i) {
		PiezaEntradaMemoria p;
		p.idTexto = to_string(i);
		p.dimx = piezas[i].dimx;
		p.dimy = piezas[i].dimy;
		p.dimz = piezas[i].dimz;
		entrada.push_back(p);
	}
	return entrada;
}

// Copia el resultado del motor al buffer de salida que reservó el llamador.
static void CopiarSalida(vector<PiezaColocadaMemoria> const& resultado, CubicajePiezaColocada* salida)
{
	for (size_t i = 0; i < resultado.size(); ++i) {
		salida[i].indicePieza = resultado[i].indiceEntrada;
		salida[i].x1 = resultado[i].x1; salida[i].y1 = resultado[i].y1; salida[i].z1 = resultado[i].z1;
		salida[i].x2 = resultado[i].x2; salida[i].y2 = resultado[i].y2; salida[i].z2 = resultado[i].z2;
	}
}

// Punto de entrada principal para Unity. Nunca deja escapar una excepción C++
// a través de la frontera C (eso tumbaría la app en HoloLens).
CUBICAJE_API int Cubicaje_Resolver(
	int contDimx, int contDimy, int contDimz,
	const CubicajePiezaEntrada* piezas, int numPiezas,
	int maxTimeSeg,
	CubicajePiezaColocada* salida, int capacidadSalida)
{
	g_apiUltimaUtilizacion = 0;
	if (!ParametrosValidos(contDimx, contDimy, contDimz, piezas, numPiezas, maxTimeSeg, salida, capacidadSalida))
		return CUBICAJE_ERROR_PARAMETROS;
	if (capacidadSalida < numPiezas)
		return CUBICAJE_ERROR_CAPACIDAD; // se rechaza antes de correr el motor, no después
	if (numPiezas == 0)
		return 0;

	try {
		vector<PiezaColocadaMemoria> resultado = SolveCubicajeInMemory(
			contDimx, contDimy, contDimz, ConvertirEntrada(piezas, numPiezas), maxTimeSeg);

		CopiarSalida(resultado, salida);
		g_apiUltimaUtilizacion = g_outUtilizacion;
		return (int)resultado.size();
	}
	catch (...) {
		g_inMemoryMode = false; // dejar el motor en modo archivo como estaba
		return CUBICAJE_ERROR_EXCEPCION;
	}
}

// Utilización de la última llamada exitosa, para loguear la calidad del packing en Unity.
CUBICAJE_API double Cubicaje_UltimaUtilizacion(void)
{
	return g_apiUltimaUtilizacion;
}

#ifndef CUBICAJE_DLL
// Prueba de la API C con el caso 205³ (6×A + 6×B), con el alto en dimz (z = vertical en el motor).
// Valida la traducción de índices y los códigos de retorno sin necesidad de Unity.
static int ProbarApiC(int maxTimeSeg)
{
	const char* ids[] = { "A1", "A2", "A3", "A4", "A5", "A6", "B1", "B2", "B3", "B4", "B5", "B6" };
	CubicajePiezaEntrada piezas[12];
	for (int i = 0; i < 6; ++i)  piezas[i] = { 40, 80, 165 };
	for (int i = 6; i < 12; ++i) piezas[i] = { 80, 80, 120 };
	CubicajePiezaColocada salida[12];

	int colocadas = Cubicaje_Resolver(205, 205, 205, piezas, 12, maxTimeSeg, salida, 12);
	cout << "=== testCApi maxTime=" << maxTimeSeg << "s  retorno=" << colocadas
		 << "  utilizacion=" << (Cubicaje_UltimaUtilizacion() * 100.0) << "% ===" << endl;
	for (int i = 0; i < colocadas; ++i) {
		CubicajePiezaColocada const& c = salida[i];
		cout << "  " << ids[c.indicePieza] << " (idx " << c.indicePieza << ") -> (" << c.x1 << "," << c.y1 << "," << c.z1
			 << ") - (" << c.x2 << "," << c.y2 << "," << c.z2 << ")" << endl;
	}

	// Casos de error: deben devolver códigos negativos sin tocar el motor.
	cout << "  parametros nulos -> " << Cubicaje_Resolver(205, 205, 205, nullptr, 12, maxTimeSeg, salida, 12) << endl;
	cout << "  capacidad 1      -> " << Cubicaje_Resolver(205, 205, 205, piezas, 12, maxTimeSeg, salida, 1) << endl;
	cout << "  cero piezas      -> " << Cubicaje_Resolver(205, 205, 205, piezas, 0, maxTimeSeg, salida, 12) << endl;
	return 0;
}

int main(int argc, char** argv) {

	// Prueba de la API C exportada (fase 2).
	// Uso: Intento14.exe -testCApi [maxTime]
	if (argc >= 2 && string(argv[1]) == "-testCApi") {
		return ProbarApiC((argc >= 3) ? atoi(argv[2]) : 3);
	}

	// Smoke test temporal del wrapper in-memory (fase 1) — no toca el flujo normal por archivos.
	// Uso: Intento14.exe -testInMemory [maxTime1 maxTime2 ...]
	if (argc >= 2 && string(argv[1]) == "-testInMemory") {
		// Convención Unity: dimx=ancho(X), dimy=alto(Y), dimz=largo/fondo(Z)
		vector<PiezaEntradaMemoria> piezas = {
			{"A1", 40, 165, 80}, {"A2", 40, 165, 80}, {"A3", 40, 165, 80},
			{"A4", 40, 165, 80}, {"A5", 40, 165, 80}, {"A6", 40, 165, 80},
			{"B1", 80, 120, 80}, {"B2", 80, 120, 80}, {"B3", 80, 120, 80},
			{"B4", 80, 120, 80}, {"B5", 80, 120, 80}, {"B6", 80, 120, 80}
		};

		vector<int> tiempos;
		for (int i = 2; i < argc; ++i) tiempos.push_back(atoi(argv[i]));
		if (tiempos.empty()) tiempos = { 1, 3, 5 };

		for (vector<int>::iterator t = tiempos.begin(); t != tiempos.end(); ++t) {
			vector<PiezaColocadaMemoria> resultado = SolveCubicajeInMemory(205, 205, 205, piezas, *t);
			cout << "=== maxTime=" << *t << "s  duracion_real=" << duracion << "s  utilizacion="
				 << (g_outUtilizacion * 100.0) << "%  colocadas=" << resultado.size() << "/" << piezas.size()
				 << " ===" << endl;
			for (vector<PiezaColocadaMemoria>::iterator r = resultado.begin(); r != resultado.end(); ++r) {
				cout << "  " << (*r).idTexto << " -> (" << (*r).x1 << "," << (*r).y1 << "," << (*r).z1 << ") - ("
					 << (*r).x2 << "," << (*r).y2 << "," << (*r).z2 << ")" << endl;
			}
		}
		return 0;
	}

	// Experimento: impacto de beamWidth en velocidad (iter/seg) y en si se llega al óptimo (12/12).
	// Uso: Intento14.exe -testBeamWidth [maxTimeFijo] [bw1 bw2 ...]
	if (argc >= 2 && string(argv[1]) == "-testBeamWidth") {
		vector<PiezaEntradaMemoria> piezas = {
			{"A1", 40, 165, 80}, {"A2", 40, 165, 80}, {"A3", 40, 165, 80},
			{"A4", 40, 165, 80}, {"A5", 40, 165, 80}, {"A6", 40, 165, 80},
			{"B1", 80, 120, 80}, {"B2", 80, 120, 80}, {"B3", 80, 120, 80},
			{"B4", 80, 120, 80}, {"B5", 80, 120, 80}, {"B6", 80, 120, 80}
		};

		int maxTimeFijo = (argc >= 3) ? atoi(argv[2]) : 10;

		vector<int> anchos;
		for (int i = 3; i < argc; ++i) anchos.push_back(atoi(argv[i]));
		if (anchos.empty()) anchos = { 5, 10, 20, 40, 80 };

		for (vector<int>::iterator bw = anchos.begin(); bw != anchos.end(); ++bw) {
			beamWidth = *bw;
			vector<PiezaColocadaMemoria> resultado = SolveCubicajeInMemory(205, 205, 205, piezas, maxTimeFijo);
			double iterPorSeg = (duracion > 0) ? (g_outTotalIter / duracion) : 0.0;
			cout << "beamWidth=" << *bw << "  maxTime=" << maxTimeFijo << "s  duracion_real=" << duracion
				 << "s  utilizacion=" << (g_outUtilizacion * 100.0) << "%  colocadas=" << resultado.size()
				 << "/" << piezas.size() << "  totalIter=" << g_outTotalIter << "  totalIterLS=" << g_outTotalIterLS
				 << "  iter/seg=" << iterPorSeg << endl;
		}
		return 0;
	}

	// Parámetros

	string ins = "";
	int maxTime = 60;
	int repeticiones = 1;
	int insIni = -1;
	int insFin = -1;
	for (int i = 1; i < argc - 1; i += 2) {
		if (argc - 1 >= i + 1) {
			if (string(argv[i]) == "-ins") ins = argv[i + 1];
			else if (string(argv[i]) == "-rep") repeticiones = atoi(argv[i + 1]);
			else if (string(argv[i]) == "-bw") beamWidth = atoi(argv[i + 1]);
			else if (string(argv[i]) == "-maxTime") maxTime = atoi(argv[i + 1]);
			else if (string(argv[i]) == "-estabilidad") r_estabilidad = atoi(argv[i + 1]);
			else if (string(argv[i]) == "-juntar") r_juntarEspacios = atoi(argv[i + 1]);
			else if (string(argv[i]) == "-ini") insIni = atoi(argv[i + 1]);
			else if (string(argv[i]) == "-fin") insFin = atoi(argv[i + 1]);
			else if (string(argv[i]) == "-presion") {
				if (string(argv[i + 1]) == "1") r_maxPresionItems = true;
				else r_maxPresionItems = false;
			}
			else if (string(argv[i]) == "-multidrop") r_multidrop = atoi(argv[i + 1]);
			else {
				cout << "Mal en par�metros" << endl;
				return 0;
			}
		}
	}

	// ciclo de repeticiones

	for (int j = insIni; j <= insFin; ++j) {
		string ins1 = ins;
		if (j != -1) {
			size_t pos = ins1.find("__");
			ins1.replace(pos, 2, "_" + to_string(j) + "_");
		}
		for (int i = 1; i <= repeticiones; ++i) {
			Ciclo_BS(ins1, maxTime, i);
			//Ciclo_LS(ins1, maxTime, i);
			//Ciclo_Constructivo(ins1, maxTime, i);
		}
	}

	return 0;
}
#endif // CUBICAJE_DLL
//END