# CubicajeCSharp — motor de cubicaje en C# para Unity / HoloLens 2

Traducción a C# del motor C++ `Intento14 modificado` (GRASP + Beam Search) para usarlo
como scripts de Unity, sin DLL nativa ni P/Invoke. Incluye además una versión **online**
del problema (las cajas llegan una a una).

| Carpeta | Contenido |
|---|---|
| `Runtime/` | Scripts para copiar a `Assets/` del proyecto de Unity. |
| `Pruebas/` | Pruebas de consola (no se copian a Unity): compilan el motor y verifican las soluciones. |

## Problema que resuelve

Container Loading Problem con **un solo contenedor** y un conjunto de cajas conocido
(3D-SKP / 3D-SLOPP): maximizar el volumen cargado respetando contención, no solapamiento y
**estabilidad parcial** (el centro de la base de cada caja debe quedar sobre el casco convexo de
sus áreas de apoyo). Las cajas pueden rotar libremente o con restricciones ("este lado arriba").

## Convención de ejes y unidades

Igual que el motor C++ y que la DLL: **X = ancho, Y = fondo, Z = alto** (`z1 == 0` es el piso).
Unity usa Y como vertical; `CubicajeEjes` hace la conversión (alto de Unity ↔ `DimZ` del motor).
Las unidades son enteras (por ejemplo milímetros).

## Uso en Unity

Copiar `Runtime/*.cs` a `Assets/Cubicaje/`. No requiere paquetes adicionales.

### Offline (todas las cajas conocidas) — reemplazo directo de `Cubicaje_Resolver`

```csharp
using Cubicaje;

var piezas = new List<PiezaEntrada> {
    new PiezaEntrada("A1", 40, 80, 165),              // x, y(fondo), z(alto); rotación libre
    new PiezaEntrada("B1", 80, 80, 120, mantenerVertical: true),
    // o desde Unity:
    CubicajeEjes.PiezaDesdeUnity("C1", tamanoEnMetros, unidadMetros: 0.001f),
};

// Llamada síncrona (bloquea el hilo que la ejecuta; en HoloLens usar el MonoBehaviour o Task.Run)
ResultadoCubicaje r = CubicajeSolver.Resolver(205, 205, 205, piezas, maxTiempoSeg: 3);

foreach (PiezaColocada p in r.Colocadas) {           // en el orden en que el motor las empacó
    Vector3 centro = CubicajeEjes.CentroLocalEnUnity(p);   // metros, relativo a la esquina (0,0,0) del contenedor
    Vector3 tamano = CubicajeEjes.TamanoEnUnity(p);
    // p.IndicePieza = posición en la lista de entrada, p.Id = el Id dado
}
r.NoColocadas;   // índices de las piezas que no cupieron
r.Utilizacion;   // 0..1, equivale a Cubicaje_UltimaUtilizacion()
```

Con el componente `CubicajeSolverBehaviour` (corre en un hilo aparte y avisa en el hilo principal):

```csharp
solver.ContenedorUnity = new Vector3Int(1200, 1800, 1000); // x ancho, y alto, z fondo (mm)
solver.MaxTiempoSeg = 3;
solver.AlTerminar += r => Mostrar(r);
solver.Resolver(piezas);
solver.Cancelar(); // opcional: devuelve la mejor solución encontrada hasta ese momento
```

Parámetros (`ParametrosSolver`): `BeamWidth` (5), `MaxNBloques` (10), `UtilizacionMaximaBloque`
(0.99) y `Semilla` (null = aleatoria, como en C++; con semilla fija el resultado es reproducible).

A diferencia de la DLL, `CubicajeSolver.Resolver` **es reentrante**: no usa estado global y se
puede llamar desde varios hilos a la vez.

### Online (las cajas llegan una a una)

