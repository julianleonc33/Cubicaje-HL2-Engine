// =====================================================================================
//  Pruebas de consola del motor C# (no se copia a Unity).
//  Compilar con csc o con el .csproj de esta carpeta. Ver README.md.
//
//  Verifica para cada solución: contención, no solapamiento, soporte parcial de toda caja
//  elevada, que cada pieza se use a lo sumo una vez y que su orientación sea permitida.
// =====================================================================================
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using Cubicaje;

static class PruebasCubicaje
{
    static int fallos = 0;

    static void Check(bool cond, string msg)
    {
        if (!cond)
        {
            ++fallos;
            Console.WriteLine("  FALLO: " + msg);
        }
    }

    // ---- verificador --------------------------------------------------------------------------

    static bool OrientacionPermitida(PiezaEntrada p, int dx, int dy, int dz)
    {
        var dims = new List<int[]>();
        if (p.RotX == 1 || p.RotX == 3) dims.Add(new[] { p.DimZ, p.DimY, p.DimX });
        if (p.RotX == 2 || p.RotX == 3) dims.Add(new[] { p.DimY, p.DimZ, p.DimX });
        if (p.RotY == 1 || p.RotY == 3) dims.Add(new[] { p.DimX, p.DimZ, p.DimY });
        if (p.RotY == 2 || p.RotY == 3) dims.Add(new[] { p.DimZ, p.DimX, p.DimY });
        if (p.RotZ == 1 || p.RotZ == 3) dims.Add(new[] { p.DimX, p.DimY, p.DimZ });
        if (p.RotZ == 2 || p.RotZ == 3) dims.Add(new[] { p.DimY, p.DimX, p.DimZ });
        if (dims.Count == 0) dims.Add(new[] { p.DimX, p.DimY, p.DimZ });
        return dims.Any(d => d[0] == dx && d[1] == dy && d[2] == dz);
    }

    static double Cruz(double ax, double ay, double bx, double by, double cx, double cy)
    {
        return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    }

    // Casco convexo + punto dentro, implementado aparte del motor para que la verificación sea independiente.
    static bool CentroSoportado(PiezaColocada p, List<PiezaColocada> todas)
    {
        var pts = new List<double[]>();
        foreach (var q in todas)
        {
            if (q.Z2 != p.Z1) continue;
            if (p.X1 >= q.X2 || q.X1 >= p.X2 || p.Y1 >= q.Y2 || q.Y1 >= p.Y2) continue;
            double x1 = Math.Max(p.X1, q.X1), x2 = Math.Min(p.X2, q.X2);
            double y1 = Math.Max(p.Y1, q.Y1), y2 = Math.Min(p.Y2, q.Y2);
            pts.Add(new[] { x1, y1 }); pts.Add(new[] { x2, y1 }); pts.Add(new[] { x1, y2 }); pts.Add(new[] { x2, y2 });
        }
        if (pts.Count == 0) return false;
        double cx = p.CentroX, cy = p.CentroY;
        // Fuerza bruta: el punto está dentro del casco convexo si no existe una recta por dos puntos
        // que deje al punto estrictamente de un lado y a todos los demás puntos del otro.
        var u = pts.GroupBy(t => t[0] + "," + t[1]).Select(g => g.First()).ToList();
        if (u.Count == 1) return Math.Abs(u[0][0] - cx) < 1e-6 && Math.Abs(u[0][1] - cy) < 1e-6;
        for (int i = 0; i < u.Count; ++i)
        {
            for (int j = i + 1; j < u.Count; ++j)
            {
                double s = Cruz(u[i][0], u[i][1], u[j][0], u[j][1], cx, cy);
                if (Math.Abs(s) < 1e-6) continue;
                bool todosOpuestos = true;
                for (int k = 0; k < u.Count; ++k)
                {
                    double sk = Cruz(u[i][0], u[i][1], u[j][0], u[j][1], u[k][0], u[k][1]);
                    if (sk * s > 1e-6) { todosOpuestos = false; break; }
                }
                if (todosOpuestos) return false;
            }
        }
        return true;
    }

