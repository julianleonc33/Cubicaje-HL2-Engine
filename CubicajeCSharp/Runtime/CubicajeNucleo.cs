// =====================================================================================
//  Núcleo del motor de cubicaje (traducción del motor C++ "Intento14").
//
//  Clases y su equivalente en el C++:
//      Box / BoxType          -> Box / BoxType          (orientaciones de un tipo de caja)
//      PackedBox              -> PackedBox              (caja ubicada, con id == índice en la lista)
//      Bloque                 -> Bloque                 (bloque "general": grupo casi-cúbico de cajas)
//      MaximalSpace           -> MaximalSpace           (espacio maximal libre)
//      BloqueEmpacado         -> BloqueEmpacado         (bloque ubicado en un espacio, con métricas best-fit)
//      EspaciosMaximales      -> métodos de Container   (actualizar / juntar / índices / esquina / soporte)
//      Container              -> Container              (solución parcial: espacios, cajas, bloques disponibles)
//      Instancia              -> variables globales     (contenedor, BoxTypes0, cantidadesBoxTypes0, Bloques0)
//
//  Alcance de la traducción: la configuración que usa la DLL de Unity
//      r_estabilidad = 1 (soporte parcial), r_juntarEspacios = 0, r_multidrop = 0, r_maxPresionItems = false.
//  La estabilidad es geométrica (centro de la base dentro del casco convexo de las áreas de
//  contacto). No se porta la distribución de masas (eliminación gaussiana) porque la app no
//  entrega masas y con masa = 0 esa lógica degenera.
// =====================================================================================
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;

namespace Cubicaje
{
    internal static class Util
    {
        public const double Error = 0.0001;

        /// <summary>Ordena ascendentemente las tres métricas best-fit (misma red de comparaciones del C++).</summary>
        public static void OrdenarBf(ref int bf1, ref int bf2, ref int bf3)
        {
            int t;
            if (bf2 <= bf1 && bf2 <= bf3) { t = bf2; bf2 = bf1; bf1 = t; }
            else if (bf3 <= bf1 && bf3 <= bf2) { t = bf3; bf3 = bf1; bf1 = t; }
            if (bf3 < bf2) { t = bf3; bf3 = bf2; bf2 = t; }
        }
    }

    // ---------------------------------------------------------------------------------
    //  Box / BoxType
    // ---------------------------------------------------------------------------------

    /// <summary>Una orientación concreta de un tipo de caja.</summary>
    internal sealed class Box
    {
        public int IndBoxType;
        public readonly int X, Y, Z;
        public readonly int Area; // área de la base

        public Box(int indBoxType, int x, int y, int z)
        {
            IndBoxType = indBoxType;
            X = x;
            Y = y;
            Z = z;
            Area = x * y;
        }

        public static int Comparar(Box a, Box b)
        {
            if (a.X != b.X) return a.X.CompareTo(b.X);
            if (a.Y != b.Y) return a.Y.CompareTo(b.Y);
            return a.Z.CompareTo(b.Z);
        }

        public bool MismasDims(Box o)
        {
            return X == o.X && Y == o.Y && Z == o.Z;
        }
    }

    /// <summary>Tipo de caja: dimensiones, cantidad y orientaciones permitidas.</summary>
    internal sealed class BoxType
    {
        public int Id;                         // índice dentro de Instancia.BoxTypes
        public readonly int Cantidad;
        public readonly int DimX, DimY, DimZ;  // dimensiones originales
        public int MinDx, MinDy, MinDz;
        public readonly long VolumenReal;
        public readonly double VolumenContenedor; // fracción del volumen del contenedor
        public readonly List<Box> Boxes = new List<Box>(6);

        /// <summary>Índices de las piezas de entrada que pertenecen a este tipo.</summary>
        public readonly List<int> IndicesEntrada = new List<int>();

        public BoxType(int id, int cantidad, int dimX, int dimY, int dimZ, int rotX, int rotY, int rotZ, double volContenedor)
        {
            Id = id;
            Cantidad = cantidad;
            DimX = dimX;
            DimY = dimY;
            DimZ = dimZ;
            VolumenReal = (long)dimX * dimY * dimZ;
            VolumenContenedor = VolumenReal / volContenedor;
            FullRotation(dimX, dimY, dimZ, rotX, rotY, rotZ);
            DetermineDmins();
        }

        void FullRotation(int dx, int dy, int dz, int rx, int ry, int rz)
        {
            // Rx
            if (rx == 1 || rx == 3) Boxes.Add(new Box(Id, dz, dy, dx));
            if (rx == 2 || rx == 3) Boxes.Add(new Box(Id, dy, dz, dx));
            // Ry
            if (ry == 1 || ry == 3) Boxes.Add(new Box(Id, dx, dz, dy));
            if (ry == 2 || ry == 3) Boxes.Add(new Box(Id, dz, dx, dy));
            // Rz
            if (rz == 1 || rz == 3) Boxes.Add(new Box(Id, dx, dy, dz));
            if (rz == 2 || rz == 3) Boxes.Add(new Box(Id, dy, dx, dz));

            // Sin ninguna rotación permitida se usa la orientación tal como viene.
            if (Boxes.Count == 0) Boxes.Add(new Box(Id, dx, dy, dz));

            // Eliminación de orientaciones iguales
            Boxes.Sort(Box.Comparar);
            for (int i = Boxes.Count - 1; i > 0; --i)
            {
                if (Boxes[i].MismasDims(Boxes[i - 1])) Boxes.RemoveAt(i);
            }
        }

        public void ActualizarIndice(int id)
        {
            Id = id;
            foreach (var b in Boxes) b.IndBoxType = id;
        }

        public void DetermineDmins()
        {
            if (Boxes.Count == 0) return;
            MinDx = Boxes[0].X;
            MinDy = Boxes[0].Y;
            MinDz = Boxes[0].Z;
            for (int i = 1; i < Boxes.Count; ++i)
            {
                if (Boxes[i].X < MinDx) MinDx = Boxes[i].X;
                if (Boxes[i].Y < MinDy) MinDy = Boxes[i].Y;
                if (Boxes[i].Z < MinDz) MinDz = Boxes[i].Z;
            }
        }

        /// <summary>Quita las orientaciones que no caben en el contenedor. Devuelve false si no queda ninguna.</summary>
        public bool EliminarRotacionesQueNoCaben(int contX, int contY, int contZ)
        {
            Boxes.RemoveAll(b => b.X > contX || b.Y > contY || b.Z > contZ);
            if (Boxes.Count == 0) return false;
            DetermineDmins();
            return true;
        }
    }

    // ---------------------------------------------------------------------------------
    //  Instancia (lo que en el C++ son variables globales)
    // ---------------------------------------------------------------------------------

    internal sealed class Instancia
    {
        public readonly int DimX, DimY, DimZ;
        public readonly double Vol;
        public readonly ParametrosSolver Par;
        public readonly Random Rnd;
        public readonly List<BoxType> BoxTypes = new List<BoxType>();
        public int[] Cantidades0 = new int[0];
        public readonly List<Bloque> Bloques = new List<Bloque>();

        /// <summary>Piezas de entrada que no caben en el contenedor en ninguna orientación.</summary>
        public readonly List<int> IndicesSinCabida = new List<int>();

        /// <summary>
        /// false: un solo contenedor (3D-SKP: maximizar volumen cargado).
        /// true: bin packing (3D-BPP: todas las cajas se empacan, abriendo contenedores idénticos
        /// cuando ninguna caja pendiente tiene espacio disponible; se minimiza el número de contenedores).
        /// </summary>
        public readonly bool ModoBins;

        /// <summary>Cota inferior por volumen del número de contenedores (bin packing).</summary>
        public int CotaInferior = 1;

