// =====================================================================================
//  Solver OFFLINE: GRASP + Beam Search (Ciclo_BS del motor C++).
//
//  Problema: un solo contenedor, un conjunto conocido de cajas (3D-SKP / 3D-SLOPP),
//  maximizar el volumen cargado con estabilidad parcial.
//
//  Esquema (idéntico al C++):
//    1. Una iteración por cada alpha (0.1 .. 1.0); luego alphas elegidos por un muestreo
//       reactivo (los que han dado mejor utilización se escogen más).
//    2. Cada iteración: constructivo GRASP -> si mejora la incumbente o su "métrica"
//       (volumen / área de piso) es competitiva, se desarma parcialmente la solución y se
//       completa con Beam Search; cada nodo del haz se evalúa terminándolo con el
//       constructivo rápido.
//    3. Termina al agotar el tiempo o al empacar todas las cajas.
// =====================================================================================
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;

namespace Cubicaje
{
    public static class CubicajeSolver
    {
        /// <summary>
        /// Resuelve el cubicaje. Equivale a Cubicaje_Resolver de la DLL.
        /// </summary>
        /// <param name="contDimX">Ancho del contenedor (X).</param>
        /// <param name="contDimY">Fondo del contenedor (Y).</param>
        /// <param name="contDimZ">Alto del contenedor (Z, vertical).</param>
        /// <param name="piezas">Cajas a empacar (su posición en la lista es el IndicePieza del resultado).</param>
        /// <param name="maxTiempoSeg">Tiempo máximo de cómputo en segundos.</param>
        /// <param name="parametros">Parámetros opcionales (beamWidth, maxNBloques, semilla).</param>
        /// <param name="cancelacion">Token para interrumpir; se devuelve la mejor solución encontrada hasta el momento.</param>
        public static ResultadoCubicaje Resolver(int contDimX, int contDimY, int contDimZ,
            IList<PiezaEntrada> piezas, double maxTiempoSeg,
            ParametrosSolver parametros = null,
            CancellationToken cancelacion = default(CancellationToken))
        {
            if (contDimX <= 0 || contDimY <= 0 || contDimZ <= 0) throw new ArgumentException("Dimensiones del contenedor inválidas.");
            if (piezas == null) throw new ArgumentNullException("piezas");
            if (maxTiempoSeg < 0) throw new ArgumentException("El tiempo máximo no puede ser negativo.");
            for (int i = 0; i < piezas.Count; ++i)
            {
                var p = piezas[i];
                if (p == null || p.DimX <= 0 || p.DimY <= 0 || p.DimZ <= 0) throw new ArgumentException("Dimensiones inválidas en la pieza " + i + ".");
            }
            var par = parametros ?? new ParametrosSolver();
            if (par.BeamWidth < 1) throw new ArgumentException("BeamWidth debe ser >= 1.");
            if (par.MaxNBloques < 1) throw new ArgumentException("MaxNBloques debe ser >= 1.");

            var sw = Stopwatch.StartNew();
            var res = new ResultadoCubicaje { ContDimX = contDimX, ContDimY = contDimY, ContDimZ = contDimZ };
            if (piezas.Count == 0)
            {
                res.DuracionSeg = sw.Elapsed.TotalSeconds;
                return res;
            }

            var inst = new Instancia(contDimX, contDimY, contDimZ, piezas, par);
            if (inst.BoxTypes.Count == 0)
            {
                for (int i = 0; i < piezas.Count; ++i) res.NoColocadas.Add(i);
                res.DuracionSeg = sw.Elapsed.TotalSeconds;
                return res;
            }
            inst.HacerBloquesGenerales(cancelacion);

            var ciclo = new CicloBS(inst, maxTiempoSeg, sw, cancelacion);
            Container inc = ciclo.Ejecutar();

            // Armar el resultado: cada caja empacada se asigna a una pieza de entrada de su tipo
            var cursores = new int[inst.BoxTypes.Count];
            var usada = new bool[piezas.Count];
            foreach (var p in inc.Empacados)
            {
                var bt = p.Bt;
                int idx = bt.IndicesEntrada[cursores[bt.Id]++];
                usada[idx] = true;
                res.Colocadas.Add(new PiezaColocada
                {
                    IndicePieza = idx,
                    Id = piezas[idx].Id,
                    X1 = p.X1, Y1 = p.Y1, Z1 = p.Z1,
                    X2 = p.X2, Y2 = p.Y2, Z2 = p.Z2
                });
            }
            for (int i = 0; i < piezas.Count; ++i)
            {
                if (!usada[i]) res.NoColocadas.Add(i);
            }
            res.Utilizacion = inc.Utilizacion;
            res.Iteraciones = ciclo.TotalIter;
            res.IteracionesBusquedaLocal = ciclo.TotalIterLS;
            res.DuracionSeg = sw.Elapsed.TotalSeconds;
            return res;
        }
    }

    /// <summary>Alpha del GRASP con su desempeño promedio (muestreo reactivo).</summary>
    internal sealed class Alpha
    {
        public readonly double Val;
        public double FunObj;
        public double Frecuencia;

        public Alpha(double val)
        {
            Val = val;
        }

