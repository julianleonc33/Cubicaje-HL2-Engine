// =====================================================================================
//  Cubicaje para HoloLens 2 — tipos públicos del motor.
//  Traducción a C# del motor C++ "Intento14" (GRASP + Beam Search) para usarlo como
//  script de Unity sin DLL nativa.
//
//  Convención de ejes del MOTOR (igual que en C++):
//      X = ancho, Y = fondo, Z = ALTO (z1 == 0 es el piso del contenedor/pallet).
//  Unity usa Y como vertical: ver CubicajeEjes en CubicajeUnity.cs para convertir.
//  Unidades: enteros (por ejemplo milímetros).
// =====================================================================================
using System;
using System.Collections.Generic;

namespace Cubicaje
{
    /// <summary>Pieza (caja) a empacar. Las dimensiones son enteras y DimZ es el alto.</summary>
    public sealed class PiezaEntrada
    {
        public string Id;
        public int DimX;
        public int DimY;
        public int DimZ;

        /// <summary>
        /// Rotaciones permitidas, misma convención del motor C++ (0 = ninguna, 1 = primera
        /// variante, 2 = segunda variante, 3 = ambas).
        ///  - RotZ: giros alrededor del eje vertical (la pieza conserva su alto DimZ).
        ///  - RotX / RotY: la pieza se acuesta (DimX o DimY pasan a ser el alto).
        /// Por defecto 3/3/3 = rotación libre (las 6 orientaciones), como en la DLL.
        /// </summary>
        public int RotX = 3;
        public int RotY = 3;
        public int RotZ = 3;

        public PiezaEntrada(string id, int dimX, int dimY, int dimZ)
        {
            Id = id;
            DimX = dimX;
            DimY = dimY;
            DimZ = dimZ;
        }

        /// <param name="mantenerVertical">true = la pieza no se puede acostar ("este lado arriba").</param>
        public PiezaEntrada(string id, int dimX, int dimY, int dimZ, bool mantenerVertical)
            : this(id, dimX, dimY, dimZ)
        {
            if (mantenerVertical)
            {
                RotX = 0;
                RotY = 0;
            }
        }

        public long Volumen { get { return (long)DimX * DimY * DimZ; } }

        public override string ToString()
        {
            return Id + " [" + DimX + "x" + DimY + "x" + DimZ + "]";
        }
    }

    /// <summary>Pieza ya colocada: índice de la pieza de entrada y las dos esquinas de la caja.</summary>
    public struct PiezaColocada
    {
        public int IndicePieza;
        public string Id;

        /// <summary>Índice del contenedor (0 en el problema de un solo contenedor; en bin packing, el contenedor asignado).</summary>
        public int Contenedor;
        public int X1, Y1, Z1;
        public int X2, Y2, Z2;

        public int DimX { get { return X2 - X1; } }
        public int DimY { get { return Y2 - Y1; } }
        public int DimZ { get { return Z2 - Z1; } }
        public double CentroX { get { return (X1 + X2) / 2.0; } }
        public double CentroY { get { return (Y1 + Y2) / 2.0; } }
        public double CentroZ { get { return (Z1 + Z2) / 2.0; } }
        public long Volumen { get { return (long)DimX * DimY * DimZ; } }

        public override string ToString()
        {
            return Id + " (idx " + IndicePieza + ") -> (" + X1 + "," + Y1 + "," + Z1 + ") - (" + X2 + "," + Y2 + "," + Z2 + ")";
        }
    }

    /// <summary>Parámetros del solver offline (GRASP + Beam Search).</summary>
    public sealed class ParametrosSolver
    {
        /// <summary>Ancho del haz (beamWidth del motor C++, por defecto 5).</summary>
        public int BeamWidth = 5;

        /// <summary>Bloques generales a generar por pieza (maxNBloques del motor, por defecto 10).</summary>
        public int MaxNBloques = 10;

        /// <summary>Utilización volumétrica mínima para que dos bloques se fusionen en uno (0.99 en el motor).</summary>
        public double UtilizacionMaximaBloque = 0.99;

        /// <summary>Semilla del generador aleatorio. null = semilla por tiempo (como el motor C++).</summary>
        public int? Semilla = null;
    }

    /// <summary>Resultado del solver offline.</summary>
    public sealed class ResultadoCubicaje
    {
        public int ContDimX, ContDimY, ContDimZ;

        /// <summary>Piezas colocadas, en el orden en que el motor las empacó (útil para guiar el cargue paso a paso).</summary>
        public List<PiezaColocada> Colocadas = new List<PiezaColocada>();

        /// <summary>Índices (en la lista de entrada) de las piezas que no se colocaron.</summary>
        public List<int> NoColocadas = new List<int>();

        /// <summary>Fracción (0..1) del volumen del contenedor ocupado por las piezas colocadas.</summary>
        public double Utilizacion;

        public int Iteraciones;
        public int IteracionesBusquedaLocal;
        public double DuracionSeg;

        public bool Completo { get { return NoColocadas.Count == 0; } }
    }
}