        public Instancia(int dimX, int dimY, int dimZ, IList<PiezaEntrada> piezas, ParametrosSolver par, bool modoBins)
        {
            DimX = dimX;
            DimY = dimY;
            DimZ = dimZ;
            Vol = (double)dimX * dimY * dimZ;
            Par = par;
            ModoBins = modoBins;
            Rnd = par.Semilla.HasValue ? new Random(par.Semilla.Value) : new Random();

            // Piezas idénticas (mismas dimensiones y rotaciones permitidas) se agrupan en un
            // tipo con cantidad > 1, como hace el motor cuando lee instancias de archivo.
            var grupos = new Dictionary<string, List<int>>();
            var orden = new List<string>();
            for (int i = 0; i < piezas.Count; ++i)
            {
                var p = piezas[i];
                string clave = p.DimX + "|" + p.DimY + "|" + p.DimZ + "|" + p.RotX + "|" + p.RotY + "|" + p.RotZ;
                List<int> lista;
                if (!grupos.TryGetValue(clave, out lista))
                {
                    lista = new List<int>();
                    grupos[clave] = lista;
                    orden.Add(clave);
                }
                lista.Add(i);
            }

            foreach (var clave in orden)
            {
                var indices = grupos[clave];
                var p = piezas[indices[0]];
                var bt = new BoxType(BoxTypes.Count, indices.Count, p.DimX, p.DimY, p.DimZ, p.RotX, p.RotY, p.RotZ, Vol);
                if (!bt.EliminarRotacionesQueNoCaben(DimX, DimY, DimZ))
                {
                    IndicesSinCabida.AddRange(indices); // "CUIDADO: se eliminó una pieza porque no alcanza en el contenedor"
                    continue;
                }
                bt.IndicesEntrada.AddRange(indices);
                bt.ActualizarIndice(BoxTypes.Count);
                BoxTypes.Add(bt);
            }

            Cantidades0 = BoxTypes.Select(bt => bt.Cantidad).ToArray();

            double volTotal = 0;
            foreach (var bt in BoxTypes) volTotal += (double)bt.VolumenReal * bt.Cantidad;
            CotaInferior = Math.Max(1, (int)Math.Ceiling(volTotal / Vol - 1e-9));
        }

        public double Rand01()
        {
            return Rnd.NextDouble();
        }

        public int RandInt(int minInclusive, int maxInclusive)
        {
            if (maxInclusive < minInclusive) maxInclusive = minInclusive;
            return Rnd.Next(minInclusive, maxInclusive + 1);
        }

        /// <summary>HacerBloquesGenerales del C++ (rama sin multidrop ni presión: un solo "cliente").</summary>
        public void HacerBloquesGenerales(CancellationToken ct)
        {
            Bloques.Clear();

            long nPiezas = 0;
            foreach (var c in Cantidades0) nPiezas += c;
            long N = nPiezas * Par.MaxNBloques;

            // Bloques de una sola caja
            foreach (var bt in BoxTypes)
            {
                foreach (var b in bt.Boxes) Bloques.Add(new Bloque(b, bt, this));
            }
            if (Bloques.Count >= N) return;

            // Bloques generales: se combinan todos los bloques con los bloques nuevos de la pasada anterior
            int lo = 0;
            int hi = Bloques.Count - 1;
            bool seguir = true;
            while (seguir)
            {
                int n0 = Bloques.Count;
                for (int i1 = n0 - 1; i1 >= 0 && seguir; --i1)
                {
                    for (int i2 = hi; i2 >= lo && seguir; --i2)
                    {
                        for (int esc = 1; esc <= 6; ++esc)
                        {
                            var nuevo = new Bloque(Bloques[i1], Bloques[i2], esc, this);
                            if (!nuevo.Sirve) continue;

                            bool duplicado = false;
                            for (int i3 = Bloques.Count - 1; i3 >= 0; --i3)
                            {
                                if (Bloques[i3].EsIgual(nuevo)) { duplicado = true; break; }
                            }
                            if (duplicado) continue;

                            Bloques.Add(nuevo);
                            if (Bloques.Count >= N) { seguir = false; break; }
                        }
                    }
                    if (ct.IsCancellationRequested) return;
                }
                if (!seguir) break;
                if (Bloques.Count == n0) seguir = false; // no se agregó ningún bloque nuevo
                else { lo = n0; hi = Bloques.Count - 1; }
            }
        }
    }

    // ---------------------------------------------------------------------------------
    //  PackedBox
    // ---------------------------------------------------------------------------------

    /// <summary>Caja ubicada. Invariante: Id == índice en la lista que la contiene.</summary>
    internal sealed class PackedBox
    {
        public int Id;
        public int IdBloque;           // bloque general del que proviene (para desarmar la solución por bloques)
        public int Contenedor;         // contenedor en el que está ubicada (bin packing)
        public int X1, Y1, Z1, X2, Y2, Z2;
        public double Cx, Cy;          // centro geométrico de la base
        public BoxType Bt;
        public Box B;

        public PackedBox(Box b, BoxType bt)
        {
            B = b;
            Bt = bt;
            X1 = 0; Y1 = 0; Z1 = 0;
            X2 = b.X; Y2 = b.Y; Z2 = b.Z;
            Cx = X2 / 2.0;
            Cy = Y2 / 2.0;
            Id = 0;
            IdBloque = 0;
        }

        public PackedBox Clone()
        {
            return (PackedBox)MemberwiseClone();
        }

        public void Desplazar(int dx, int dy, int dz)
        {
            X1 += dx; X2 += dx; Cx += dx;
            Y1 += dy; Y2 += dy; Cy += dy;
            Z1 += dz; Z2 += dz;
        }

        public bool SolapaXY(int x1, int y1, int x2, int y2)
        {
            return !(X1 >= x2 || x1 >= X2 || Y1 >= y2 || y1 >= Y2);
        }
    }

    // ---------------------------------------------------------------------------------
    //  Bloque
    // ---------------------------------------------------------------------------------

    /// <summary>Bloque general: una o varias cajas formando un paralelepípedo casi lleno.</summary>
    internal sealed class Bloque
    {
        public int Dx, Dy, Dz;
        public long Vol, VolOcupado;
        public double VolContenedor;
        public List<PackedBox> Cajas = new List<PackedBox>();
        public int[] Cantidades;        // cantidad de cada tipo de caja dentro del bloque
        public bool Sirve;              // si el bloque se pudo construir

        public int Bf1, Bf2, Bf3;       // métricas best-fit (scratch, se recalculan por espacio)

        public Bloque(Box b, BoxType bt, Instancia inst)
        {
            Cajas.Add(new PackedBox(b, bt));
            Dx = b.X;
            Dy = b.Y;
            Dz = b.Z;
            Vol = bt.VolumenReal;
            VolContenedor = bt.VolumenContenedor;
            VolOcupado = bt.VolumenReal;
            Cantidades = new int[inst.BoxTypes.Count];
            Cantidades[bt.Id] = 1;
            Sirve = true;
        }

        /// <param name="esc">1: b1 -x / b2 +x, 2: b2 -x / b1 +x, 3: b1 -y / b2 +y, 4: b2 -y / b1 +y, 5: b1 abajo / b2 arriba, 6: b2 abajo / b1 arriba.</param>
        public Bloque(Bloque b1, Bloque b2, int esc, Instancia inst)
        {
            Sirve = true;
            Cantidades = b1.Cantidades;

            // Dimensiones: el bloque debe estar dentro del contenedor
            if (esc <= 2)
            {
                Dx = b1.Dx + b2.Dx;
                if (Dx > inst.DimX) { Sirve = false; return; }
                Dy = Math.Max(b1.Dy, b2.Dy);
                Dz = Math.Max(b1.Dz, b2.Dz);
            }
            else if (esc <= 4)
            {
                Dy = b1.Dy + b2.Dy;
                if (Dy > inst.DimY) { Sirve = false; return; }
                Dx = Math.Max(b1.Dx, b2.Dx);
                Dz = Math.Max(b1.Dz, b2.Dz);
            }
            else
            {
                Dz = b1.Dz + b2.Dz;
                if (Dz > inst.DimZ) { Sirve = false; return; }
                Dx = Math.Max(b1.Dx, b2.Dx);
                Dy = Math.Max(b1.Dy, b2.Dy);
            }

            // Utilización volumétrica del bloque
            Vol = (long)Dx * Dy * Dz;
            VolOcupado = b1.VolOcupado + b2.VolOcupado;
            if ((double)VolOcupado / Vol < inst.Par.UtilizacionMaximaBloque) { Sirve = false; return; }

            // Cantidades
            VolContenedor = b1.VolContenedor + b2.VolContenedor;
            Cantidades = (int[])b1.Cantidades.Clone();
            for (int i = 0; i < Cantidades.Length; ++i)
            {
                Cantidades[i] += b2.Cantidades[i];
                if (Cantidades[i] > inst.Cantidades0[i]) { Sirve = false; return; }
            }

            // Escenarios
            Bloque primero, segundo;
            int delta;
            switch (esc)
            {
                case 1: primero = b1; segundo = b2; delta = b1.Dx; break;
                case 2: primero = b2; segundo = b1; delta = b2.Dx; break;
                case 3: primero = b1; segundo = b2; delta = b1.Dy; break;
                case 4: primero = b2; segundo = b1; delta = b2.Dy; break;
                case 5: primero = b1; segundo = b2; delta = b1.Dz; break;
                default: primero = b2; segundo = b1; delta = b2.Dz; break;
            }
            Cajas = new List<PackedBox>(primero.Cajas.Count + segundo.Cajas.Count);
            foreach (var p in primero.Cajas) Cajas.Add(p.Clone());
            int desde = Cajas.Count;
            foreach (var p in segundo.Cajas) Cajas.Add(p.Clone());

            if (esc <= 2) AjustarPiezasEjex(desde, delta);
            else if (esc <= 4) AjustarPiezasEjey(desde, delta);
            else AjustarPiezasEjez(desde, delta);
        }

