// =====================================================================================
//  Solver ONLINE: las cajas llegan una a una y cada una debe ubicarse en el momento, sin
//  conocer las siguientes (o conociendo solo un "buffer" de las próximas) y sin reubicar
//  las ya colocadas.
//
//  Reutiliza la misma maquinaria del motor offline (espacios maximales, esquinas,
//  soporte parcial / completo). Para cada caja:
//    1. Se generan los candidatos: (espacio maximal x orientación) donde cabe, ubicada en
//       la esquina del espacio que escoge el motor, y con soporte válido si está elevada.
//    2. Se ordenan según la estrategia (por defecto: la posición más baja, luego mejor
//       ajuste, luego más al fondo).
//    3. Sin lookahead se toma el primero. Con lookahead (lista de próximas cajas
//       conocidas) se evalúan los K mejores candidatos completando el buffer con la misma
//       regla voraz y se elige el que deja el mejor estado — la misma idea con que el
//       Beam Search offline evalúa sus nodos.
//
//  MotorOnline es el núcleo y trabaja sobre UNA lista de espacios maximales que puede contener
//  espacios de varios contenedores abiertos (campo Contenedor). CubicajeOnline lo usa con un
//  solo contenedor; BinPackingOnline (CubicajeBinPacking.cs) lo usa abriendo contenedores
//  cuando una caja no tiene espacio disponible en la lista.
// =====================================================================================
using System;
using System.Collections.Generic;
using System.Linq;

namespace Cubicaje
{
    public enum EstrategiaOnline
    {
        /// <summary>Posición más baja primero (va construyendo capas), luego mejor ajuste, luego más al fondo.</summary>
        MenorAlturaMejorAjuste = 0,

        /// <summary>Más al fondo primero y de arriba hacia abajo (la regla Fondo_Altura del motor offline), luego mejor ajuste.</summary>
        FondoAlturaMejorAjuste = 1,

        /// <summary>Mejor ajuste puro (menor espacio sobrante en el espacio maximal), luego más bajo y más al fondo.</summary>
        MejorAjuste = 2
    }

    /// <summary>Cómo se elige el contenedor cuando hay varios abiertos (bin packing online).</summary>
    public enum EstrategiaBins
    {
        /// <summary>Entre los contenedores abiertos donde cabe la pieza, el más lleno (tiende a cerrar contenedores).</summary>
        MejorAjuste = 0,

        /// <summary>El contenedor abierto más antiguo donde cabe.</summary>
        PrimerAjuste = 1
    }

    public sealed class ParametrosOnline
    {
        public EstrategiaOnline Estrategia = EstrategiaOnline.MenorAlturaMejorAjuste;

        /// <summary>Candidatos que se evalúan con lookahead (equivale al beamWidth). Por defecto 5.</summary>
        public int CandidatosEvaluados = 5;

        /// <summary>false = soporte parcial (centro de la base sobre el casco convexo de los apoyos). true = soporte completo.</summary>
        public bool SoporteCompleto = false;
    }

    /// <summary>
    /// Núcleo del empaque online sobre una lista de espacios maximales con varios contenedores.
    /// </summary>
    internal sealed class MotorOnline
    {
        internal sealed class Estado
        {
            public List<PackedBox> Empacados = new List<PackedBox>();
            public List<MaximalSpace> Espacios = new List<MaximalSpace>();
            public List<long> VolPorContenedor = new List<long>();
            public List<bool> Abierto = new List<bool>();
            public int NumContenedores;

            public Estado Clonar()
            {
                var s = new Estado
                {
                    Empacados = new List<PackedBox>(Empacados.Count),
                    Espacios = new List<MaximalSpace>(Espacios.Count),
                    VolPorContenedor = new List<long>(VolPorContenedor),
                    Abierto = new List<bool>(Abierto),
                    NumContenedores = NumContenedores
                };
                foreach (var p in Empacados) s.Empacados.Add(p.Clone());
                foreach (var e in Espacios) s.Espacios.Add(e.Clone());
                return s;
            }
        }