```csharp
var online = new CubicajeOnline(205, 205, 205, new ParametrosOnline {
    Estrategia = EstrategiaOnline.MenorAlturaMejorAjuste,  // por defecto
    CandidatosEvaluados = 5,                                // "beam width" del lookahead
    SoporteCompleto = false                                 // false = soporte parcial
});

PiezaColocada? donde = online.Colocar(piezaQueLlega);                 // null si no cabe
PiezaColocada? donde2 = online.Colocar(piezaQueLlega, proximasConocidas); // con lookahead
online.Cabe(pieza);          // consulta sin modificar el estado
online.Colocadas;            // lo colocado hasta ahora
online.EspaciosLibres();     // huecos actuales, para visualizarlos
online.Reiniciar();
```

Cada caja se ubica en el momento y no se mueve después. Para cada llegada se generan todos los
candidatos (espacio maximal × orientación, en la esquina que elige el motor, con soporte válido) y
se ordenan según la estrategia:

- `MenorAlturaMejorAjuste`: lo más bajo posible, luego mejor ajuste, luego al fondo (construye capas; recomendado para pallets).
- `FondoAlturaMejorAjuste`: al fondo y de arriba hacia abajo (la regla del motor offline), luego mejor ajuste.
- `MejorAjuste`: menor espacio sobrante en el espacio maximal.

Si se conocen las próximas cajas (un buffer), se evalúan los `CandidatosEvaluados` mejores candidatos
completando el buffer con la misma regla voraz y se elige el que deja más volumen cargado: es la misma
idea con la que el Beam Search offline evalúa sus nodos.

### Bin packing (3D-BPP): varios contenedores idénticos

Hay que empacar **todas** las cajas usando el menor número de contenedores. Es una extensión
del concepto de espacios maximales: la lista de espacios contiene espacios de **todos los
contenedores abiertos** (cada espacio y cada caja llevan su índice de contenedor, y partir,
contener y soportar solo relacionan elementos del mismo contenedor). Cuando para las cajas
pendientes no existe espacio disponible en la lista, se abre un contenedor nuevo y su espacio
completo entra a la lista.

**Offline** (`BinPackingSolver`): el mismo GRASP + Beam Search del motor, con el constructivo
abriendo contenedores y con puntaje `-N + Σ(utilización²)/N` (primero menos contenedores `N`;
a igualdad, la solución más concentrada). Se detiene al alcanzar la cota inferior por volumen o
al agotar el tiempo.

```csharp
ResultadoBinPacking r = BinPackingSolver.Resolver(1200, 1000, 1800, piezas, maxTiempoSeg: 5);
r.NumContenedores; r.CotaInferior;
foreach (ResultadoCubicaje c in r.Contenedores)      // uno por contenedor, con su Utilizacion
    foreach (PiezaColocada p in c.Colocadas)          // p.Contenedor = índice del contenedor, p.IndicePieza global
        ...
r.NoColocadas;  // solo piezas que no caben en un contenedor vacío
```

**Online** (`BinPackingOnline`): cada caja que llega se ubica en el mejor espacio de la lista
(estrategia `MejorAjuste` = contenedor más lleno donde cabe, o `PrimerAjuste` = el más antiguo;
dentro del contenedor, la estrategia de posición del online). Si no hay espacio se abre un
contenedor nuevo. `MaxContenedoresAbiertos` limita los abiertos (al abrir uno nuevo se cierra el
más lleno y sus espacios salen de la lista). `Cerrar(k)` cierra uno manualmente (el pallet salió).

```csharp
var bpp = new BinPackingOnline(1200, 1000, 1800, new ParametrosBinPackingOnline {
    Estrategia = EstrategiaBins.MejorAjuste, MaxContenedoresAbiertos = 2 });
PiezaColocada? p = bpp.Colocar(piezaQueLlega, proximasConocidas);  // p.Contenedor dice en cuál va
bpp.NumContenedores; bpp.NumAbiertos; bpp.UtilizacionDe(k); bpp.ColocadasDe(k); bpp.EspaciosLibres(k);
```