        void AjustarPiezasEjex(int desde, int deltax)
        {
            for (int i = desde; i < Cajas.Count; ++i)
            {
                var p = Cajas[i];
                p.Id += desde;
                p.Desplazar(deltax, 0, 0);
            }
        }

        void AjustarPiezasEjey(int desde, int deltay)
        {
            for (int i = desde; i < Cajas.Count; ++i)
            {
                var p = Cajas[i];
                p.Id += desde;
                p.Desplazar(0, deltay, 0);
            }
        }

        void AjustarPiezasEjez(int desde, int deltaz)
        {
            // Cajas "top" del bloque inferior
            var cajasTop = new List<PackedBox>();
            for (int i = 0; i < desde; ++i)
            {
                if (Cajas[i].Z2 == deltaz) cajasTop.Add(Cajas[i]);
            }

            var intersecciones = new List<Punto>();
            for (int i = desde; i < Cajas.Count; ++i)
            {
                var p = Cajas[i];
                p.Id += desde;
                p.Desplazar(0, 0, deltaz);

                // Cada caja inferior del bloque superior debe quedar soportada por las cajas top
                if (p.Z1 == deltaz)
                {
                    intersecciones.Clear();
                    foreach (var t in cajasTop)
                    {
                        Geometria.AgregarInterseccionXY(intersecciones, p.X1, p.Y1, p.X2, p.Y2, t.X1, t.Y1, t.X2, t.Y2);
                    }
                    if (intersecciones.Count == 0 || !Geometria.PuntoDentroCascoDe(intersecciones, p.Cx, p.Cy))
                    {
                        Sirve = false;
                        return;
                    }
                }
            }
        }

        /// <summary>Métricas best-fit del bloque respecto a un espacio de dimensiones (dxe, dye, dze).</summary>
        public void DeterminarEspacioMaximal_BS(int dxe, int dye, int dze)
        {
            Bf1 = dxe - Dx;
            Bf2 = dye - Dy;
            Bf3 = dze - Dz;
            Util.OrdenarBf(ref Bf1, ref Bf2, ref Bf3);
        }

        public bool Cabe(MaximalSpace e)
        {
            return Dx <= e.Dx && Dy <= e.Dy && Dz <= e.Dz;
        }

        /// <summary>operator== del C++: mismas dimensiones, volúmenes, número de cajas y cantidades por tipo.</summary>
        public bool EsIgual(Bloque o)
        {
            if (Dx != o.Dx || Dy != o.Dy || Dz != o.Dz || Vol != o.Vol || VolOcupado != o.VolOcupado || Cajas.Count != o.Cajas.Count) return false;
            for (int i = 0; i < Cantidades.Length; ++i)
            {
                if (Cantidades[i] != o.Cantidades[i]) return false;
            }
            return true;
        }

        public static int CompararBf(Bloque a, Bloque b)
        {
            if (a.Bf1 != b.Bf1) return a.Bf1.CompareTo(b.Bf1);
            if (a.Bf2 != b.Bf2) return a.Bf2.CompareTo(b.Bf2);
            return a.Bf3.CompareTo(b.Bf3);
        }
    }

    // ---------------------------------------------------------------------------------
    //  MaximalSpace
    // ---------------------------------------------------------------------------------

    internal sealed class MaximalSpace
    {
        /// <summary>Esquina donde se ubica el bloque: 1: (-x,-y), 2: (-x,+y), 3: (+x,-y), 4: (+x,+y).</summary>
        public int IndEsquina;
        public int Metrica;
        public int X1, Y1, Z1, X2, Y2, Z2, Dx, Dy, Dz;
        public bool Cambio;                 // hay que recalcular IndCajas / esquina
        public List<int> IndCajas;          // índices de las cajas sobre las que se apoya el espacio (z1 > 0)

        /// <summary>
        /// Contenedor al que pertenece el espacio. Extensión para bin packing: la lista de espacios
        /// maximales puede contener espacios de varios contenedores abiertos a la vez; todas las
        /// operaciones (partir, contener, soporte) solo relacionan espacios y cajas del mismo contenedor.
        /// </summary>
        public int Contenedor;

        public MaximalSpace(int x1, int y1, int z1, int x2, int y2, int z2, int contenedor)
        {
            X1 = x1; Y1 = y1; Z1 = z1;
            X2 = x2; Y2 = y2; Z2 = z2;
            Dx = x2 - x1;
            Dy = y2 - y1;
            Dz = z2 - z1;
            IndCajas = new List<int>();
            Cambio = true;
            IndEsquina = -1;
            Metrica = 0;
            Contenedor = contenedor;
        }

        /// <summary>Espacio igual a todo el contenedor (constructor por defecto del C++).</summary>
        public static MaximalSpace Completo(int dimX, int dimY, int dimZ, int contenedor)
        {
            var e = new MaximalSpace(0, 0, 0, dimX, dimY, dimZ, contenedor);
            e.IndEsquina = 1;
            e.Cambio = false;
            return e;
        }

        public MaximalSpace Clone()
        {
            var c = (MaximalSpace)MemberwiseClone();
            c.IndCajas = new List<int>(IndCajas);
            return c;
        }

        public bool MismosLimites(MaximalSpace o)
        {
            return X1 == o.X1 && Y1 == o.Y1 && Z1 == o.Z1 && X2 == o.X2 && Y2 == o.Y2 && Z2 == o.Z2;
        }

        public bool Intersecta(BloqueEmpacado c)
        {
            return Contenedor == c.Contenedor && !(X1 >= c.X2 || Y1 >= c.Y2 || Z1 >= c.Z2 || c.X1 >= X2 || c.Y1 >= Y2 || c.Z1 >= Z2);
        }

        public bool Contiene(MaximalSpace o)
        {
            return Contenedor == o.Contenedor && Z1 <= o.Z1 && o.Z2 <= Z2 && Y1 <= o.Y1 && o.Y2 <= Y2 && X1 <= o.X1 && o.X2 <= X2;
        }
    }

    // ---------------------------------------------------------------------------------
    //  BloqueEmpacado
    // ---------------------------------------------------------------------------------

    /// <summary>Un bloque ubicado en una esquina de un espacio maximal.</summary>
    internal sealed class BloqueEmpacado
    {
        public Bloque B;                 // null para los bloques ficticios
        public int X1, Y1, Z1, X2, Y2, Z2;
        public int Bf1, Bf2, Bf3;        // métricas best-fit
        public int Contenedor;           // contenedor del espacio donde se ubica

        public BloqueEmpacado(Bloque b, MaximalSpace e)
        {
            B = b;
            Contenedor = e.Contenedor;
            switch (e.IndEsquina)
            {
                case 2: X1 = e.X1; Y1 = e.Y2 - b.Dy; break;          // -x, +y
                case 3: X1 = e.X2 - b.Dx; Y1 = e.Y1; break;          // +x, -y
                case 4: X1 = e.X2 - b.Dx; Y1 = e.Y2 - b.Dy; break;   // +x, +y
                default: X1 = e.X1; Y1 = e.Y1; break;                // -x, -y
            }
            X2 = X1 + b.Dx;
            Y2 = Y1 + b.Dy;
            Z1 = e.Z1;
            Z2 = Z1 + b.Dz;

            Bf1 = e.Dz - b.Dz;
            Bf2 = e.Dy - b.Dy;
            Bf3 = e.Dx - b.Dx;
            Util.OrdenarBf(ref Bf1, ref Bf2, ref Bf3);
        }