        struct Candidato
        {
            public int IEspacio;
            public int IRotacion;
            public int Contenedor;
            public Box B;
            public BoxType Bt;
            public int X1, Y1, Z1;
            public int Bf1, Bf2, Bf3;
        }

        public readonly int DimX, DimY, DimZ;
        public readonly double Vol;
        readonly ParametrosOnline Par;
        readonly bool Multi;              // true: abrir contenedores cuando no hay espacio (bin packing)
        readonly EstrategiaBins EstBins;
        readonly int MaxAbiertos;         // 0 = sin límite
        public Estado S;

        public MotorOnline(int dimX, int dimY, int dimZ, ParametrosOnline par, bool multiContenedor, EstrategiaBins estBins, int maxAbiertos)
        {
            DimX = dimX; DimY = dimY; DimZ = dimZ;
            Vol = (double)dimX * dimY * dimZ;
            Par = par;
            Multi = multiContenedor;
            EstBins = estBins;
            MaxAbiertos = maxAbiertos;
            Reiniciar();
        }

        public void Reiniciar()
        {
            S = new Estado();
            if (!Multi) AbrirContenedor(S);
        }

        public int NumAbiertos(Estado s)
        {
            int n = 0;
            foreach (bool a in s.Abierto) if (a) ++n;
            return n;
        }

        /// <summary>Abre un contenedor: su espacio completo entra a la lista de espacios maximales.</summary>
        public void AbrirContenedor(Estado s)
        {
            if (Multi && MaxAbiertos > 0)
            {
                while (NumAbiertos(s) >= MaxAbiertos) CerrarMasLleno(s);
            }
            int k = s.NumContenedores++;
            s.VolPorContenedor.Add(0);
            s.Abierto.Add(true);
            s.Espacios.Add(MaximalSpace.Completo(DimX, DimY, DimZ, k));
        }

        /// <summary>Cierra un contenedor: sus espacios salen de la lista y no recibe más cajas.</summary>
        public void Cerrar(Estado s, int k)
        {
            if (k < 0 || k >= s.NumContenedores || !s.Abierto[k]) return;
            s.Abierto[k] = false;
            s.Espacios.RemoveAll(e => e.Contenedor == k);
        }

        void CerrarMasLleno(Estado s)
        {
            int mejor = -1;
            for (int k = 0; k < s.NumContenedores; ++k)
            {
                if (!s.Abierto[k]) continue;
                if (mejor < 0 || s.VolPorContenedor[k] > s.VolPorContenedor[mejor]) mejor = k;
            }
            if (mejor >= 0) Cerrar(s, mejor);
        }

        public double Utilizacion(Estado s, int k)
        {
            return s.VolPorContenedor[k] / Vol;
        }

        /// <summary>Puntaje de un estado (mayor es mejor): volumen cargado con un contenedor; -N + sumSq/N en bin packing.</summary>
        public double Puntaje(Estado s)
        {
            if (!Multi) return s.NumContenedores == 0 ? 0 : s.VolPorContenedor[0];
            if (s.NumContenedores == 0) return 0;
            double sumSq = 0;
            foreach (long v in s.VolPorContenedor)
            {
                double u = v / Vol;
                sumSq += u * u;
            }
            return -s.NumContenedores + sumSq / s.NumContenedores;
        }

        public BoxType CrearTipo(PiezaEntrada p)
        {
            var bt = new BoxType(0, 1, p.DimX, p.DimY, p.DimZ, p.RotX, p.RotY, p.RotZ, Vol);
            return bt.EliminarRotacionesQueNoCaben(DimX, DimY, DimZ) ? bt : null;
        }

        /// <summary>¿La pieza se puede ubicar? Con varios contenedores basta con que quepa en uno vacío.</summary>
        public bool Cabe(PiezaEntrada pieza)
        {
            var bt = CrearTipo(pieza);
            if (bt == null) return false;
            if (Multi) return true;
            return GenerarCandidatos(S, bt).Count > 0;
        }

