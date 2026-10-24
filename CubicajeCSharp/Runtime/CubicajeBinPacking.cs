// =====================================================================================
//  3D Bin Packing Problem (3D-BPP): varios contenedores idénticos, hay que empacar TODAS las
//  cajas usando el menor número posible de contenedores.
//
//  Extensión del concepto de espacios maximales a varios contenedores abiertos: la lista de
//  espacios maximales contiene espacios de todos los contenedores abiertos (cada espacio y cada
//  caja llevan su índice de contenedor; partir, contener y soportar solo relacionan elementos
//  del mismo contenedor). Cuando para las cajas pendientes no existe espacio disponible en la
//  lista, se abre un contenedor nuevo: su espacio completo entra a la lista.
//
//  OFFLINE (BinPackingSolver): el mismo GRASP + Beam Search del motor (CicloBS) con
//  Instancia.ModoBins. El constructivo abre contenedores cuando la lista queda vacía; el
//  puntaje a maximizar es -N + (suma de utilizaciones^2)/N, así que primero se minimiza el
//  número de contenedores N y, a igualdad, se prefiere la solución más concentrada. Se
//  detiene al alcanzar la cota inferior por volumen o al agotar el tiempo.
//
//  ONLINE (BinPackingOnline): MotorOnline con varios contenedores. Cada caja que llega se
//  ubica en el mejor candidato de la lista de espacios (contenedor más lleno o más antiguo,
//  según la estrategia; dentro del contenedor, la estrategia de posición); si no hay
//  candidato se abre un contenedor nuevo. Opcionalmente se limita el número de contenedores
//  abiertos (al abrir uno nuevo se cierra el más lleno y sus espacios salen de la lista).
// =====================================================================================
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;

namespace Cubicaje
{
    public sealed class ResultadoBinPacking
    {
        public int ContDimX, ContDimY, ContDimZ;

        /// <summary>Un resultado por contenedor usado. Los IndicePieza son globales (lista de entrada) y Contenedor es el índice en esta lista.</summary>
        public List<ResultadoCubicaje> Contenedores = new List<ResultadoCubicaje>();

        /// <summary>Piezas que no caben en un contenedor vacío en ninguna orientación.</summary>
        public List<int> NoColocadas = new List<int>();

        /// <summary>Cota inferior por volumen del número de contenedores.</summary>
        public int CotaInferior;

        public int Iteraciones;
        public int IteracionesBusquedaLocal;
        public double DuracionSeg;

        public int NumContenedores { get { return Contenedores.Count; } }

        /// <summary>Todas las piezas colocadas (con su contenedor), en el orden en que el motor las empacó.</summary>
        public List<PiezaColocada> TodasLasColocadas()
        {
            var res = new List<PiezaColocada>();
            foreach (var c in Contenedores) res.AddRange(c.Colocadas);
            return res.OrderBy(p => p.IndicePieza).ToList();
        }
    }

    public static class BinPackingSolver
    {
        /// <summary>
        /// Resuelve el bin packing: empaca todas las piezas en el menor número de contenedores idénticos
        /// (contDimX × contDimY × contDimZ, Z vertical).
        /// </summary>
        public static ResultadoBinPacking Resolver(int contDimX, int contDimY, int contDimZ,
            IList<PiezaEntrada> piezas, double maxTiempoSeg,
            ParametrosSolver parametros = null,
            CancellationToken cancelacion = default(CancellationToken))
        {
            var par = MotorCubicaje.ValidarEntrada(contDimX, contDimY, contDimZ, piezas, maxTiempoSeg, parametros);
            var sw = Stopwatch.StartNew();
            var res = new ResultadoBinPacking { ContDimX = contDimX, ContDimY = contDimY, ContDimZ = contDimZ };
            if (piezas.Count == 0)
            {
                res.DuracionSeg = sw.Elapsed.TotalSeconds;
                return res;
            }

            var inst = new Instancia(contDimX, contDimY, contDimZ, piezas, par, true);
            res.NoColocadas.AddRange(inst.IndicesSinCabida);
            res.NoColocadas.Sort();
            if (inst.BoxTypes.Count == 0)
            {
                res.DuracionSeg = sw.Elapsed.TotalSeconds;
                return res;
            }
            res.CotaInferior = inst.CotaInferior;

            int iter, iterLS;
            Container inc = MotorCubicaje.Ejecutar(inst, maxTiempoSeg, sw, cancelacion, out iter, out iterLS);

            var sinMapear = new List<int>();
            var colocadas = MotorCubicaje.MapearColocadas(inst, piezas, inc, sinMapear);
            foreach (int i in sinMapear)
            {
                if (!res.NoColocadas.Contains(i)) res.NoColocadas.Add(i); // no debería ocurrir: en bin packing todo lo que cabe se empaca
            }

            for (int c = 0; c < inc.NumContenedores; ++c)
            {
                var rc = new ResultadoCubicaje { ContDimX = contDimX, ContDimY = contDimY, ContDimZ = contDimZ };
                foreach (var p in colocadas)
                {
                    if (p.Contenedor == c) rc.Colocadas.Add(p);
                }
                if (rc.Colocadas.Count == 0) continue;
                rc.Utilizacion = inc.VolPorContenedor[c] / inst.Vol;
                rc.Iteraciones = iter;
                rc.IteracionesBusquedaLocal = iterLS;
                res.Contenedores.Add(rc);
            }
            // Renumerar por si algún contenedor quedó vacío
            for (int c = 0; c < res.Contenedores.Count; ++c)
            {
                var lista = res.Contenedores[c].Colocadas;
                for (int i = 0; i < lista.Count; ++i)
                {
                    var p = lista[i];
                    p.Contenedor = c;
                    lista[i] = p;
                }
            }
            res.Iteraciones = iter;
            res.IteracionesBusquedaLocal = iterLS;
            res.DuracionSeg = sw.Elapsed.TotalSeconds;
            return res;
        }
    }