## Correspondencia con el motor C++

| C++ (`Intento14.cpp`) | C# |
|---|---|
| `Cubicaje_Resolver`, `Cubicaje_UltimaUtilizacion` | `CubicajeSolver.Resolver` → `ResultadoCubicaje` |
| `Ciclo_BS` | `CicloBS` (CubicajeSolver.cs) |
| `Container` (Constructivo, ConstructivoFast, ObtenerOpcionesBeam, RemoverPiezas, MetricaIncumbente…) | `Container` (CubicajeNucleo.cs) |
| `ActualizarEspacios`, `JuntarEspacios`, `DeterminarEsquina`, `BloqueSoportado`… | `EspaciosMaximales` (compartido con el online) |
| `Bloque`, `HacerBloquesGenerales` | `Bloque`, `Instancia.HacerBloquesGenerales` |
| `Poligono` (casco convexo) | `Geometria` (CubicajeGeometria.cs) |
| — (no existe en C++) | `BinPackingSolver`, `BinPackingOnline` (CubicajeBinPacking.cs); `MotorOnline` (CubicajeOnline.cs) |

Se tradujo la configuración que usa la app (`r_estabilidad = 1`, `r_juntarEspacios = 0`,
`r_multidrop = 0`, `r_maxPresionItems = false`). Diferencias deliberadas:

1. **Sin distribución de masas.** La app no entrega masas; con masa = 0 la eliminación gaussiana
   del C++ produce NaN y en la práctica impide apilar a tres niveles. En C# la estabilidad es
   geométrica pura (misma regla de soporte parcial) y vale a cualquier altura.
2. **Todas las cajas de apoyo.** Con `r_juntarEspacios = 0` el C++ solo registraba la primera caja
   bajo un espacio; aquí se registran todas (como en los modos 1 y 2), así una caja que descansa
   sobre varias se evalúa correctamente.
3. **Piezas idénticas agrupadas** en un tipo con cantidad, como hace el motor al leer archivos
   (la DLL creaba un tipo por pieza). El resultado sigue siendo 1:1 con la lista de entrada.
4. No se portaron multidrop, presión/carga máxima, ni los modos de juntar/expandir espacios,
   ni los ciclos alternativos `Ciclo_LS` / `Ciclo_Constructivo` que `main` no usa.

### Resultados de referencia

Caso de `-testCApi` (205³, 6 × A 40×80×165 + 6 × B 80×80×120):

| Motor | 3 s | 15 s |
|---|---|---|
| C++ original (`Intento14.exe -testBeamWidth 15 5`) | 11/12, 84.13 %, ~550 iter/s | 11/12, 84.13 % |
| C# (`PruebasCubicaje.exe 15`) | 11/12, 84.13 %, ~14 000 iter/s | 11/12, 84.13 % |

Ambos convergen a la misma solución. La versión C# itera mucho más rápido que el C++ en x64
(no carga la maquinaria de masas ni tipos duplicados por pieza), lo que ayuda en el ARM64 del HoloLens.

## Pruebas

```
Pruebas\compilar_y_probar.cmd [segundos para el caso 205]
```

Busca `csc.exe` de Visual Studio / Build Tools, compila `Runtime/*.cs` + `Pruebas/PruebasCubicaje.cs`
y ejecuta: el caso 205³, determinismo con semilla, casos borde, 8 instancias aleatorias offline, la
versión online con y sin lookahead sobre 20 instancias aleatorias, y el bin packing offline (casos con
óptimo conocido: 8, 10 y 20 cubos de 50 y 27 cubos de 33 en 100³, más aleatorios) y online (con y sin
límite de contenedores abiertos). Un verificador independiente comprueba contención, no solapamiento,
soporte, orientaciones permitidas, uso único de cada pieza entre contenedores y que no se use menos
contenedores que la cota inferior.
Con el SDK de .NET: `dotnet run -c Release --project Pruebas\PruebasCubicaje.csproj`.
