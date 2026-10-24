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
//       regla voraz y se elige el que deja más volumen cargado — la misma idea con que el
//       Beam Search offline evalúa sus nodos.
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

    public sealed class ParametrosOnline
    {
        public EstrategiaOnline Estrategia = EstrategiaOnline.MenorAlturaMejorAjuste;

        /// <summary>Candidatos que se evalúan con lookahead (equivale al beamWidth). Por defecto 5.</summary>
        public int CandidatosEvaluados = 5;

        /// <summary>false = soporte parcial (centro de la base sobre el casco convexo de los apoyos). true = soporte completo.</summary>
        public bool SoporteCompleto = false;
    }

    /// <summary>Empacador online para un solo contenedor.</summary>
    public sealed class CubicajeOnline
    {
        sealed class Estado
        {
            public List<PackedBox> Empacados;
            public List<MaximalSpace> Espacios;
            public double Utilizacion;

            public Estado Clonar()
            {
                var s = new Estado
                {
                    Empacados = new List<PackedBox>(Empacados.Count),
                    Espacios = new List<MaximalSpace>(Espacios.Count),
                    Utilizacion = Utilizacion
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
            public Box B;
            public BoxType Bt;
            public int X1, Y1, Z1;
            public int Bf1, Bf2, Bf3;
        }

        readonly int DimX, DimY, DimZ;
        readonly double Vol;
        readonly ParametrosOnline Par;
        readonly List<PiezaColocada> colocadas = new List<PiezaColocada>();
        Estado estado;
        int recibidas;

        public CubicajeOnline(int contDimX, int contDimY, int contDimZ, ParametrosOnline parametros = null)
        {
            if (contDimX <= 0 || contDimY <= 0 || contDimZ <= 0) throw new ArgumentException("Dimensiones del contenedor inválidas.");
            DimX = contDimX;
            DimY = contDimY;
            DimZ = contDimZ;
            Vol = (double)contDimX * contDimY * contDimZ;
            Par = parametros ?? new ParametrosOnline();
            if (Par.CandidatosEvaluados < 1) Par.CandidatosEvaluados = 1;
            Reiniciar();
        }

        public int ContDimX { get { return DimX; } }
        public int ContDimY { get { return DimY; } }
        public int ContDimZ { get { return DimZ; } }

        /// <summary>Piezas colocadas hasta ahora, en orden de llegada.</summary>
        public IReadOnlyList<PiezaColocada> Colocadas { get { return colocadas; } }

        /// <summary>Fracción (0..1) del volumen del contenedor ocupado.</summary>
        public double Utilizacion { get { return estado.Utilizacion; } }

        /// <summary>Piezas recibidas (colocadas + rechazadas).</summary>
        public int PiezasRecibidas { get { return recibidas; } }

        public int PiezasRechazadas { get { return recibidas - colocadas.Count; } }

        /// <summary>Vacía el contenedor.</summary>
        public void Reiniciar()
        {
            estado = new Estado
            {
                Empacados = new List<PackedBox>(),
                Espacios = new List<MaximalSpace> { MaximalSpace.Completo(DimX, DimY, DimZ) },
                Utilizacion = 0
            };
            colocadas.Clear();
            recibidas = 0;
        }

        /// <summary>Espacios libres actuales (para visualizarlos). Se devuelven como cajas con IndicePieza = -1.</summary>
        public List<PiezaColocada> EspaciosLibres()
        {
            var res = new List<PiezaColocada>(estado.Espacios.Count);
            foreach (var e in estado.Espacios)
            {
                res.Add(new PiezaColocada { IndicePieza = -1, Id = "espacio", X1 = e.X1, Y1 = e.Y1, Z1 = e.Z1, X2 = e.X2, Y2 = e.Y2, Z2 = e.Z2 });
            }
            return res;
        }

        /// <summary>¿La pieza tiene al menos una ubicación factible en el estado actual? No modifica nada.</summary>
        public bool Cabe(PiezaEntrada pieza)
        {
            var bt = CrearTipo(pieza);
            return bt != null && GenerarCandidatos(estado, bt).Count > 0;
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
            if (pieza == null) throw new ArgumentNullException("pieza");
            if (pieza.DimX <= 0 || pieza.DimY <= 0 || pieza.DimZ <= 0) throw new ArgumentException("Dimensiones de la pieza inválidas.");

            int indice = recibidas++;
            var bt = CrearTipo(pieza);
            if (bt == null) return null;

            var candidatos = GenerarCandidatos(estado, bt);
            if (candidatos.Count == 0) return null;

            int elegido = 0;
            if (proximas != null && proximas.Count > 0 && candidatos.Count > 1)
            {
                var tiposProximas = new List<BoxType>(proximas.Count);
                foreach (var p in proximas) tiposProximas.Add(p == null ? null : CrearTipo(p));

                int k = Math.Min(Par.CandidatosEvaluados, candidatos.Count);
                long mejorVol = -1;
                for (int i = 0; i < k; ++i)
                {
                    var s = estado.Clonar();
                    Aplicar(s, candidatos[i]);
                    long vol = bt.VolumenReal;
                    foreach (var t in tiposProximas)
                    {
                        if (t == null) continue;
                        var cs = GenerarCandidatos(s, t);
                        if (cs.Count == 0) continue;
                        Aplicar(s, cs[0]);
                        vol += t.VolumenReal;
                    }
                    if (vol > mejorVol)
                    {
                        mejorVol = vol;
                        elegido = i;
                    }
                }
            }

            var caja = Aplicar(estado, candidatos[elegido]);
            var pc = new PiezaColocada
            {
                IndicePieza = indice,
                Id = pieza.Id,
                X1 = caja.X1, Y1 = caja.Y1, Z1 = caja.Z1,
                X2 = caja.X2, Y2 = caja.Y2, Z2 = caja.Z2
            };
            colocadas.Add(pc);
            return pc;
        }

        // ---- internos ---------------------------------------------------------------------------

        BoxType CrearTipo(PiezaEntrada p)
        {
            var bt = new BoxType(0, 1, p.DimX, p.DimY, p.DimZ, p.RotX, p.RotY, p.RotZ, Vol);
            return bt.EliminarRotacionesQueNoCaben(DimX, DimY, DimZ) ? bt : null;
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
                        B = b,
                        Bt = bt,
                        X1 = x1, Y1 = y1, Z1 = e.Z1,
                        Bf1 = bf1, Bf2 = bf2, Bf3 = bf3
                    });
                }
            }
            res.Sort(Comparador(Par.Estrategia));
            return res;
        }

        static Comparison<Candidato> Comparador(EstrategiaOnline estrategia)
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
            p.Desplazar(c.X1, c.Y1, c.Z1);
            s.Empacados.Add(p);
            s.Utilizacion += c.Bt.VolumenContenedor;

            var be = new BloqueEmpacado(p.X1, p.Y1, p.Z1, p.X2, p.Y2, p.Z2);
            EspaciosMaximales.Actualizar(s.Espacios, be, !Par.SoporteCompleto);
            EspaciosMaximales.Juntar(s.Espacios, s.Empacados, DimZ);
            EspaciosMaximales.EliminarMinD(s.Espacios, 1, 1, 1);
            EspaciosMaximales.ActualizarIndicesCajas(s.Espacios, s.Empacados, DimX, DimY);
            return p;
        }
    }
}