    static void Verificar(string nombre, int cx, int cy, int cz, IList<PiezaEntrada> piezas, List<PiezaColocada> col)
    {
        var usadas = new HashSet<int>();
        foreach (var p in col)
        {
            Check(p.X1 >= 0 && p.Y1 >= 0 && p.Z1 >= 0 && p.X2 <= cx && p.Y2 <= cy && p.Z2 <= cz, nombre + ": contención " + p);
            Check(p.DimX > 0 && p.DimY > 0 && p.DimZ > 0, nombre + ": dimensiones " + p);
            Check(p.IndicePieza >= 0 && p.IndicePieza < piezas.Count, nombre + ": índice fuera de rango " + p);
            Check(usadas.Add(p.IndicePieza), nombre + ": pieza repetida " + p);
            if (p.IndicePieza >= 0 && p.IndicePieza < piezas.Count)
                Check(OrientacionPermitida(piezas[p.IndicePieza], p.DimX, p.DimY, p.DimZ), nombre + ": orientación no permitida " + p + " de " + piezas[p.IndicePieza]);
            if (p.Z1 > 0) Check(CentroSoportado(p, col), nombre + ": sin soporte " + p);
        }
        for (int i = 0; i < col.Count; ++i)
        {
            for (int j = i + 1; j < col.Count; ++j)
            {
                var a = col[i]; var b = col[j];
                bool solapa = !(a.X1 >= b.X2 || b.X1 >= a.X2 || a.Y1 >= b.Y2 || b.Y1 >= a.Y2 || a.Z1 >= b.Z2 || b.Z1 >= a.Z2);
                Check(!solapa, nombre + ": solapamiento " + a + " / " + b);
            }
        }
    }

    // ---- casos --------------------------------------------------------------------------------

    static List<PiezaEntrada> Caso205()
    {
        // Mismo caso de -testCApi del C++: 6 x A (40x80, alto 165) + 6 x B (80x80, alto 120) en 205^3.
        var piezas = new List<PiezaEntrada>();
        for (int i = 1; i <= 6; ++i) piezas.Add(new PiezaEntrada("A" + i, 40, 80, 165));
        for (int i = 1; i <= 6; ++i) piezas.Add(new PiezaEntrada("B" + i, 80, 80, 120));
        return piezas;
    }

    static void Prueba205(double maxTime)
    {
        Console.WriteLine("== Offline 205^3 (12 piezas), maxTime=" + maxTime + "s ==");
        var piezas = Caso205();
        var r = CubicajeSolver.Resolver(205, 205, 205, piezas, maxTime, new ParametrosSolver { Semilla = 123 });
        Console.WriteLine("  colocadas=" + r.Colocadas.Count + "/12  utilizacion=" + (r.Utilizacion * 100).ToString("F2") + "%  iter=" + r.Iteraciones + "  iterLS=" + r.IteracionesBusquedaLocal + "  dur=" + r.DuracionSeg.ToString("F2") + "s");
        foreach (var p in r.Colocadas) Console.WriteLine("    " + p);
        Verificar("205", 205, 205, 205, piezas, r.Colocadas);
        // Referencia: el motor C++ original (Intento14.exe -testCApi 3 / -testBeamWidth 15 5) llega a 11/12 con 84.13 %.
        Check(r.Colocadas.Count >= 11, "205: se esperaban al menos 11/12 piezas colocadas (referencia del motor C++)");
        Check(r.Utilizacion >= 0.84, "205: se esperaba utilización >= 84 % (referencia del motor C++: 84.13 %)");
        Check(Math.Abs(r.Utilizacion - r.Colocadas.Sum(p => p.Volumen) / (205.0 * 205 * 205)) < 1e-9, "205: utilización inconsistente");
    }

    static void PruebaDeterminismo()
    {
        Console.WriteLine("== Determinismo con semilla ==");
        var piezas = Caso205();
        var r1 = CubicajeSolver.Resolver(205, 205, 205, piezas, 0, new ParametrosSolver { Semilla = 7 });
        var r2 = CubicajeSolver.Resolver(205, 205, 205, piezas, 0, new ParametrosSolver { Semilla = 7 });
        Check(r1.Colocadas.Count == r2.Colocadas.Count && r1.Colocadas.Select(p => p.ToString()).SequenceEqual(r2.Colocadas.Select(p => p.ToString())), "misma semilla debe dar la misma solución");
        Console.WriteLine("  ok (" + r1.Colocadas.Count + " piezas, iter=" + r1.Iteraciones + ")");
    }