        /// <summary>Bloque ficticio (solo coordenadas) para reconstruir los espacios maximales al retirar piezas.</summary>
        public BloqueEmpacado(int x1, int y1, int z1, int x2, int y2, int z2, int contenedor)
        {
            B = null;
            X1 = x1; Y1 = y1; Z1 = z1;
            X2 = x2; Y2 = y2; Z2 = z2;
            Contenedor = contenedor;
        }

        /// <summary>Orden_BestFit_MaxVol del C++.</summary>
        public static int OrdenBestFitMaxVol(BloqueEmpacado b1, BloqueEmpacado b2)
        {
            if (b1.Bf1 != b2.Bf1) return b1.Bf1.CompareTo(b2.Bf1);
            if (b1.Bf2 != b2.Bf2) return b1.Bf2.CompareTo(b2.Bf2);
            if (b1.Bf3 != b2.Bf3) return b1.Bf3.CompareTo(b2.Bf3);
            if (b1.B.VolOcupado != b2.B.VolOcupado) return b2.B.VolOcupado.CompareTo(b1.B.VolOcupado); // mayor volumen ocupado primero
            return b1.B.Vol.CompareTo(b2.B.Vol);
        }

        /// <summary>Orden_MaxVol_BestFit del C++.</summary>
        public static int OrdenMaxVolBestFit(BloqueEmpacado b1, BloqueEmpacado b2)
        {
            if (b1.B.VolOcupado != b2.B.VolOcupado) return b2.B.VolOcupado.CompareTo(b1.B.VolOcupado);
            if (b1.B.Vol != b2.B.Vol) return b1.B.Vol.CompareTo(b2.B.Vol);
            if (b1.Bf1 != b2.Bf1) return b1.Bf1.CompareTo(b2.Bf1);
            if (b1.Bf2 != b2.Bf2) return b1.Bf2.CompareTo(b2.Bf2);
            return b1.Bf3.CompareTo(b2.Bf3);
        }
    }

    // ---------------------------------------------------------------------------------
    //  Operaciones sobre listas de espacios maximales (compartidas por el solver offline y el online)
    // ---------------------------------------------------------------------------------

    internal static class EspaciosMaximales
    {
        /// <summary>
        /// ActualizarEspacios / ActualizarEspaciosFast del C++: los espacios que intersectan al bloque
        /// se parten en hasta 6 sub-espacios.
        /// topCompleto = true  -> el espacio sobre el bloque conserva la huella del espacio original
        ///                        (r_estabilidad = 1, lista "espacios").
        /// topCompleto = false -> el espacio sobre el bloque se limita a la intersección en planta
        ///                        (lista "espaciosFast" / soporte completo).
        /// </summary>
        public static void Actualizar(List<MaximalSpace> le, BloqueEmpacado c, bool topCompleto)
        {
            var nuevos = new List<MaximalSpace>();
            for (int i = le.Count - 1; i >= 0; --i)
            {
                var e = le[i];
                if (!e.Intersecta(c)) continue; // solo espacios del mismo contenedor que se solapan con el bloque
                int k = e.Contenedor;

                // en x
                if (c.X1 > e.X1) nuevos.Add(new MaximalSpace(e.X1, e.Y1, e.Z1, c.X1, e.Y2, e.Z2, k));
                if (e.X2 > c.X2) nuevos.Add(new MaximalSpace(c.X2, e.Y1, e.Z1, e.X2, e.Y2, e.Z2, k));
                // en y
                if (c.Y1 > e.Y1) nuevos.Add(new MaximalSpace(e.X1, e.Y1, e.Z1, e.X2, c.Y1, e.Z2, k));
                if (e.Y2 > c.Y2) nuevos.Add(new MaximalSpace(e.X1, c.Y2, e.Z1, e.X2, e.Y2, e.Z2, k));
                // en z
                if (c.Z1 > e.Z1) nuevos.Add(new MaximalSpace(e.X1, e.Y1, e.Z1, e.X2, e.Y2, c.Z1, k));
                if (e.Z2 > c.Z2)
                {
                    if (topCompleto)
                    {
                        nuevos.Add(new MaximalSpace(e.X1, e.Y1, c.Z2, e.X2, e.Y2, e.Z2, k));
                    }
                    else
                    {
                        int ax1 = Math.Max(c.X1, e.X1), ay1 = Math.Max(c.Y1, e.Y1);
                        int ax2 = Math.Min(c.X2, e.X2), ay2 = Math.Min(c.Y2, e.Y2);
                        if (ax2 > ax1 && ay2 > ay1) nuevos.Add(new MaximalSpace(ax1, ay1, c.Z2, ax2, ay2, e.Z2, k));
                    }
                }
                le.RemoveAt(i);
            }
            le.AddRange(nuevos);
        }

        /// <summary>
        /// JuntarEspacios del C++ con r_juntarEspacios = 0:
        ///  1) si justo sobre el techo de un espacio hay una caja, el espacio se baja 1 unidad
        ///     (para que una caja nueva no quede tocando una caja ya ubicada por debajo);
        ///  2) se eliminan los espacios contenidos en otros.
        /// </summary>
        public static void Juntar(List<MaximalSpace> le, List<PackedBox> empacados, int contDimZ)
        {
            foreach (var e in le)
            {
                if (e.Z2 >= contDimZ) continue;
                for (int k = 0; k < empacados.Count; ++k)
                {
                    var p = empacados[k];
                    if (p.Contenedor == e.Contenedor && e.Z2 == p.Z1 && p.SolapaXY(e.X1, e.Y1, e.X2, e.Y2))
                    {
                        e.Dz -= 1;
                        e.Z2 -= 1;
                        break;
                    }
                }
            }

            for (int i = 0; i < le.Count; ++i)
            {
                bool eliminadoI = false;
                for (int j = i + 1; j < le.Count;)
                {
                    if (le[j].Contiene(le[i]))
                    {
                        le.RemoveAt(i);
                        eliminadoI = true;
                        break;
                    }
                    if (le[i].Contiene(le[j])) le.RemoveAt(j);
                    else ++j;
                }
                if (eliminadoI) --i;
            }
        }

        /// <summary>EliminarEspaciosMinD: quita los espacios donde ya no cabe ninguna caja pendiente.</summary>
        public static void EliminarMinD(List<MaximalSpace> le, int minDx, int minDy, int minDz)
        {
            le.RemoveAll(e => e.Dx < minDx || e.Dy < minDy || e.Dz < minDz);
        }

        /// <summary>
        /// DeterminarIndicesCajasUnEspacio: cajas cuyo techo coincide con el piso del espacio y se
        /// solapan con él en planta (todas, como en los modos 1 y 2 del C++; el modo 0 solo guardaba la primera).
        /// </summary>
        public static void DeterminarIndicesCajas(MaximalSpace e, List<PackedBox> empacados, int contDimX, int contDimY)
        {
            e.Cambio = false;
            if (e.Z1 > 0)
            {
                e.IndCajas.Clear();
                for (int k = 0; k < empacados.Count; ++k)
                {
                    var p = empacados[k];
                    if (p.Contenedor == e.Contenedor && p.Z2 == e.Z1 && p.SolapaXY(e.X1, e.Y1, e.X2, e.Y2)) e.IndCajas.Add(p.Id);
                }
            }
            DeterminarEsquina(e, empacados, contDimX, contDimY);
        }

        /// <summary>ActualizarIndicesCajasDeEspacios: recalcula los espacios marcados y elimina los elevados sin soporte.</summary>
        public static void ActualizarIndicesCajas(List<MaximalSpace> le, List<PackedBox> empacados, int contDimX, int contDimY)
        {
            for (int i = 0; i < le.Count;)
            {
                var e = le[i];
                if (e.Cambio)
                {
                    DeterminarIndicesCajas(e, empacados, contDimX, contDimY);
                    if (e.Z1 > 0 && e.IndCajas.Count == 0)
                    {
                        le.RemoveAt(i);
                        continue;
                    }
                }
                ++i;
            }
        }

