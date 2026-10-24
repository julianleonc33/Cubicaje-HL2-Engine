# Cubicaje-HL2-Engine

Motor de cubicaje (GRASP + Beam Search, C++) usado por la aplicación CubicajeHL3 para HoloLens 2.

## Contenido

| Carpeta | Descripción |
|---|---|
| `Intento14/` | Motor original, sin modificar. Lee instancias desde `Instances/` y escribe resultados en `Solutions/`. |
| `Intento14 modificado/` | Mismo motor, adaptado para integrarse con Unity (ver abajo). |
| `CubicajeCSharp/` | Traducción del motor a C# (GRASP + Beam Search) como scripts de Unity, sin DLL, más una versión **online** del problema. Ver `CubicajeCSharp/README.md`. |
| `docs/` | Presentación del desarrollo: `cubicaje-motor-optimizacion.md` (texto, legible en GitHub) y `cubicaje-motor-optimizacion.html` (láminas, autocontenida: abrir en el navegador, ← → para avanzar, Ctrl+P imprime una lámina por página). Fuentes en `docs/presentacion-cubicaje/` (`construir_html.py` la regenera). |

### Cambios en `Intento14 modificado/`

- **Modo in-memory:** `SolveCubicajeInMemory(...)` recibe las piezas y el contenedor en memoria y reutiliza `Ciclo_BS` sin leer ni escribir archivos (parches en `ReadData2` / `WriteData`).
- **API C (`CubicajeApi.h`):** `Cubicaje_Resolver(...)` y `Cubicaje_UltimaUtilizacion()`, con structs blittables para llamar al motor vía P/Invoke desde Unity.
- **Proyecto `CubicajeNative.vcxproj`:** compila el motor como DLL UWP para ARM64 (HoloLens 2). `main` queda excluido con `CUBICAJE_DLL`.
- **Modos de prueba en `main`:** `-testInMemory`, `-testBeamWidth` y `-testCApi [maxTime]`.

Nota: el motor usa **Z como eje vertical** (`z1 == 0` es el piso del contenedor). Las unidades son enteras (por ejemplo, mm).

## Compilar

Abrir `Intento14.sln` en Visual Studio 2022 (toolset v143).
- `Intento14`: ejecutable de consola (x64).
- `CubicajeNative`: DLL Release | ARM64 (requiere las cargas de trabajo UWP y ARM64, Windows SDK 10.0.26100).

### DLL precompilada

`Intento14 modificado/Build/ARM64/Release/CubicajeNative.dll` está incluida en el repo, compilada desde este mismo código fuente (Release | ARM64, VS2022 v143).
- Arquitectura: ARM64, App Container (UWP).
- Exports: `Cubicaje_Resolver`, `Cubicaje_UltimaUtilizacion`.
- SHA-256: `E948CEC43EC9888EAFFC6DA7A1CF83D7ED63947DF90B7BD34526469CF8DB5877`
