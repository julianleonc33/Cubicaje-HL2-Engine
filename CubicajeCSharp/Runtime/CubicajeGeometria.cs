// =====================================================================================
//  Geometría plana para el chequeo de estabilidad.
//  En el motor C++ la clase Poligono construye el casco convexo de las esquinas de las
//  áreas de contacto y verifica que el centro de la caja quede dentro (soporte parcial).
//  Aquí se hace lo mismo con un casco convexo estándar (monotone chain) en enteros.
// =====================================================================================
using System;
using System.Collections.Generic;

namespace Cubicaje
{
    internal struct Punto
    {
        public readonly int X;
        public readonly int Y;

        public Punto(int x, int y)
        {
            X = x;
            Y = y;
        }
    }

    internal static class Geometria
    {
        public const double Error = 0.0001;

        static long Cruz(Punto o, Punto a, Punto b)
        {
            return (long)(a.X - o.X) * (b.Y - o.Y) - (long)(a.Y - o.Y) * (b.X - o.X);
        }

        /// <summary>Casco convexo en sentido antihorario, sin puntos repetidos ni colineales en los bordes.</summary>
        public static List<Punto> CascoConvexo(List<Punto> puntos)
        {
            var pts = new List<Punto>(puntos);
            pts.Sort((a, b) => a.X != b.X ? a.X.CompareTo(b.X) : a.Y.CompareTo(b.Y));

            var u = new List<Punto>(pts.Count);
            foreach (var p in pts)
            {
                if (u.Count == 0 || u[u.Count - 1].X != p.X || u[u.Count - 1].Y != p.Y) u.Add(p);
            }
            if (u.Count <= 2) return u;

            var h = new Punto[2 * u.Count];
            int k = 0;
            for (int i = 0; i < u.Count; ++i)
            {
                while (k >= 2 && Cruz(h[k - 2], h[k - 1], u[i]) <= 0) --k;
                h[k++] = u[i];
            }
            for (int i = u.Count - 2, t = k + 1; i >= 0; --i)
            {
                while (k >= t && Cruz(h[k - 2], h[k - 1], u[i]) <= 0) --k;
                h[k++] = u[i];
            }

            var res = new List<Punto>(k - 1);
            for (int i = 0; i < k - 1; ++i) res.Add(h[i]);
            return res;
        }

        /// <summary>¿El punto (cx, cy) está dentro del casco convexo de los puntos? El borde cuenta como dentro.</summary>
        public static bool PuntoDentroCascoDe(List<Punto> puntos, double cx, double cy)
        {
            var h = CascoConvexo(puntos);
            if (h.Count == 0) return false;
            if (h.Count == 1) return Math.Abs(h[0].X - cx) < Error && Math.Abs(h[0].Y - cy) < Error;
            if (h.Count == 2)
            {
                double ax = h[0].X, ay = h[0].Y, bx = h[1].X, by = h[1].Y;
                double cr = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
                if (Math.Abs(cr) > Error) return false;
                return cx >= Math.Min(ax, bx) - Error && cx <= Math.Max(ax, bx) + Error
                    && cy >= Math.Min(ay, by) - Error && cy <= Math.Max(ay, by) + Error;
            }
            for (int i = 0; i < h.Count; ++i)
            {
                Punto a = h[i];
                Punto b = h[(i + 1) % h.Count];
                double cr = (double)(b.X - a.X) * (cy - a.Y) - (double)(b.Y - a.Y) * (cx - a.X);
                if (cr < -Error) return false;
            }
            return true;
        }

        /// <summary>
        /// Si las bases de dos cajas se solapan en planta, agrega las 4 esquinas del rectángulo
        /// de intersección a <paramref name="destino"/> y devuelve true.
        /// </summary>
        public static bool AgregarInterseccionXY(List<Punto> destino,
            int ax1, int ay1, int ax2, int ay2,
            int bx1, int by1, int bx2, int by2)
        {
            if (ax1 >= bx2 || bx1 >= ax2 || ay1 >= by2 || by1 >= ay2) return false;
            int x1 = Math.Max(ax1, bx1), x2 = Math.Min(ax2, bx2);
            int y1 = Math.Max(ay1, by1), y2 = Math.Min(ay2, by2);
            destino.Add(new Punto(x1, y1));
            destino.Add(new Punto(x2, y1));
            destino.Add(new Punto(x1, y2));
            destino.Add(new Punto(x2, y2));
            return true;
        }
    }
}