        static int EsquinaMasCercana(MaximalSpace e, int contDimX, int contDimY)
        {
            int v1 = e.X1 + e.Y1;
            int v2 = e.X1 + contDimY - e.Y2;
            int v3 = contDimX - e.X2 + e.Y1;
            int v4 = contDimX - e.X2 + contDimY - e.Y2;
            if (v1 <= v2 && v1 <= v3 && v1 <= v4) return 1;
            if (v2 <= v1 && v2 <= v3 && v2 <= v4) return 2;
            if (v3 <= v1 && v3 <= v2 && v3 <= v4) return 3;
            return 4;
        }

        /// <summary>DeterminarEsquina del C++ (rama sin multidrop: se consideran las 4 esquinas inferiores).</summary>
        public static void DeterminarEsquina(MaximalSpace e, List<PackedBox> empacados, int contDimX, int contDimY)
        {
            if (e.Z1 == 0)
            {
                e.IndEsquina = EsquinaMasCercana(e, contDimX, contDimY);
                return;
            }

            // Centroide de las áreas de contacto con las cajas de soporte
            double areax = 0, areay = 0, areaTotal = 0;
            foreach (int idx in e.IndCajas)
            {
                var p = empacados[idx];
                int ix1 = Math.Max(e.X1, p.X1), ix2 = Math.Min(e.X2, p.X2);
                int iy1 = Math.Max(e.Y1, p.Y1), iy2 = Math.Min(e.Y2, p.Y2);
                double nuevaArea = (double)(ix2 - ix1) * (iy2 - iy1);
                double px = (ix2 + ix1) / 2.0;
                double py = (iy2 + iy1) / 2.0;
                areax = areax * areaTotal + px * nuevaArea;
                areay = areay * areaTotal + py * nuevaArea;
                areaTotal += nuevaArea;
                areax /= areaTotal;
                areay /= areaTotal;
            }
            double centroEy = (e.Y2 + e.Y1) / 2.0;
            double centroEx = (e.X2 + e.X1) / 2.0;
            bool xCentrado = Math.Abs(areax - centroEx) < Util.Error;
            bool yCentrado = Math.Abs(areay - centroEy) < Util.Error;

            if (xCentrado && yCentrado)
            {
                e.IndEsquina = EsquinaMasCercana(e, contDimX, contDimY);
            }
            else if (xCentrado)
            {
                bool izq = e.X1 <= contDimX - e.X2;
                if (areay < centroEy) e.IndEsquina = izq ? 1 : 3;
                else e.IndEsquina = izq ? 2 : 4;
            }
            else if (yCentrado)
            {
                bool frente = e.Y1 <= contDimY - e.Y2;
                if (areax < centroEx) e.IndEsquina = frente ? 1 : 2;
                else e.IndEsquina = frente ? 3 : 4;
            }
            else if (areax < centroEx && areay < centroEy) e.IndEsquina = 1;
            else if (areax < centroEx && areay > centroEy) e.IndEsquina = 2;
            else if (areax > centroEx && areay < centroEy) e.IndEsquina = 3;
            else e.IndEsquina = 4;
        }

        /// <summary>
        /// Soporte parcial de una caja apoyada sobre las cajas de <paramref name="indCajas"/>:
        /// el centro de su base debe quedar dentro del casco convexo de las áreas de contacto.
        /// </summary>
        public static bool CajaSoportada(int x1, int y1, int x2, int y2, double cx, double cy, List<int> indCajas, List<PackedBox> empacados)
        {
            var inter = new List<Punto>();
            foreach (int idx in indCajas)
            {
                var pp = empacados[idx];
                Geometria.AgregarInterseccionXY(inter, x1, y1, x2, y2, pp.X1, pp.Y1, pp.X2, pp.Y2);
            }
            if (inter.Count == 0) return false;
            return Geometria.PuntoDentroCascoDe(inter, cx, cy);
        }

        /// <summary>
        /// Soporte completo (criterio de SeleccionarBloque_FAST del C++): las 4 esquinas de la base
        /// deben ser esquinas de alguna área de contacto.
        /// </summary>
        public static bool CajaSoporteCompleto(int x1, int y1, int x2, int y2, List<int> indCajas, List<PackedBox> empacados)
        {
            var inter = new List<Punto>();
            foreach (int idx in indCajas)
            {
                var pp = empacados[idx];
                Geometria.AgregarInterseccionXY(inter, x1, y1, x2, y2, pp.X1, pp.Y1, pp.X2, pp.Y2);
            }
            if (inter.Count == 0) return false;
            return ContienePunto(inter, x1, y1) && ContienePunto(inter, x2, y1)
                && ContienePunto(inter, x2, y2) && ContienePunto(inter, x1, y2);
        }

        static bool ContienePunto(List<Punto> pts, int x, int y)
        {
            foreach (var p in pts)
            {
                if (p.X == x && p.Y == y) return true;
            }
            return false;
        }

        /// <summary>BloqueSoportado del C++: cada caja inferior del bloque debe tener soporte parcial.</summary>
        public static bool BloqueSoportado(BloqueEmpacado b, MaximalSpace e, List<PackedBox> empacados)
        {
            if (e.IndCajas.Count == 0) return true;
            foreach (var p in b.B.Cajas)
            {
                if (p.Z1 != 0) continue;
                if (!CajaSoportada(p.X1 + b.X1, p.Y1 + b.Y1, p.X2 + b.X1, p.Y2 + b.Y1,
                                   p.Cx + b.X1, p.Cy + b.Y1, e.IndCajas, empacados)) return false;
            }
            return true;
        }

        /// <summary>Criterio de soporte completo de SeleccionarBloque_FAST aplicado a todo el bloque.</summary>
        public static bool BloqueSoporteCompleto(BloqueEmpacado b, MaximalSpace e, List<PackedBox> empacados)
        {
            if (e.IndCajas.Count == 0) return true;
            foreach (var p in b.B.Cajas)
            {
                if (p.Z1 != 0) continue;
                if (!CajaSoporteCompleto(p.X1 + b.X1, p.Y1 + b.Y1, p.X2 + b.X1, p.Y2 + b.Y1, e.IndCajas, empacados)) return false;
            }
            return true;
        }
    }

    // ---------------------------------------------------------------------------------
    //  Container
    // ---------------------------------------------------------------------------------

    /// <summary>Solución parcial: espacios libres, cajas ubicadas y bloques todavía disponibles.</summary>
    internal sealed class Container
    {
        readonly Instancia I;

        public int MinDx, MinDy, MinDz;
        public double Alpha = 1;
        public List<MaximalSpace> Espacios;       // espacios para el constructivo GRASP y el beam (soporte parcial)
        public List<MaximalSpace> EspaciosFast;   // espacios para el constructivo rápido de evaluación
        public int[] CantidadesBoxType;
        public double Utilizacion;                // volumen empacado / volumen de UN contenedor (puede superar 1 en bin packing)
        public double PuntajeBW;                  // puntaje de la evaluación del nodo en el beam search
        public long VolEmpacado;
        public List<Bloque> Bloques;              // bloques que todavía se pueden usar
        public readonly List<BloqueEmpacado> Opciones = new List<BloqueEmpacado>();
        public List<PackedBox> Empacados;
        public bool HayCajasPorEmpacar;

        /// <summary>Contenedores abiertos (1 en el problema de un solo contenedor).</summary>
        public int NumContenedores;

        /// <summary>Volumen empacado en cada contenedor.</summary>
        public List<long> VolPorContenedor;

        public Container(Instancia inst)
        {
            I = inst;
            CantidadesBoxType = (int[])inst.Cantidades0.Clone();
            ActualizarMinD();
            Espacios = new List<MaximalSpace>();
            EspaciosFast = new List<MaximalSpace>();
            VolPorContenedor = new List<long>();
            NumContenedores = 0;
            AbrirContenedor();
            Utilizacion = 0;
            Bloques = new List<Bloque>(inst.Bloques);
            Empacados = new List<PackedBox>();
            HayCajasPorEmpacar = inst.Cantidades0.Any(c => c > 0);
        }