    // ---------------------------------------------------------------------------------
    //  ONLINE
    // ---------------------------------------------------------------------------------

    public sealed class ParametrosBinPackingOnline
    {
        /// <summary>Cómo se ubica la pieza dentro del contenedor (estrategia de posición, lookahead, tipo de soporte).</summary>
        public ParametrosOnline Colocacion = new ParametrosOnline();

        public EstrategiaBins Estrategia = EstrategiaBins.MejorAjuste;

        /// <summary>Máximo de contenedores abiertos a la vez (0 = sin límite). Al abrir uno nuevo se cierra el más lleno.</summary>
        public int MaxContenedoresAbiertos = 0;
    }

    /// <summary>Bin packing online: las cajas llegan una a una y se reparten entre contenedores idénticos.</summary>
    public sealed class BinPackingOnline
    {
        readonly MotorOnline motor;
        readonly List<PiezaColocada> colocadas = new List<PiezaColocada>();
        int recibidas;

        public BinPackingOnline(int contDimX, int contDimY, int contDimZ, ParametrosBinPackingOnline parametros = null)
        {
            if (contDimX <= 0 || contDimY <= 0 || contDimZ <= 0) throw new ArgumentException("Dimensiones del contenedor inválidas.");
            var par = parametros ?? new ParametrosBinPackingOnline();
            motor = new MotorOnline(contDimX, contDimY, contDimZ, par.Colocacion ?? new ParametrosOnline(), true, par.Estrategia, Math.Max(0, par.MaxContenedoresAbiertos));
        }

        public int ContDimX { get { return motor.DimX; } }
        public int ContDimY { get { return motor.DimY; } }
        public int ContDimZ { get { return motor.DimZ; } }

        /// <summary>Contenedores abiertos hasta ahora (abiertos y cerrados).</summary>
        public int NumContenedores { get { return motor.S.NumContenedores; } }

        public int NumAbiertos { get { return motor.NumAbiertos(motor.S); } }

        public bool EstaAbierto(int contenedor) { return motor.S.Abierto[contenedor]; }

        /// <summary>Piezas colocadas (IndicePieza global y Contenedor asignado), en orden de llegada.</summary>
        public IReadOnlyList<PiezaColocada> Colocadas { get { return colocadas; } }

        public List<PiezaColocada> ColocadasDe(int contenedor)
        {
            return colocadas.Where(p => p.Contenedor == contenedor).ToList();
        }

        public double UtilizacionDe(int contenedor) { return motor.Utilizacion(motor.S, contenedor); }

        /// <summary>Utilización media de los contenedores usados.</summary>
        public double UtilizacionMedia
        {
            get
            {
                int n = NumContenedores;
                if (n == 0) return 0;
                double s = 0;
                for (int c = 0; c < n; ++c) s += UtilizacionDe(c);
                return s / n;
            }
        }

        public int PiezasRecibidas { get { return recibidas; } }
        public int PiezasRechazadas { get { return recibidas - colocadas.Count; } }

        /// <summary>Espacios libres de un contenedor (o de todos con -1), para visualizarlos.</summary>
        public List<PiezaColocada> EspaciosLibres(int contenedor = -1)
        {
            return motor.EspaciosLibres(contenedor);
        }

        public void Reiniciar()
        {
            motor.Reiniciar();
            colocadas.Clear();
            recibidas = 0;
        }

        /// <summary>Cierra un contenedor (por ejemplo, el pallet ya salió): sus espacios salen de la lista y no recibe más piezas.</summary>
        public void Cerrar(int contenedor)
        {
            motor.Cerrar(motor.S, contenedor);
        }

        /// <summary>¿La pieza cabe en un contenedor vacío? (si cabe, siempre se puede ubicar abriendo uno nuevo).</summary>
        public bool Cabe(PiezaEntrada pieza) { return motor.Cabe(pieza); }

        public PiezaColocada? Colocar(PiezaEntrada pieza)
        {
            return Colocar(pieza, null);
        }

        /// <summary>
        /// Ubica la pieza en el mejor espacio disponible de los contenedores abiertos; si no hay ninguno,
        /// abre un contenedor nuevo. null solo si la pieza no cabe en un contenedor vacío.
        /// </summary>
        public PiezaColocada? Colocar(PiezaEntrada pieza, IList<PiezaEntrada> proximas)
        {
            if (pieza == null) throw new ArgumentNullException("pieza");
            if (pieza.DimX <= 0 || pieza.DimY <= 0 || pieza.DimZ <= 0) throw new ArgumentException("Dimensiones de la pieza inválidas.");
            int indice = recibidas++;
            var caja = motor.Colocar(pieza, proximas);
            if (caja == null) return null;
            var pc = new PiezaColocada
            {
                IndicePieza = indice,
                Id = pieza.Id,
                Contenedor = caja.Contenedor,
                X1 = caja.X1, Y1 = caja.Y1, Z1 = caja.Z1,
                X2 = caja.X2, Y2 = caja.Y2, Z2 = caja.Z2
            };
            colocadas.Add(pc);
            return pc;
        }
    }
}