        public void Actualizar(double fo)
        {
            ++Frecuencia;
            FunObj += (fo - FunObj) / Frecuencia;
        }
    }

    /// <summary>Ciclo_BS del C++.</summary>
    internal sealed class CicloBS
    {
        const double Epsilon = 1e-9;

        readonly Instancia I;
        readonly double MaxTime;
        readonly Stopwatch Sw;
        readonly CancellationToken Ct;
        readonly List<Alpha> Alphas;
        readonly double[] PesosAcum;

        Container C0;
        Container Inc;

        public int TotalIter;
        public int TotalIterLS;

        public CicloBS(Instancia inst, double maxTime, Stopwatch sw, CancellationToken ct)
        {
            I = inst;
            MaxTime = maxTime;
            Sw = sw;
            Ct = ct;

            Alphas = new List<Alpha>();
            for (int i = 1; i <= 10; ++i) Alphas.Add(new Alpha(i / 10.0));

            // Pesos 1/j normalizados y acumulados: el mejor alpha (tras ordenar) es el más probable
            PesosAcum = new double[Alphas.Count];
            double total = 0;
            for (int j = 0; j < PesosAcum.Length; ++j)
            {
                PesosAcum[j] = 1.0 / (j + 1);
                total += PesosAcum[j];
            }
            for (int j = 0; j < PesosAcum.Length; ++j) PesosAcum[j] /= total;
            for (int j = 1; j < PesosAcum.Length; ++j) PesosAcum[j] += PesosAcum[j - 1];
        }

        double Duracion { get { return Sw.Elapsed.TotalSeconds; } }

        bool Parar { get { return Duracion >= MaxTime || Ct.IsCancellationRequested; } }

        public Container Ejecutar()
        {
            C0 = new Container(I);
            Inc = new Container(C0, false);

            // Adquisición de datos iniciales: una iteración por alpha.
            // (Se garantiza al menos una iteración aunque el tiempo sea 0.)
            for (int ia = 0; ia < Alphas.Count && Inc.HayCajasPorEmpacar && (!Parar || TotalIter == 0); ++ia)
            {
                if (Iteracion(Alphas[ia])) break;
            }

            // Resto de la metaheurística: alpha reactivo
            while (!Parar && Inc.HayCajasPorEmpacar)
            {
                if (Iteracion(DeterminarAlpha())) break;
            }
            return Inc;
        }

        Alpha DeterminarAlpha()
        {
            Alphas.Sort((a, b) => b.FunObj.CompareTo(a.FunObj)); // mejor funObj primero
            double val = I.Rand01();
            for (int i = 0; i < Alphas.Count; ++i)
            {
                if (val < PesosAcum[i]) return Alphas[i];
            }
            return Alphas[Alphas.Count - 1];
        }

        /// <summary>Una iteración GRASP (+ beam search si aplica). Devuelve true si la incumbente ya está completa.</summary>
        bool Iteracion(Alpha a)
        {
            ++TotalIter;
            var C = new Container(C0, true);
            C.Alpha = a.Val;
            C.Constructivo();
            a.Actualizar(C.Utilizacion);

            double porcentaje = MaxTime > 0 ? 0.7 - 0.6 * Duracion / MaxTime : 0.7;
            bool bl = false;
            if (C.Utilizacion > Inc.Utilizacion + Epsilon)
            {
                Inc = new Container(C, false);
                if (!Inc.HayCajasPorEmpacar) return true;
                bl = true;
            }

            long volHasta;
            double metricaInc, metricaC;
            int hasta;
            Inc.MetricaIncumbente(porcentaje, out volHasta, out metricaInc);
            C.MetricaNoIncumbente(volHasta, out metricaC, out hasta);
            if (bl || metricaInc <= metricaC)
            {
                ++TotalIterLS;
                C.RemoverPiezas(hasta);
                BeamSearch(C);
            }
            return !Inc.HayCajasPorEmpacar;
        }

        /// <summary>Beam Search desde una solución parcial. Cada hijo se evalúa completándolo con ConstructivoFast.</summary>
        void BeamSearch(Container raiz)
        {
            var haz = new List<Container> { raiz };
            while (haz.Count > 0 && !Parar && Inc.HayCajasPorEmpacar)
            {
                var hijos = new List<Container>();
                foreach (var c in haz)
                {
                    if (!Inc.HayCajasPorEmpacar) break;
                    var opciones = c.ObtenerOpcionesBeam();
                    foreach (var o in opciones)
                    {
                        if (!Inc.HayCajasPorEmpacar) break;
                        var completo = new Container(o, true);
                        completo.ConstructivoFast();
                        o.UtilizacionBW = completo.Utilizacion;
                        if (completo.Utilizacion > Inc.Utilizacion) Inc = new Container(completo, false);
                    }
                    hijos.AddRange(opciones);
                }

                if (!Inc.HayCajasPorEmpacar || hijos.Count == 0) break;
                hijos = hijos.OrderByDescending(x => x.UtilizacionBW).ToList(); // list::sort estable
                if (hijos.Count > I.Par.BeamWidth) hijos.RemoveRange(I.Par.BeamWidth, hijos.Count - I.Par.BeamWidth);
                haz = hijos;
            }
        }
    }
}