        /// <param name="copiaCompleta">true: copia todo el estado (para seguir empacando). false: copia "ligera" para guardar la incumbente.</param>
        public Container(Container c, bool copiaCompleta)
        {
            I = c.I;
            Alpha = c.Alpha;
            Utilizacion = c.Utilizacion;
            HayCajasPorEmpacar = c.HayCajasPorEmpacar;
            NumContenedores = c.NumContenedores;
            VolPorContenedor = new List<long>(c.VolPorContenedor);
            CantidadesBoxType = (int[])c.CantidadesBoxType.Clone();
            Empacados = new List<PackedBox>(c.Empacados.Count);
            foreach (var p in c.Empacados) Empacados.Add(p.Clone());

            if (copiaCompleta)
            {
                MinDx = c.MinDx;
                MinDy = c.MinDy;
                MinDz = c.MinDz;
                Espacios = new List<MaximalSpace>(c.Espacios.Count);
                foreach (var e in c.Espacios) Espacios.Add(e.Clone());
                EspaciosFast = new List<MaximalSpace>(c.EspaciosFast.Count);
                foreach (var e in c.EspaciosFast) EspaciosFast.Add(e.Clone());
                Bloques = new List<Bloque>(c.Bloques);
            }
            else
            {
                Espacios = new List<MaximalSpace>();
                EspaciosFast = new List<MaximalSpace>();
                Bloques = new List<Bloque>();
                VolEmpacado = 0;
                foreach (var p in Empacados) VolEmpacado += p.Bt.VolumenReal;
            }
        }

        // ---- contenedores (bin packing) ---------------------------------------------------

        /// <summary>Abre un contenedor nuevo: su espacio completo entra a las dos listas de espacios maximales.</summary>
        public void AbrirContenedor()
        {
            int k = NumContenedores++;
            VolPorContenedor.Add(0);
            Espacios.Add(MaximalSpace.Completo(I.DimX, I.DimY, I.DimZ, k));
            EspaciosFast.Add(MaximalSpace.Completo(I.DimX, I.DimY, I.DimZ, k));
        }

        /// <summary>
        /// Valor de la solución (mayor es mejor).
        ///  - Un contenedor: utilización (volumen cargado).
        ///  - Bin packing: -NumContenedores + (suma de utilizaciones al cuadrado) / NumContenedores.
        ///    El segundo término está en (0, 1], así que primero manda el número de contenedores y, a
        ///    igualdad, se prefiere la solución más concentrada (contenedores llenos y uno casi vacío),
        ///    que es la que más fácilmente permite eliminar un contenedor.
        /// </summary>
        public double Puntaje
        {
            get
            {
                if (!I.ModoBins) return Utilizacion;
                double sumSq = 0;
                foreach (long v in VolPorContenedor)
                {
                    double u = v / I.Vol;
                    sumSq += u * u;
                }
                return -NumContenedores + sumSq / Math.Max(1, NumContenedores);
            }
        }

        /// <summary>true cuando ya no tiene sentido seguir buscando: todo empacado (un contenedor) o se alcanzó la cota inferior (bin packing).</summary>
        public bool Terminado
        {
            get { return I.ModoBins ? (!HayCajasPorEmpacar && NumContenedores <= I.CotaInferior) : !HayCajasPorEmpacar; }
        }

        // ---- utilidades internas -------------------------------------------------------

        void ActualizarMinD()
        {
            MinDx = I.DimX;
            MinDy = I.DimY;
            MinDz = I.DimZ;
            for (int i = 0; i < CantidadesBoxType.Length; ++i)
            {
                if (CantidadesBoxType[i] <= 0) continue;
                var bt = I.BoxTypes[i];
                if (bt.MinDx < MinDx) MinDx = bt.MinDx;
                if (bt.MinDy < MinDy) MinDy = bt.MinDy;
                if (bt.MinDz < MinDz) MinDz = bt.MinDz;
            }
        }

        void EliminarBloques()
        {
            Bloques.RemoveAll(ExcedeCantidades);
        }

        bool ExcedeCantidades(Bloque b)
        {
            for (int i = 0; i < CantidadesBoxType.Length; ++i)
            {
                if (b.Cantidades[i] > CantidadesBoxType[i]) return true;
            }
            return false;
        }

        // ---- selección de espacios maximales -----------------------------------------------

        // Con varios contenedores abiertos, primero se llenan los de menor índice.

        /// <summary>Menor x1, luego mayor z1 (fondo del contenedor, de arriba hacia abajo).</summary>
        public static int SeleccionarEspacio_Fondo_Altura(List<MaximalSpace> le)
        {
            int mejor = 0;
            for (int i = 1; i < le.Count; ++i)
            {
                var e = le[i];
                var m = le[mejor];
                if (e.Contenedor != m.Contenedor) { if (e.Contenedor < m.Contenedor) mejor = i; }
                else if (e.X1 < m.X1) mejor = i;
                else if (e.X1 == m.X1)
                {
                    if (e.Z1 > m.Z1) mejor = i;
                    else if (e.Z1 == m.Z1 && e.Metrica < m.Metrica) mejor = i;
                }
            }
            return mejor;
        }

        /// <summary>Menor x1, luego menor z1.</summary>
        public static int SeleccionarEspacio_Fondo_Bajo(List<MaximalSpace> le)
        {
            int mejor = 0;
            for (int i = 1; i < le.Count; ++i)
            {
                var e = le[i];
                var m = le[mejor];
                if (e.Contenedor != m.Contenedor) { if (e.Contenedor < m.Contenedor) mejor = i; }
                else if (e.X1 < m.X1) mejor = i;
                else if (e.X1 == m.X1)
                {
                    if (e.Z1 < m.Z1) mejor = i;
                    else if (e.Z1 == m.Z1 && e.Metrica < m.Metrica) mejor = i;
                }
            }
            return mejor;
        }

        /// <summary>Mayor z1, luego menor x1.</summary>
        public static int SeleccionarEspacio_Altura_Fondo(List<MaximalSpace> le)
        {
            int mejor = 0;
            for (int i = 1; i < le.Count; ++i)
            {
                var e = le[i];
                var m = le[mejor];
                if (e.Contenedor != m.Contenedor) { if (e.Contenedor < m.Contenedor) mejor = i; }
                else if (e.Z1 > m.Z1) mejor = i;
                else if (e.Z1 == m.Z1)
                {
                    if (e.X1 < m.X1) mejor = i;
                    else if (e.X1 == m.X1 && e.Metrica < m.Metrica) mejor = i;
                }
            }
            return mejor;
        }

        // ---- selección de bloques ----------------------------------------------------------

        void GenerarOpcionesBasicas(MaximalSpace e)
        {
            Opciones.Clear();
            foreach (var b in Bloques)
            {
                if (b.Cabe(e)) Opciones.Add(new BloqueEmpacado(b, e));
            }
        }

        int BusquedaBinariaBF(double val)
        {
            int n = Opciones.Count;
            if (Opciones[n - 1].Bf1 < val) return n - 1;
            int l = 0, r = n - 1;
            while (l < r)
            {
                int m = (l + r) / 2;
                if (Opciones[m].Bf1 > val) r = m - 1;
                else l = m + 1;
            }
            return Math.Max(0, Math.Min(r, n - 1));
        }

        int BusquedaBinariaVol(double val)
        {
            int n = Opciones.Count;
            if (Opciones[n - 1].B.Vol < val) return n - 1;
            int l = 0, r = n - 1;
            while (l < r)
            {
                int m = (l + r) / 2;
                if (Opciones[m].B.Vol > val) r = m - 1;
                else l = m + 1;
            }
            return Math.Max(0, Math.Min(r, n - 1));
        }

        bool OpcionValida(BloqueEmpacado be, MaximalSpace e)
        {
            return e.Z1 == 0 || EspaciosMaximales.BloqueSoportado(be, e, Empacados);
        }

        /// <summary>GRASP: lista restringida por best-fit (bf1 &lt;= min + alpha * (max - min)), elección aleatoria.</summary>
        BloqueEmpacado SeleccionarBloque_BestFit(MaximalSpace e)
        {
            Opciones.Sort(BloqueEmpacado.OrdenBestFitMaxVol);
            while (Opciones.Count > 0)
            {
                double lim = Opciones[0].Bf1 + (Opciones[Opciones.Count - 1].Bf1 - Opciones[0].Bf1) * Alpha + Util.Error;
                int idx = I.RandInt(0, BusquedaBinariaBF(lim));
                var resp = Opciones[idx];
                if (OpcionValida(resp, e)) return resp;
                Opciones.RemoveAt(idx);
            }
            return null;
        }

