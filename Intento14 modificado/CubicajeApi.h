#pragma once

// ===== API C del motor de cubicaje (fase 2) =====
// Frontera plana en C para llamar al motor desde Unity vía P/Invoke.
// - Todo es blittable: sin strings ni memoria reservada por la DLL.
// - Unidades: enteros (ej. milímetros), igual que el motor.
// - Ejes: el motor usa Z como eje VERTICAL (z1 == 0 es el piso del pallet).
//   El llamador debe mapear el alto de la pieza a dimz (en Unity: Y <-> z).
// - No es reentrante: una sola llamada a la vez (usa estado global del motor).

#ifdef CUBICAJE_DLL
#define CUBICAJE_API extern "C" __declspec(dllexport)
#else
#define CUBICAJE_API extern "C"
#endif

// Códigos de error (valores negativos devueltos por Cubicaje_Resolver)
#define CUBICAJE_ERROR_PARAMETROS   -1  // punteros nulos, dimensiones o cantidades inválidas
#define CUBICAJE_ERROR_CAPACIDAD    -2  // capacidadSalida < numPiezas (se rechaza sin correr el motor)
#define CUBICAJE_ERROR_EXCEPCION    -99 // el motor lanzó una excepción interna

// Pieza de entrada. Su posición en el arreglo es su índice (indicePieza en la salida).
typedef struct {
	int dimx, dimy, dimz;
} CubicajePiezaEntrada;

// Pieza colocada: índice de la pieza de entrada y las dos esquinas de la caja en el pallet.
typedef struct {
	int indicePieza;
	int x1, y1, z1, x2, y2, z2;
} CubicajePiezaColocada;

// Resuelve el cubicaje. Devuelve cuántas piezas colocó (>= 0) o un código de error negativo.
// capacidadSalida debe ser >= numPiezas.
CUBICAJE_API int Cubicaje_Resolver(
	int contDimx, int contDimy, int contDimz,
	const CubicajePiezaEntrada* piezas, int numPiezas,
	int maxTimeSeg,
	CubicajePiezaColocada* salida, int capacidadSalida);

// Utilización (0..1) del volumen del pallet en la última llamada exitosa. Solo diagnóstico.
CUBICAJE_API double Cubicaje_UltimaUtilizacion(void);