    static void PruebaCasosBorde()
    {
        Console.WriteLine("== Casos borde ==");
        var r0 = CubicajeSolver.Resolver(100, 100, 100, new List<PiezaEntrada>(), 1);
        Check(r0.Colocadas.Count == 0 && r0.Completo, "lista vacía");

        var grande = new List<PiezaEntrada> { new PiezaEntrada("G", 300, 10, 10), new PiezaEntrada("P", 10, 10, 10) };
        var r1 = CubicajeSolver.Resolver(100, 100, 100, grande, 0.2, new ParametrosSolver { Semilla = 1 });
        Check(r1.NoColocadas.Contains(0) && r1.Colocadas.Count == 1 && r1.Colocadas[0].IndicePieza == 1, "pieza que no cabe queda en NoColocadas y la otra se coloca");

        var vert = new List<PiezaEntrada> { new PiezaEntrada("V", 50, 30, 90, true), new PiezaEntrada("V2", 50, 30, 90, true) };
        var r2 = CubicajeSolver.Resolver(100, 100, 100, vert, 0.2, new ParametrosSolver { Semilla = 1 });
        Verificar("vertical", 100, 100, 100, vert, r2.Colocadas);
        Check(r2.Colocadas.All(p => p.DimZ == 90), "mantenerVertical: el alto debe conservarse");
        Console.WriteLine("  ok");
    }

    static List<PiezaEntrada> PiezasAleatorias(Random rnd, int n, int cx, int cy, int cz)
    {
        var piezas = new List<PiezaEntrada>();
        for (int i = 0; i < n; ++i)
        {
            int dx = rnd.Next(Math.Max(1, cx / 10), Math.Max(2, cx / 2));
            int dy = rnd.Next(Math.Max(1, cy / 10), Math.Max(2, cy / 2));
            int dz = rnd.Next(Math.Max(1, cz / 10), Math.Max(2, cz / 2));
            piezas.Add(new PiezaEntrada("P" + i, dx, dy, dz, rnd.Next(4) == 0));
        }
        return piezas;
    }

    static void PruebaAleatoriasOffline(int casos, double maxTime)
    {
        Console.WriteLine("== Offline aleatorio (" + casos + " casos, " + maxTime + "s cada uno) ==");
        var rnd = new Random(2024);
        double sumaUt = 0;
        for (int c = 0; c < casos; ++c)
        {
            int cx = rnd.Next(200, 1200), cy = rnd.Next(200, 1200), cz = rnd.Next(200, 1200);
            var piezas = PiezasAleatorias(rnd, rnd.Next(5, 35), cx, cy, cz);
            var r = CubicajeSolver.Resolver(cx, cy, cz, piezas, maxTime, new ParametrosSolver { Semilla = c });
            Verificar("rnd" + c, cx, cy, cz, piezas, r.Colocadas);
            Check(r.Colocadas.Count + r.NoColocadas.Count == piezas.Count, "rnd" + c + ": colocadas + no colocadas != total");
            sumaUt += r.Utilizacion;
            Console.WriteLine("  caso " + c + ": cont=" + cx + "x" + cy + "x" + cz + " piezas=" + piezas.Count + " colocadas=" + r.Colocadas.Count + " ut=" + (r.Utilizacion * 100).ToString("F1") + "% iter=" + r.Iteraciones + " dur=" + r.DuracionSeg.ToString("F2") + "s");
        }
        Console.WriteLine("  utilización media " + (100 * sumaUt / casos).ToString("F1") + "%");
    }