        /// <summary>
        /// Ubica la pieza. Devuelve la caja ubicada o null si no hay espacio (con un solo contenedor) o
        /// si la pieza no cabe ni en un contenedor vacío.
        /// </summary>
        public PackedBox Colocar(PiezaEntrada pieza, IList<PiezaEntrada> proximas)
        {
            var bt = CrearTipo(pieza);
            if (bt == null) return null;

            var candidatos = GenerarCandidatos(S, bt);
            if (candidatos.Count == 0)
            {
                // Ninguna caja... esta caja no tiene espacio disponible en la lista: se abre un contenedor nuevo
                if (!Multi) return null;
                AbrirContenedor(S);
                candidatos = GenerarCandidatos(S, bt);
                if (candidatos.Count == 0) return null;
            }

            int elegido = 0;
            if (proximas != null && proximas.Count > 0 && candidatos.Count > 1)
            {
                var tiposProximas = new List<BoxType>(proximas.Count);
                foreach (var p in proximas) tiposProximas.Add(p == null ? null : CrearTipo(p));

                int k = Math.Min(Math.Max(1, Par.CandidatosEvaluados), candidatos.Count);
                double mejorPuntaje = double.NegativeInfinity;
                for (int i = 0; i < k; ++i)
                {
                    var s = S.Clonar();
                    Aplicar(s, candidatos[i]);
                    foreach (var t in tiposProximas)
                    {
                        if (t == null) continue;
                        var cs = GenerarCandidatos(s, t);
                        if (cs.Count == 0)
                        {
                            if (!Multi) continue;
                            AbrirContenedor(s);
                            cs = GenerarCandidatos(s, t);
                            if (cs.Count == 0) continue;
                        }
                        Aplicar(s, cs[0]);
                    }
                    double puntaje = Puntaje(s);
                    if (puntaje > mejorPuntaje + 1e-12)
                    {
                        mejorPuntaje = puntaje;
                        elegido = i;
                    }
                }
            }

            return Aplicar(S, candidatos[elegido]);
        }

        static void EsquinaDe(MaximalSpace e, int dx, int dy, out int x1, out int y1)
        {
            switch (e.IndEsquina)
            {
                case 2: x1 = e.X1; y1 = e.Y2 - dy; break;
                case 3: x1 = e.X2 - dx; y1 = e.Y1; break;
                case 4: x1 = e.X2 - dx; y1 = e.Y2 - dy; break;
                default: x1 = e.X1; y1 = e.Y1; break;
            }
        }

        List<Candidato> GenerarCandidatos(Estado s, BoxType bt)
        {
            var res = new List<Candidato>();
            for (int ie = 0; ie < s.Espacios.Count; ++ie)
            {
                var e = s.Espacios[ie];
                for (int ir = 0; ir < bt.Boxes.Count; ++ir)
                {
                    var b = bt.Boxes[ir];
                    if (b.X > e.Dx || b.Y > e.Dy || b.Z > e.Dz) continue;

                    int x1, y1;
                    EsquinaDe(e, b.X, b.Y, out x1, out y1);
                    if (e.Z1 > 0)
                    {
                        bool ok = Par.SoporteCompleto
                            ? EspaciosMaximales.CajaSoporteCompleto(x1, y1, x1 + b.X, y1 + b.Y, e.IndCajas, s.Empacados)
                            : EspaciosMaximales.CajaSoportada(x1, y1, x1 + b.X, y1 + b.Y, x1 + b.X / 2.0, y1 + b.Y / 2.0, e.IndCajas, s.Empacados);
                        if (!ok) continue;
                    }

                    int bf1 = e.Dx - b.X, bf2 = e.Dy - b.Y, bf3 = e.Dz - b.Z;
                    Util.OrdenarBf(ref bf1, ref bf2, ref bf3);
                    res.Add(new Candidato
                    {
                        IEspacio = ie,
                        IRotacion = ir,
                        Contenedor = e.Contenedor,
                        B = b,
                        Bt = bt,
                        X1 = x1, Y1 = y1, Z1 = e.Z1,
                        Bf1 = bf1, Bf2 = bf2, Bf3 = bf3
                    });
                }
            }
            if (res.Count > 1) res.Sort(Comparador(s));
            return res;
        }