        /// <summary>GRASP: lista ordenada por volumen ocupado, elección aleatoria (misma regla de corte del C++).</summary>
        BloqueEmpacado SeleccionarBloque_MaxVol(MaximalSpace e)
        {
            Opciones.Sort(BloqueEmpacado.OrdenMaxVolBestFit);
            while (Opciones.Count > 0)
            {
                double lim = Opciones[0].B.Vol + (Opciones[Opciones.Count - 1].B.Vol - Opciones[0].B.Vol) * Alpha + Util.Error;
                int idx = I.RandInt(0, BusquedaBinariaVol(lim));
                var resp = Opciones[idx];
                if (OpcionValida(resp, e)) return resp;
                Opciones.RemoveAt(idx);
            }
            return null;
        }

        /// <summary>SeleccionarBloque_FAST: primer bloque por best-fit con soporte completo (determinístico).</summary>
        BloqueEmpacado SeleccionarBloque_FAST(MaximalSpace e)
        {
            var bb = new List<Bloque>();
            foreach (var b in Bloques)
            {
                if (!b.Cabe(e)) continue;
                b.DeterminarEspacioMaximal_BS(e.Dx, e.Dy, e.Dz);
                bb.Add(b);
            }
            if (bb.Count == 0) return null;

            // list::sort del C++ es estable: OrderBy también lo es
            foreach (var b in bb.OrderBy(x => x.Bf1).ThenBy(x => x.Bf2).ThenBy(x => x.Bf3))
            {
                var be = new BloqueEmpacado(b, e);
                if (e.Z1 == 0) return be;
                if (EspaciosMaximales.BloqueSoporteCompleto(be, e, Empacados)) return be;
            }
            return null;
        }

        // ---- empacar -----------------------------------------------------------------------

        void Empacar0(BloqueEmpacado b)
        {
            Utilizacion += b.B.VolContenedor;

            // Cantidades de los tipos de caja
            HayCajasPorEmpacar = false;
            for (int i = 0; i < CantidadesBoxType.Length; ++i)
            {
                CantidadesBoxType[i] -= b.B.Cantidades[i];
                if (CantidadesBoxType[i] > 0) HayCajasPorEmpacar = true;
            }
            for (int i = 0; i < CantidadesBoxType.Length; ++i)
            {
                if (CantidadesBoxType[i] == 0 && b.B.Cantidades[i] > 0)
                {
                    var bt = I.BoxTypes[i];
                    if (bt.MinDx == MinDx || bt.MinDy == MinDy || bt.MinDz == MinDz)
                    {
                        ActualizarMinD();
                        break;
                    }
                }
            }

            // Se empacan las cajas del bloque
            VolPorContenedor[b.Contenedor] += b.B.VolOcupado;
            int nCajas = Empacados.Count;
            int idBloque = nCajas > 0 ? Empacados[nCajas - 1].IdBloque + 1 : 0;
            foreach (var p0 in b.B.Cajas)
            {
                var p = p0.Clone();
                p.IdBloque = idBloque;
                p.Contenedor = b.Contenedor;
                p.Id += nCajas;
                p.Desplazar(b.X1, b.Y1, b.Z1);
                Empacados.Add(p);
            }
        }

        /// <summary>
        /// Bin packing: cuando la lista de espacios está vacía y quedan cajas, ninguna caja pendiente
        /// tiene espacio disponible en los contenedores abiertos, así que se abre uno nuevo.
        /// Devuelve true si la lista quedó con espacios.
        /// </summary>
        bool AsegurarEspacios(List<MaximalSpace> le)
        {
            if (le.Count == 0 && HayCajasPorEmpacar && I.ModoBins) AbrirContenedor();
            return le.Count > 0;
        }

        void ActualizarListaEspacios(List<MaximalSpace> le, BloqueEmpacado b, bool topCompleto)
        {
            EspaciosMaximales.Actualizar(le, b, topCompleto);
            EspaciosMaximales.Juntar(le, Empacados, I.DimZ);
            EspaciosMaximales.EliminarMinD(le, MinDx, MinDy, MinDz);
            EspaciosMaximales.ActualizarIndicesCajas(le, Empacados, I.DimX, I.DimY);
        }

        public void Empacar(BloqueEmpacado b)
        {
            Empacar0(b);
            if (!HayCajasPorEmpacar) return;
            ActualizarListaEspacios(Espacios, b, true);
            ActualizarListaEspacios(EspaciosFast, b, false);
            EliminarBloques();
        }

        public void EmpacarFast(BloqueEmpacado b)
        {
            Empacar0(b);
            if (!HayCajasPorEmpacar) return;
            ActualizarListaEspacios(EspaciosFast, b, false);
            EliminarBloques();
        }

        // ---- constructivos -----------------------------------------------------------------

        /// <summary>Constructivo GRASP aleatorizado (selección de espacio y de bloque con alpha).</summary>
        public void Constructivo()
        {
            while (HayCajasPorEmpacar && AsegurarEspacios(Espacios))
            {
                int iE = I.Rand01() < 0.5 ? SeleccionarEspacio_Fondo_Altura(Espacios) : SeleccionarEspacio_Altura_Fondo(Espacios);
                var e = Espacios[iE];

                GenerarOpcionesBasicas(e);
                if (Opciones.Count > 0)
                {
                    var mejor = I.Rand01() < 0.5 ? SeleccionarBloque_BestFit(e) : SeleccionarBloque_MaxVol(e);
                    if (mejor == null) Espacios.RemoveAt(iE);
                    else Empacar(mejor);
                }
                else Espacios.RemoveAt(iE);
            }
        }

        /// <summary>Constructivo rápido y determinístico (evaluación de los nodos del beam).</summary>
        public void ConstructivoFast()
        {
            while (HayCajasPorEmpacar && AsegurarEspacios(EspaciosFast))
            {
                int iE = SeleccionarEspacio_Fondo_Altura(EspaciosFast);
                var e = EspaciosFast[iE];
                var mejor = SeleccionarBloque_FAST(e);
                if (mejor != null) EmpacarFast(mejor);
                else EspaciosFast.RemoveAt(iE);
            }
        }

        // ---- quitar piezas -----------------------------------------------------------------

        /// <summary>RemoverPiezas: conserva las primeras <paramref name="hasta"/> cajas y reconstruye todo el estado.</summary>
        public void RemoverPiezas(int hasta)
        {
            if (hasta < 0) hasta = 0;
            if (hasta < Empacados.Count) Empacados.RemoveRange(hasta, Empacados.Count - hasta);

            // Cantidades, minD y utilización
            CantidadesBoxType = (int[])I.Cantidades0.Clone();
            foreach (var p in Empacados) --CantidadesBoxType[p.Bt.Id];
            ActualizarMinD();
            Utilizacion = 0;
            HayCajasPorEmpacar = false;
            for (int i = 0; i < CantidadesBoxType.Length; ++i)
            {
                var bt = I.BoxTypes[i];
                if (CantidadesBoxType[i] < bt.Cantidad) Utilizacion += bt.VolumenContenedor * (bt.Cantidad - CantidadesBoxType[i]);
                if (CantidadesBoxType[i] > 0) HayCajasPorEmpacar = true;
            }

            // Contenedores: se conservan los que todavía tienen cajas (las cajas se empacan en orden de contenedor)
            Espacios = new List<MaximalSpace>();
            EspaciosFast = new List<MaximalSpace>();
            VolPorContenedor = new List<long>();
            NumContenedores = 0;
            int maxCont = 0;
            foreach (var p in Empacados) if (p.Contenedor > maxCont) maxCont = p.Contenedor;
            while (NumContenedores <= maxCont) AbrirContenedor();
            foreach (var p in Empacados) VolPorContenedor[p.Contenedor] += p.Bt.VolumenReal;

            // Espacios maximales: se parte de cada contenedor completo y se "empaca" un bloque ficticio por cada bloque ya ubicado
            if (Empacados.Count > 0)
            {
                var p0 = Empacados[0];
                int x1 = p0.X1, y1 = p0.Y1, z1 = p0.Z1, x2 = p0.X2, y2 = p0.Y2, z2 = p0.Z2;
                int idBloque = p0.IdBloque;
                int cont = p0.Contenedor;
                for (int k = 1; k < Empacados.Count; ++k)
                {
                    var p = Empacados[k];
                    if (p.IdBloque == idBloque && p.Contenedor == cont)
                    {
                        if (x1 > p.X1) x1 = p.X1;
                        if (y1 > p.Y1) y1 = p.Y1;
                        if (z1 > p.Z1) z1 = p.Z1;
                        if (x2 < p.X2) x2 = p.X2;
                        if (y2 < p.Y2) y2 = p.Y2;
                        if (z2 < p.Z2) z2 = p.Z2;
                    }
                    else
                    {
                        EmpacarFicticio(x1, y1, z1, x2, y2, z2, cont);
                        x1 = p.X1; y1 = p.Y1; z1 = p.Z1;
                        x2 = p.X2; y2 = p.Y2; z2 = p.Z2;
                        idBloque = p.IdBloque;
                        cont = p.Contenedor;
                    }
                }
                EmpacarFicticio(x1, y1, z1, x2, y2, z2, cont);
            }
            EspaciosMaximales.EliminarMinD(Espacios, MinDx, MinDy, MinDz);
            EspaciosMaximales.ActualizarIndicesCajas(Espacios, Empacados, I.DimX, I.DimY);
            EspaciosMaximales.EliminarMinD(EspaciosFast, MinDx, MinDy, MinDz);
            EspaciosMaximales.ActualizarIndicesCajas(EspaciosFast, Empacados, I.DimX, I.DimY);

            // Bloques válidos
            Bloques = new List<Bloque>(I.Bloques);
            EliminarBloques();
        }