    static void PruebaOnline()
    {
        Console.WriteLine("== Online 205^3 (12 piezas en orden aleatorio) ==");
        var piezas = Caso205();
        var rnd = new Random(5);
        var orden = piezas.OrderBy(p => rnd.Next()).ToList();

        foreach (EstrategiaOnline est in Enum.GetValues(typeof(EstrategiaOnline)))
        {
            // Sin lookahead
            var on = new CubicajeOnline(205, 205, 205, new ParametrosOnline { Estrategia = est });
            foreach (var p in orden) on.Colocar(p);
            var col = on.Colocadas.ToList();
            Verificar("online-" + est, 205, 205, 205, orden, col);
            Check(Math.Abs(on.Utilizacion - col.Sum(p => p.Volumen) / (205.0 * 205 * 205)) < 1e-9, "online: utilización inconsistente");

            // Con lookahead de 3 piezas
            var on2 = new CubicajeOnline(205, 205, 205, new ParametrosOnline { Estrategia = est });
            for (int i = 0; i < orden.Count; ++i)
            {
                var prox = orden.Skip(i + 1).Take(3).ToList();
                on2.Colocar(orden[i], prox);
            }
            Verificar("online-la-" + est, 205, 205, 205, orden, on2.Colocadas.ToList());
            Console.WriteLine("  " + est + ": sin lookahead " + col.Count + "/12 (" + (on.Utilizacion * 100).ToString("F1") + "%), lookahead 3: " + on2.Colocadas.Count + "/12 (" + (on2.Utilizacion * 100).ToString("F1") + "%)");
        }

        // Rechazo de una pieza que no cabe: el estado no cambia
        var on3 = new CubicajeOnline(100, 100, 100);
        Check(on3.Colocar(new PiezaEntrada("X", 500, 10, 10)) == null && on3.Colocadas.Count == 0 && on3.PiezasRechazadas == 1, "online: pieza que no cabe debe rechazarse");
        Check(on3.Colocar(new PiezaEntrada("Y", 100, 100, 100)) != null && on3.Utilizacion > 0.999, "online: pieza exacta debe llenar el contenedor");
        Check(on3.Colocar(new PiezaEntrada("Z", 1, 1, 1)) == null, "online: contenedor lleno debe rechazar");
    }

    static void PruebaOnlineAleatoria(int casos)
    {
        Console.WriteLine("== Online aleatorio (" + casos + " casos) ==");
        var rnd = new Random(99);
        double suma = 0, sumaLa = 0;
        var sw = Stopwatch.StartNew();
        for (int c = 0; c < casos; ++c)
        {
            int cx = rnd.Next(200, 1200), cy = rnd.Next(200, 1200), cz = rnd.Next(200, 1200);
            var piezas = PiezasAleatorias(rnd, rnd.Next(10, 60), cx, cy, cz);
            bool completo = rnd.Next(2) == 0;

            var on = new CubicajeOnline(cx, cy, cz, new ParametrosOnline { SoporteCompleto = completo });
            foreach (var p in piezas) on.Colocar(p);
            Verificar("online-rnd" + c, cx, cy, cz, piezas, on.Colocadas.ToList());

            var onLa = new CubicajeOnline(cx, cy, cz, new ParametrosOnline { SoporteCompleto = completo });
            for (int i = 0; i < piezas.Count; ++i) onLa.Colocar(piezas[i], piezas.Skip(i + 1).Take(4).ToList());
            Verificar("online-la-rnd" + c, cx, cy, cz, piezas, onLa.Colocadas.ToList());

            suma += on.Utilizacion;
            sumaLa += onLa.Utilizacion;
        }
        Console.WriteLine("  utilización media sin lookahead " + (100 * suma / casos).ToString("F1") + "%, con lookahead 4: " + (100 * sumaLa / casos).ToString("F1") + "%  (" + sw.Elapsed.TotalSeconds.ToString("F2") + "s en total)");
    }

    // ---- bin packing --------------------------------------------------------------------------

    static void VerificarBins(string nombre, int cx, int cy, int cz, IList<PiezaEntrada> piezas, List<List<PiezaColocada>> bins, List<int> noColocadas)
    {
        var usadas = new HashSet<int>();
        for (int c = 0; c < bins.Count; ++c)
        {
            Verificar(nombre + "[bin " + c + "]", cx, cy, cz, piezas, bins[c]);
            Check(bins[c].Count > 0, nombre + ": contenedor " + c + " vacío");
            foreach (var p in bins[c])
            {
                Check(p.Contenedor == c, nombre + ": campo Contenedor incorrecto en " + p);
                Check(usadas.Add(p.IndicePieza), nombre + ": pieza " + p.IndicePieza + " repetida entre contenedores");
            }
        }
        foreach (int i in noColocadas) Check(usadas.Add(i), nombre + ": pieza no colocada " + i + " también aparece colocada");
        Check(usadas.Count == piezas.Count, nombre + ": colocadas + no colocadas (" + usadas.Count + ") != total (" + piezas.Count + ")");
    }

