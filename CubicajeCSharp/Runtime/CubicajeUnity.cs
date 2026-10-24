// =====================================================================================
//  Integración con Unity / HoloLens 2.
//
//  - CubicajeEjes: conversión entre los ejes del motor (Z vertical) y los de Unity (Y vertical).
//  - CubicajeSolverBehaviour: MonoBehaviour que corre el solver offline en un hilo aparte y
//    entrega el resultado en el hilo principal (no bloquea el render del HoloLens).
//
//  Este archivo solo compila dentro de Unity (#if UNITY_5_3_OR_NEWER); fuera de Unity se ignora,
//  así toda la carpeta Runtime se puede copiar tal cual a Assets/.
// =====================================================================================
#if UNITY_5_3_OR_NEWER
using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;

namespace Cubicaje
{
    /// <summary>
    /// Motor:  X = ancho, Y = fondo, Z = alto.
    /// Unity:  X = ancho, Y = alto,  Z = fondo.
    /// Es el mismo mapeo que usaba el P/Invoke a la DLL (alto de Unity -> dimz del motor).
    /// </summary>
    public static class CubicajeEjes
    {
        /// <summary>Tamaño en unidades enteras de Unity (x ancho, y alto, z fondo) -> motor (x, y fondo, z alto).</summary>
        public static Vector3Int AMotor(Vector3Int unity)
        {
            return new Vector3Int(unity.x, unity.z, unity.y);
        }

        /// <summary>Coordenadas/tamaño del motor -> Unity.</summary>
        public static Vector3Int AUnity(int x, int y, int z)
        {
            return new Vector3Int(x, z, y);
        }

        /// <summary>Convierte metros a las unidades enteras del motor (por defecto milímetros).</summary>
        public static int AUnidades(float metros, float unidadMetros = 0.001f)
        {
            return Mathf.RoundToInt(metros / unidadMetros);
        }

        /// <summary>Crea una pieza del motor a partir de un tamaño en metros de Unity (x ancho, y alto, z fondo).</summary>
        public static PiezaEntrada PiezaDesdeUnity(string id, Vector3 tamanoMetros, float unidadMetros = 0.001f, bool mantenerVertical = false)
        {
            return new PiezaEntrada(id,
                AUnidades(tamanoMetros.x, unidadMetros),
                AUnidades(tamanoMetros.z, unidadMetros),
                AUnidades(tamanoMetros.y, unidadMetros),
                mantenerVertical);
        }

        /// <summary>Tamaño de la caja colocada en metros, en ejes de Unity.</summary>
        public static Vector3 TamanoEnUnity(PiezaColocada p, float unidadMetros = 0.001f)
        {
            return new Vector3(p.DimX, p.DimZ, p.DimY) * unidadMetros;
        }

        /// <summary>
        /// Centro de la caja colocada en metros, relativo a la esquina (0,0,0) del contenedor
        /// (esquina inferior, izquierda, trasera). Para ubicarla en la escena:
        ///     obj.transform.position = contenedor.TransformPoint(CentroLocalEnUnity(p));
        /// </summary>
        public static Vector3 CentroLocalEnUnity(PiezaColocada p, float unidadMetros = 0.001f)
        {
            return new Vector3((p.X1 + p.X2) * 0.5f, (p.Z1 + p.Z2) * 0.5f, (p.Y1 + p.Y2) * 0.5f) * unidadMetros;
        }
    }

    /// <summary>
    /// Ejecuta el solver offline en segundo plano. Uso:
    ///     solver.AlTerminar += r => { foreach (var p in r.Colocadas) ... };
    ///     solver.Resolver(piezas);
    /// </summary>
    public sealed class CubicajeSolverBehaviour : MonoBehaviour
    {
        [Header("Contenedor en unidades del motor (p. ej. mm). Ejes de Unity: x ancho, y alto, z fondo")]
        public Vector3Int ContenedorUnity = new Vector3Int(1200, 1800, 1000);

        [Header("Parámetros del solver")]
        public float MaxTiempoSeg = 3f;
        public int BeamWidth = 5;
        public int MaxNBloques = 10;

        /// <summary>Se invoca en el hilo principal cuando termina el solver.</summary>
        public event Action<ResultadoCubicaje> AlTerminar;

        public ResultadoCubicaje UltimoResultado { get; private set; }

        public bool Ocupado { get { return tarea != null && !tarea.IsCompleted; } }

        Task<ResultadoCubicaje> tarea;
        CancellationTokenSource cts;

        /// <summary>Lanza el solver. Devuelve false si ya hay una ejecución en curso.</summary>
        public bool Resolver(IList<PiezaEntrada> piezas)
        {
            if (Ocupado) return false;
            cts = new CancellationTokenSource();
            var token = cts.Token;
            var cont = CubicajeEjes.AMotor(ContenedorUnity);
            var copia = new List<PiezaEntrada>(piezas);
            var par = new ParametrosSolver { BeamWidth = BeamWidth, MaxNBloques = MaxNBloques };
            double maxTiempo = MaxTiempoSeg;
            tarea = Task.Run(() => CubicajeSolver.Resolver(cont.x, cont.y, cont.z, copia, maxTiempo, par, token), token);
            return true;
        }

        /// <summary>Interrumpe la ejecución; el resultado entregado será la mejor solución hasta ese momento.</summary>
        public void Cancelar()
        {
            if (cts != null) cts.Cancel();
        }

        void Update()
        {
            if (tarea == null || !tarea.IsCompleted) return;
            var t = tarea;
            tarea = null;
            if (t.Status == TaskStatus.RanToCompletion)
            {
                UltimoResultado = t.Result;
                var h = AlTerminar;
                if (h != null) h(t.Result);
            }
            else if (t.Exception != null)
            {
                Debug.LogException(t.Exception);
            }
        }

        void OnDestroy()
        {
            Cancelar();
        }
    }
}
#endif