        Comparison<Candidato> Comparador(Estado s)
        {
            Comparison<Candidato> posicion = ComparadorPosicion(Par.Estrategia);
            if (!Multi) return posicion;
            if (EstBins == EstrategiaBins.PrimerAjuste)
            {
                return (a, b) =>
                {
                    if (a.Contenedor != b.Contenedor) return a.Contenedor.CompareTo(b.Contenedor);
                    return posicion(a, b);
                };
            }
            return (a, b) =>
            {
                if (a.Contenedor != b.Contenedor)
                {
                    long va = s.VolPorContenedor[a.Contenedor], vb = s.VolPorContenedor[b.Contenedor];
                    if (va != vb) return vb.CompareTo(va); // el más lleno primero
                    return a.Contenedor.CompareTo(b.Contenedor);
                }
                return posicion(a, b);
            };
        }

        static Comparison<Candidato> ComparadorPosicion(EstrategiaOnline estrategia)
        {
            switch (estrategia)
            {
                case EstrategiaOnline.FondoAlturaMejorAjuste:
                    return (a, b) =>
                    {
                        if (a.X1 != b.X1) return a.X1.CompareTo(b.X1);
                        if (a.Z1 != b.Z1) return b.Z1.CompareTo(a.Z1);
                        int c = CompararBf(a, b); if (c != 0) return c;
                        if (a.Y1 != b.Y1) return a.Y1.CompareTo(b.Y1);
                        return Desempate(a, b);
                    };
                case EstrategiaOnline.MejorAjuste:
                    return (a, b) =>
                    {
                        int c = CompararBf(a, b); if (c != 0) return c;
                        if (a.Z1 != b.Z1) return a.Z1.CompareTo(b.Z1);
                        if (a.X1 != b.X1) return a.X1.CompareTo(b.X1);
                        if (a.Y1 != b.Y1) return a.Y1.CompareTo(b.Y1);
                        return Desempate(a, b);
                    };
                default:
                    return (a, b) =>
                    {
                        if (a.Z1 != b.Z1) return a.Z1.CompareTo(b.Z1);
                        int c = CompararBf(a, b); if (c != 0) return c;
                        if (a.X1 != b.X1) return a.X1.CompareTo(b.X1);
                        if (a.Y1 != b.Y1) return a.Y1.CompareTo(b.Y1);
                        return Desempate(a, b);
                    };
            }
        }

        static int CompararBf(Candidato a, Candidato b)
        {
            if (a.Bf1 != b.Bf1) return a.Bf1.CompareTo(b.Bf1);
            if (a.Bf2 != b.Bf2) return a.Bf2.CompareTo(b.Bf2);
            return a.Bf3.CompareTo(b.Bf3);
        }

        static int Desempate(Candidato a, Candidato b)
        {
            if (a.IEspacio != b.IEspacio) return a.IEspacio.CompareTo(b.IEspacio);
            return a.IRotacion.CompareTo(b.IRotacion);
        }

        /// <summary>Ubica el candidato y actualiza los espacios maximales (misma secuencia que Container.Empacar).</summary>
        PackedBox Aplicar(Estado s, Candidato c)
        {
            var p = new PackedBox(c.B, c.Bt);
            p.Id = s.Empacados.Count;
            p.IdBloque = s.Empacados.Count;
            p.Contenedor = c.Contenedor;
            p.Desplazar(c.X1, c.Y1, c.Z1);
            s.Empacados.Add(p);
            s.VolPorContenedor[c.Contenedor] += c.Bt.VolumenReal;

            var be = new BloqueEmpacado(p.X1, p.Y1, p.Z1, p.X2, p.Y2, p.Z2, c.Contenedor);
            EspaciosMaximales.Actualizar(s.Espacios, be, !Par.SoporteCompleto);
            EspaciosMaximales.Juntar(s.Espacios, s.Empacados, DimZ);
            EspaciosMaximales.EliminarMinD(s.Espacios, 1, 1, 1);
            EspaciosMaximales.ActualizarIndicesCajas(s.Espacios, s.Empacados, DimX, DimY);
            return p;
        }