    static List<PiezaEntrada> Cubos(int n, int lado)
    {
        var l = new List<PiezaEntrada>();
        for (int i = 0; i < n; ++i) l.Add(new PiezaEntrada("C" + i, lado, lado, lado));
        return l;
    }

    static void PruebaBinPackingOffline()
    {
        Console.WriteLine("== Bin packing offline ==");

        // 8 cubos de 50 en 100^3 -> 1 contenedor exacto; 10 cubos -> 2; 27 cubos de 33 -> 1
        var casos = new[] {
            new { nombre = "8 cubos de 50", piezas = Cubos(8, 50), esperado = 1 },
            new { nombre = "10 cubos de 50", piezas = Cubos(10, 50), esperado = 2 },
            new { nombre = "27 cubos de 33", piezas = Cubos(27, 33), esperado = 1 },
            new { nombre = "20 cubos de 50", piezas = Cubos(20, 50), esperado = 3 },
        };
        foreach (var caso in casos)
        {
            var r = BinPackingSolver.Resolver(100, 100, 100, caso.piezas, 2, new ParametrosSolver { Semilla = 1 });
            VerificarBins("bpp " + caso.nombre, 100, 100, 100, caso.piezas, r.Contenedores.Select(c => c.Colocadas).ToList(), r.NoColocadas);
            Check(r.NumContenedores >= r.CotaInferior, "bpp " + caso.nombre + ": menos contenedores que la cota inferior");
            Check(r.NumContenedores == caso.esperado, "bpp " + caso.nombre + ": se esperaban " + caso.esperado + " contenedores, hubo " + r.NumContenedores);
            Console.WriteLine("  " + caso.nombre + ": " + r.NumContenedores + " contenedores (cota " + r.CotaInferior + "), ut=" + string.Join("/", r.Contenedores.Select(c => (c.Utilizacion * 100).ToString("F0"))) + "%  dur=" + r.DuracionSeg.ToString("F2") + "s");
        }

        // Piezas que no caben en ningún contenedor
        var mixto = Cubos(3, 50); mixto.Add(new PiezaEntrada("G", 150, 10, 10));
        var rm = BinPackingSolver.Resolver(100, 100, 100, mixto, 0.5, new ParametrosSolver { Semilla = 1 });
        Check(rm.NoColocadas.Count == 1 && rm.NoColocadas[0] == 3 && rm.NumContenedores == 1, "bpp: pieza gigante debe quedar en NoColocadas");

        // Aleatorios
        var rnd = new Random(77);
        int sumaBins = 0, sumaCota = 0;
        for (int c = 0; c < 6; ++c)
        {
            int cx = rnd.Next(200, 800), cy = rnd.Next(200, 800), cz = rnd.Next(200, 800);
            var piezas = PiezasAleatorias(rnd, rnd.Next(20, 60), cx, cy, cz);
            var r = BinPackingSolver.Resolver(cx, cy, cz, piezas, 2, new ParametrosSolver { Semilla = c });
            VerificarBins("bpp rnd" + c, cx, cy, cz, piezas, r.Contenedores.Select(x => x.Colocadas).ToList(), r.NoColocadas);
            Check(r.NumContenedores >= r.CotaInferior, "bpp rnd" + c + ": menos contenedores que la cota inferior");
            sumaBins += r.NumContenedores; sumaCota += r.CotaInferior;
            Console.WriteLine("  rnd" + c + ": piezas=" + piezas.Count + " contenedores=" + r.NumContenedores + " cota=" + r.CotaInferior + " iter=" + r.Iteraciones + " dur=" + r.DuracionSeg.ToString("F2") + "s");
        }
        Console.WriteLine("  total contenedores " + sumaBins + " vs cota " + sumaCota);
    }