        void EmpacarFicticio(int x1, int y1, int z1, int x2, int y2, int z2, int contenedor)
        {
            var ficticio = new BloqueEmpacado(x1, y1, z1, x2, y2, z2, contenedor);
            EspaciosMaximales.Actualizar(Espacios, ficticio, true);
            EspaciosMaximales.Juntar(Espacios, Empacados, I.DimZ);
            EspaciosMaximales.Actualizar(EspaciosFast, ficticio, false);
            EspaciosMaximales.Juntar(EspaciosFast, Empacados, I.DimZ);
        }

        // ---- beam search ---------------------------------------------------------------------

        /// <summary>
        /// ObtenerOpcionesBeam: hijos de este nodo. Se expanden hasta 3 espacios distintos (elegidos
        /// con Altura_Fondo, Fondo_Altura y Fondo_Bajo) y en cada uno hasta beamWidth bloques por best-fit.
        /// Los espacios en los que no cabe ningún bloque se eliminan de este contenedor.
        /// </summary>
        public List<Container> ObtenerOpcionesBeam()
        {
            var resp = new List<Container>();
            // Bin packing: si ninguna caja pendiente tiene espacio en los contenedores abiertos, se abre uno nuevo
            // (como mucho una vez: en un contenedor vacío siempre cabe algún bloque).
            for (int intento = 0; intento < 2 && resp.Count == 0 && HayCajasPorEmpacar && AsegurarEspacios(Espacios); ++intento)
            {
                ExpandirNodo(resp);
            }
            return resp;
        }

        void ExpandirNodo(List<Container> resp)
        {
            var esp = new List<MaximalSpace>();

            // Primer espacio
            int agregados = 0;
            while (agregados == 0 && Espacios.Count > 0)
            {
                int iE = SeleccionarEspacio_Altura_Fondo(Espacios);
                var e = Espacios[iE];
                agregados = ExpandirEspacio(e, resp);
                if (agregados == 0) Espacios.RemoveAt(iE);
                else esp.Add(e);
            }

            // Segundo espacio
            agregados = 0;
            while (agregados == 0 && Espacios.Count > esp.Count && esp.Count > 0)
            {
                int iE = SeleccionarEspacio_Fondo_Altura(Espacios);
                var e = Espacios[iE];
                if (e.MismosLimites(esp[esp.Count - 1])) break;
                agregados = ExpandirEspacio(e, resp);
                if (agregados == 0) Espacios.RemoveAt(iE);
                else esp.Add(e);
            }

            // Tercer espacio
            agregados = 0;
            while (agregados == 0 && Espacios.Count > esp.Count)
            {
                int iE = SeleccionarEspacio_Fondo_Bajo(Espacios);
                var e = Espacios[iE];
                bool repetido = false;
                foreach (var x in esp)
                {
                    if (e.MismosLimites(x)) { repetido = true; break; }
                }
                if (repetido) break;
                agregados = ExpandirEspacio(e, resp);
                if (agregados == 0) Espacios.RemoveAt(iE);
            }
        }

        int ExpandirEspacio(MaximalSpace e, List<Container> resp)
        {
            var candidatos = new List<Bloque>();
            foreach (var b in Bloques)
            {
                if (!b.Cabe(e)) continue;
                b.DeterminarEspacioMaximal_BS(e.Dx, e.Dy, e.Dz);
                candidatos.Add(b);
            }

            int agregados = 0;
            foreach (var b in candidatos.OrderBy(x => x.Bf1).ThenBy(x => x.Bf2).ThenBy(x => x.Bf3))
            {
                var nuevo = new BloqueEmpacado(b, e);
                if (!OpcionValida(nuevo, e)) continue;
                var hijo = new Container(this, true);
                hijo.Empacar(nuevo);
                resp.Add(hijo);
                if (++agregados == I.Par.BeamWidth) break;
            }
            return agregados;
        }

        // ---- métricas para decidir desde dónde desarmar la solución ------------------------------

        /// <summary>
        /// MetricaIncumbente: recorre la incumbente por bloques hasta acumular ~porcentaje del volumen
        /// empacado y devuelve ese volumen y 0.9 * (volumen / área de piso ocupada).
        /// </summary>
        public void MetricaIncumbente(double porcentaje, out long volHasta, out double metrica)
        {
            const double factor = 0.9;
            long miVol = (long)(VolEmpacado * porcentaje);
            long areaB = 0, areaTotal = 0, volB = 0, volTotal = 0;
            int idBloque = 0;
            int k = 0;
            while (k < Empacados.Count)
            {
                var p = Empacados[k];
                if (p.IdBloque == idBloque)
                {
                    volB += p.Bt.VolumenReal;
                    if (p.Z1 == 0) areaB += p.B.Area;
                    ++k;
                }
                else
                {
                    if (volB + volTotal >= miVol)
                    {
                        if (miVol - volTotal <= volB + volTotal - miVol)
                        {
                            volHasta = volTotal;
                            metrica = factor * (double)volTotal / (double)areaTotal;
                        }
                        else
                        {
                            volHasta = volB + volTotal;
                            metrica = factor * (double)(volB + volTotal) / (double)(areaB + areaTotal);
                        }
                        return;
                    }
                    volTotal += volB;
                    volB = 0;
                    areaTotal += areaB;
                    areaB = 0;
                    idBloque = p.IdBloque;
                }
            }
            if (miVol - volTotal <= volB + volTotal - miVol)
            {
                volHasta = volTotal;
                metrica = factor * (double)volTotal / (double)areaTotal;
            }
            else
            {
                volHasta = volB + volTotal;
                metrica = factor * (double)(volB + volTotal) / (double)(areaB + areaTotal);
            }
        }

        /// <summary>
        /// MetricaNoIncumente: misma métrica (sin factor) para la solución actual hasta el volumen
        /// <paramref name="miVol"/>, junto con el número de cajas que se conservan.
        /// </summary>
        public void MetricaNoIncumbente(long miVol, out double metrica, out int hasta)
        {
            long volB = 0, volTotal = 0, areaB = 0, areaTotal = 0;
            int hastaB = 0, hastaTotal = 0;
            int idBloque = 0;
            int k = 0;
            while (k < Empacados.Count)
            {
                var p = Empacados[k];
                if (idBloque == p.IdBloque)
                {
                    volB += p.Bt.VolumenReal;
                    if (p.Z1 == 0) areaB += p.B.Area;
                    ++hastaB;
                    ++k;
                }
                else
                {
                    if (volB + volTotal >= miVol)
                    {
                        if (miVol - volTotal <= volB + volTotal - miVol)
                        {
                            metrica = (double)volTotal / (double)areaTotal;
                            hasta = hastaTotal;
                        }
                        else
                        {
                            metrica = (double)(volB + volTotal) / (double)(areaB + areaTotal);
                            hasta = hastaB + hastaTotal;
                        }
                        return;
                    }
                    hastaTotal += hastaB;
                    hastaB = 0;
                    volTotal += volB;
                    volB = 0;
                    areaTotal += areaB;
                    areaB = 0;
                    idBloque = p.IdBloque;
                }
            }
            metrica = 0;
            hasta = 0;
        }
    }
}