        public List<PiezaColocada> EspaciosLibres(int contenedor)
        {
            var res = new List<PiezaColocada>();
            foreach (var e in S.Espacios)
            {
                if (contenedor >= 0 && e.Contenedor != contenedor) continue;
                res.Add(new PiezaColocada { IndicePieza = -1, Id = "espacio", Contenedor = e.Contenedor, X1 = e.X1, Y1 = e.Y1, Z1 = e.Z1, X2 = e.X2, Y2 = e.Y2, Z2 = e.Z2 });
            }
            return res;
        }
    }

    /// <summary>Empacador online para un solo contenedor.</summary>
    public sealed class CubicajeOnline
    {
        readonly MotorOnline motor;
        readonly List<PiezaColocada> colocadas = new List<PiezaColocada>();
        int recibidas;

        public CubicajeOnline(int contDimX, int contDimY, int contDimZ, ParametrosOnline parametros = null)
        {
            if (contDimX <= 0 || contDimY <= 0 || contDimZ <= 0) throw new ArgumentException("Dimensiones del contenedor inválidas.");
            motor = new MotorOnline(contDimX, contDimY, contDimZ, parametros ?? new ParametrosOnline(), false, EstrategiaBins.PrimerAjuste, 0);
        }

        public int ContDimX { get { return motor.DimX; } }
        public int ContDimY { get { return motor.DimY; } }
        public int ContDimZ { get { return motor.DimZ; } }

        /// <summary>Piezas colocadas hasta ahora, en orden de llegada.</summary>
        public IReadOnlyList<PiezaColocada> Colocadas { get { return colocadas; } }

        /// <summary>Fracción (0..1) del volumen del contenedor ocupado.</summary>
        public double Utilizacion { get { return motor.Utilizacion(motor.S, 0); } }

        /// <summary>Piezas recibidas (colocadas + rechazadas).</summary>
        public int PiezasRecibidas { get { return recibidas; } }

        public int PiezasRechazadas { get { return recibidas - colocadas.Count; } }

        /// <summary>Vacía el contenedor.</summary>
        public void Reiniciar()
        {
            motor.Reiniciar();
            colocadas.Clear();
            recibidas = 0;
        }

        /// <summary>Espacios libres actuales (para visualizarlos). Se devuelven como cajas con IndicePieza = -1.</summary>
        public List<PiezaColocada> EspaciosLibres()
        {
            return motor.EspaciosLibres(0);
        }

        /// <summary>¿La pieza tiene al menos una ubicación factible en el estado actual? No modifica nada.</summary>
        public bool Cabe(PiezaEntrada pieza)
        {
            return motor.Cabe(pieza);
        }

        /// <summary>Coloca la pieza que acaba de llegar (sin información de las siguientes). null si no cabe.</summary>
        public PiezaColocada? Colocar(PiezaEntrada pieza)
        {
            return Colocar(pieza, null);
        }

        /// <summary>
        /// Coloca la pieza que acaba de llegar usando como lookahead las próximas piezas conocidas
        /// (en el orden en que llegarán). null si la pieza no cabe; en ese caso el estado no cambia
        /// y la pieza cuenta como rechazada.
        /// </summary>
        public PiezaColocada? Colocar(PiezaEntrada pieza, IList<PiezaEntrada> proximas)
        {
            return Colocar(pieza, proximas, recibidas);
        }

        /// <summary>Igual que Colocar pero con un IndicePieza dado por el llamador.</summary>
        public PiezaColocada? Colocar(PiezaEntrada pieza, IList<PiezaEntrada> proximas, int indicePieza)
        {
            if (pieza == null) throw new ArgumentNullException("pieza");
            if (pieza.DimX <= 0 || pieza.DimY <= 0 || pieza.DimZ <= 0) throw new ArgumentException("Dimensiones de la pieza inválidas.");
            ++recibidas;
            var caja = motor.Colocar(pieza, proximas);
            if (caja == null) return null;
            var pc = new PiezaColocada
            {
                IndicePieza = indicePieza,
                Id = pieza.Id,
                Contenedor = 0,
                X1 = caja.X1, Y1 = caja.Y1, Z1 = caja.Z1,
                X2 = caja.X2, Y2 = caja.Y2, Z2 = caja.Z2
            };
            colocadas.Add(pc);
            return pc;
        }
    }
}