    static void PruebaBinPackingOnline()
    {
        Console.WriteLine("== Bin packing online ==");
        var rnd = new Random(31);
        int sumaBins = 0, sumaBinsPA = 0, sumaCota = 0;
        for (int c = 0; c < 10; ++c)
        {
            int cx = rnd.Next(200, 800), cy = rnd.Next(200, 800), cz = rnd.Next(200, 800);
            var piezas = PiezasAleatorias(rnd, rnd.Next(20, 80), cx, cy, cz);
            int maxAbiertos = c % 2 == 0 ? 0 : 2;

            var on = new BinPackingOnline(cx, cy, cz, new ParametrosBinPackingOnline { MaxContenedoresAbiertos = maxAbiertos });
            for (int i = 0; i < piezas.Count; ++i)
            {
                on.Colocar(piezas[i], piezas.Skip(i + 1).Take(3).ToList());
                if (maxAbiertos > 0) Check(on.NumAbiertos <= maxAbiertos, "bpp online: más contenedores abiertos que el máximo");
            }
            var bins = new List<List<PiezaColocada>>();
            for (int b = 0; b < on.NumContenedores; ++b) bins.Add(on.Colocadas.Where(p => p.Contenedor == b).ToList());
            VerificarBins("bpp online rnd" + c, cx, cy, cz, piezas, bins, new List<int>());
            for (int b = 0; b < on.NumContenedores; ++b)
                Check(on.ColocadasDe(b).Select(p => p.IndicePieza).SequenceEqual(bins[b].Select(p => p.IndicePieza)), "bpp online: índices del contenedor " + b + " no coinciden");

            var pa = new BinPackingOnline(cx, cy, cz, new ParametrosBinPackingOnline { Estrategia = EstrategiaBins.PrimerAjuste });
            foreach (var p in piezas) pa.Colocar(p);
            var binsPA = new List<List<PiezaColocada>>();
            for (int b = 0; b < pa.NumContenedores; ++b) binsPA.Add(pa.Colocadas.Where(p => p.Contenedor == b).ToList());
            VerificarBins("bpp online PA rnd" + c, cx, cy, cz, piezas, binsPA, new List<int>());

            int cota = (int)Math.Ceiling(piezas.Sum(p => (double)p.Volumen) / ((double)cx * cy * cz) - 1e-9);
            sumaBins += on.NumContenedores; sumaBinsPA += pa.NumContenedores; sumaCota += cota;
        }
        Console.WriteLine("  mejor ajuste + lookahead 3: " + sumaBins + " contenedores; primer ajuste: " + sumaBinsPA + "; cota " + sumaCota);

        var vacio = new BinPackingOnline(100, 100, 100);
        Check(vacio.Colocar(new PiezaEntrada("G", 500, 1, 1)) == null && vacio.NumContenedores == 0 && vacio.PiezasRechazadas == 1, "bpp online: pieza gigante rechazada sin abrir contenedor");
        foreach (var p in Cubos(9, 50)) Check(vacio.Colocar(p) != null, "bpp online: cubo debe colocarse");
        Check(vacio.NumContenedores == 2, "bpp online: 9 cubos de 50 deben usar 2 contenedores, usaron " + vacio.NumContenedores);
        vacio.Cerrar(1);
        Check(vacio.Colocar(Cubos(1, 50)[0]).Value.Contenedor == 2, "bpp online: tras cerrar el único contenedor con espacio debe abrirse otro");
    }

    static int Main(string[] args)
    {
        double t205 = args.Length > 0 ? double.Parse(args[0], System.Globalization.CultureInfo.InvariantCulture) : 3;
        Prueba205(t205);
        PruebaDeterminismo();
        PruebaCasosBorde();
        PruebaAleatoriasOffline(8, 0.5);
        PruebaOnline();
        PruebaOnlineAleatoria(20);
        PruebaBinPackingOffline();
        PruebaBinPackingOnline();

        Console.WriteLine();
        Console.WriteLine(fallos == 0 ? "TODAS LAS PRUEBAS PASARON" : "FALLOS: " + fallos);
        return fallos == 0 ? 0 : 1;
    }
}
