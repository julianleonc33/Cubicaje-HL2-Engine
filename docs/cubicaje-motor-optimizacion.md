# Cubicaje HoloLens 2 · Motor de optimización

**Cuatro versiones del problema de carga, un solo motor en C#**

El GRASP + Beam Search del motor C++ traducido a scripts de Unity, extendido a la variante **online** (las cajas llegan una a una) y al **bin packing** (varios contenedores idénticos), con espacios maximales compartidos entre contenedores.

Edison Julian León-Carreño · Pablo Medina · David Álvarez-Martínez
Equipo de desarrollo Cubicaje HL2 · github.com/julianleonc33/Cubicaje-HL2-Engine · rama `csharp-port`, pull requests #1 y #2 · 24 de octubre de 2026

Versión en láminas: [`cubicaje-motor-optimizacion.html`](cubicaje-motor-optimizacion.html) (abrir en el navegador; ← → avanza, N notas, F pantalla completa, Ctrl+P imprime una lámina por página).

---

## 01 · El problema

### De una DLL nativa a scripts de Unity, y de un problema a cuatro

**Punto de partida.** El motor C++ Intento14 resuelve la carga de **un** contenedor con todas las cajas conocidas (GRASP + Beam Search). La app de HoloLens lo llamaba por P/Invoke a una DLL ARM64, no reentrante y sin posibilidad de intervenir el algoritmo desde Unity.

**Lo que hicimos.** Traducimos el motor a C# puro (sin DLL), conservando el algoritmo y sus parámetros, y lo extendimos a las variantes **online** (las cajas llegan una a una) y de **bin packing** (varios contenedores idénticos, minimizar cuántos se usan).

**Lo que no se portó.** Multidrop (varios clientes), carga máxima por presión, distribución de masas y los modos de juntar espacios: la app no entrega masas ni clientes y la DLL tampoco los usaba.

| Cifra | Significado |
|---|---|
| 2.997 | líneas de C# en 6 archivos de `Runtime/` (el motor C++ tenía 6.985) |
| 4 | versiones: un contenedor y bin packing, cada una offline y online |
| ≈ 25× | más iteraciones por segundo que el C++ en x64 (14.000 frente a 550–900) |
| 84,13 % | utilización en el caso de prueba de la DLL (11 de 12 cajas), igual en C++ y en C# |

Repositorio julianleonc33/Cubicaje-HL2-Engine · carpeta `CubicajeCSharp/` · pruebas en `Pruebas/compilar_y_probar.cmd`.

> El C++ sigue en el repo y se puede compilar con MSVC; se usó para comparar. El caso 205³ es el mismo de la prueba `-testCApi` de la DLL. Las 2.997 líneas incluyen el online y el bin packing; las pruebas son 375 líneas aparte.

### Las cuatro versiones: dos objetivos × dos momentos de decisión

| | Un contenedor · 3D-SKP | Varios contenedores · 3D-BPP |
|---|---|---|
| **Offline** (todas las cajas conocidas antes de empezar) | `CubicajeSolver.Resolver` — **Maximizar el volumen cargado.** Single Knapsack 3D: cabe lo que cabe, el resto queda afuera. GRASP + Beam Search con límite de tiempo. Es el problema del motor C++ y reemplaza a `Cubicaje_Resolver`. | `BinPackingSolver.Resolver` — **Minimizar el número de contenedores.** Bin Packing 3D: todas las cajas se empacan. El mismo GRASP + Beam Search sobre una lista de espacios maximales que abarca todos los contenedores abiertos. |
| **Online** (cada caja se ubica al llegar y no se mueve) | `CubicajeOnline.Colocar` — **Ubicar cada caja sin conocer las siguientes.** Decisión voraz por caja: candidatos (espacio × orientación) ordenados por estrategia, con lookahead opcional sobre las próximas cajas conocidas. Rechaza la caja si no cabe. | `BinPackingOnline.Colocar` — **Repartir cada caja entre contenedores abiertos.** La caja va al mejor espacio de la lista (contenedor más lleno o más antiguo donde cabe); si no hay espacio, se abre un contenedor. Nunca rechaza una caja que cabe en uno vacío. |

Las cuatro comparten geometría, estabilidad y representación: un solo núcleo (`CubicajeNucleo.cs`) y un solo motor online (`MotorOnline`).

---

## 02 · Supuestos y mecanismo base

### Lo que las cuatro versiones dan por hecho

| Supuesto | Detalle | Aplica a |
|---|---|---|
| Geometría | Cajas y contenedores ortogonales; unidades enteras (mm). Ejes del motor: X ancho, Y fondo, Z alto; Unity usa Y vertical y `CubicajeEjes` convierte. | Las cuatro |
| Rotaciones | Las 6 orientaciones por defecto (RotX/RotY/RotZ = 3). «Este lado arriba» = solo giros sobre el eje vertical. | Las cuatro |
| Estabilidad | Soporte parcial geométrico: el centro de la base de cada caja elevada queda dentro del casco convexo de sus áreas de contacto con las cajas de abajo. En el online se puede exigir soporte completo. | Las cuatro |
| Sin masas ni presión | No hay pesos, límite de carga ni distribución de masas. La app no los entrega y con masa = 0 la lógica del C++ degeneraba. | Las cuatro |
| Contenedores | Un solo tipo de contenedor; en bin packing todos son idénticos y, en principio, ilimitados. | BPP |
| Información | Offline: todas las cajas conocidas, cantidades agrupadas por dimensiones. Online: se conoce la caja actual y, si acaso, un buffer de las próximas; nada se reubica. | Según versión |
| Un solo destino | Sin multidrop: no hay orden de descarga por cliente ni restricciones de visibilidad. | Las cuatro |

Frente al C++: estabilidad geométrica a cualquier altura, todas las cajas de apoyo registradas y piezas idénticas agrupadas por tipo.

> Lo más importante para la operación: no hay pesos. Si en el futuro se quiere restringir carga sobre cajas frágiles, hay que agregar masa y presión máxima por tipo; el C++ tenía esa lógica pero no funcionaba sin masas.

### Espacios maximales y bloques: la representación común a las cuatro

Al colocar un bloque, cada espacio que lo intersecta se parte en hasta seis sub-espacios (−X, +X, −Y, +Y, abajo, arriba). Con soporte parcial el espacio superior conserva la huella del espacio original; en la lista «rápida» se limita a la huella del bloque.

- **Espacio maximal.** Paralelepípedo libre máximo. Tras cada colocación se eliminan los espacios contenidos en otros, los que no admiten ninguna caja pendiente y los elevados sin apoyo. Cada uno recuerda qué cajas lo soportan.
- **Bloque.** Una caja en una orientación, o la unión de dos bloques lado a lado o apilados que llena al menos el 99 % de su envolvente. Hasta 10 bloques por pieza (`MaxNBloques`).
- **Esquina y soporte.** El bloque va a una de las 4 esquinas inferiores del espacio, elegida por el centroide de las áreas de apoyo. Toda caja elevada tiene el centro de su base dentro del casco convexo de sus contactos.
- **Mejor ajuste (best-fit).** Los tres sobrantes del espacio menos el bloque, ordenados de menor a mayor (bf1 ≤ bf2 ≤ bf3): el criterio para ordenar bloques en todas las versiones.

> Mecanismo base, idéntico al C++. En bin packing cada espacio y cada caja llevan además el índice de su contenedor; partir, contener y soportar solo relacionan elementos del mismo contenedor.

---

## 03 · Las cuatro versiones

### Versión 1 · Offline · Un contenedor — `CubicajeSolver`

**GRASP + Beam Search: construir, desarmar y completar**

1. **Constructivo.** Elige un espacio (fondo-altura o altura-fondo) y un bloque de una lista restringida por α. Los α con mejor utilización se escogen más.
2. **¿Vale la pena?** Pasa a búsqueda local si mejora la incumbente o si su métrica volumen / área de piso compite con ella (umbral que baja con el tiempo).
3. **Desarmar.** Se conservan los primeros bloques hasta el punto de corte y se reconstruyen espacios, cantidades y bloques disponibles (`RemoverPiezas`).
4. **Beam Search.** Cada nodo expande 3 espacios × 5 bloques; cada hijo se completa con un constructivo rápido determinístico y sobreviven los 5 mejores por nivel.

| Parámetro | Valor | Significado |
|---|---|---|
| `BeamWidth` | 5 | nodos que sobreviven por nivel del haz |
| `MaxNBloques` | 10 | bloques generales por pieza |
| `UtilizacionMaximaBloque` | 0,99 | llenado mínimo para fusionar bloques |
| α | 0,1 … 1,0 | lista restringida; muestreo reactivo |
| `maxTiempoSeg` | libre | presupuesto (3 s en las pruebas) |

**Criterio de parada**
- Se agota `maxTiempoSeg` (revisado tras cada iteración y nivel del haz).
- La incumbente tiene **todas** las cajas.
- Cancelación desde Unity: devuelve la mejor solución hasta ese momento.

> Es el algoritmo del C++ sin cambios de estructura. Fase 1: una iteración por cada alfa. Fase 2: alfa reactivo hasta agotar el tiempo. Siempre se completa al menos una iteración, aun con tiempo 0. El resultado es la incumbente: cajas colocadas en orden de empaque, no colocadas y utilización.

### Versión 2 · Offline · Varios contenedores — `BinPackingSolver`

**Una sola lista de espacios para todos los contenedores**

```
contenedor 0 (lleno · 1 espacio)   contenedor 1 (en carga · 2 espacios)   → contenedor 2 (se abre · entra a la lista)
```

**Regla de apertura.** La lista reúne los espacios de todos los contenedores abiertos. Cuando ninguna caja pendiente tiene espacio en la lista, se abre un contenedor nuevo. En un contenedor vacío siempre cabe algún bloque, así que el constructivo termina con todas las cajas empacadas.

**Puntaje a maximizar.** `puntaje = −N + ( Σ utilización² ) / N`. El segundo término está en (0, 1]: primero manda el número de contenedores N; a igualdad gana la solución más concentrada, la que más fácil permite eliminar uno.

- **Qué cambia en el núcleo.** Espacios, bloques ubicados y cajas llevan el índice de contenedor. Partir, contener y calcular apoyos solo relacionan elementos del mismo contenedor. Se prioriza el contenedor de menor índice: se llenan en orden.
- **Qué no cambia.** El mismo GRASP + Beam Search de la versión 1. Al desarmar una solución se reconstruyen los espacios de cada contenedor y se descartan los vacíos. El beam también abre contenedores cuando un nodo se queda sin espacios.

**Criterio de parada**
- N alcanza la cota inferior por volumen ⌈ Σ volumen cajas / volumen contenedor ⌉: óptimo demostrado.
- Se agota `maxTiempoSeg` o llega la cancelación: se devuelve la mejor solución (menos contenedores, luego más concentrada).

> La cota inferior por volumen es débil cuando las cajas son grandes respecto al contenedor; en esos casos el solver corre hasta agotar el tiempo aunque ya tenga el óptimo. Se puede reforzar con cotas de Martello y Vigo si hace falta.

### Versión 3 · Online · Un contenedor — `CubicajeOnline`

**Una decisión por caja: candidatos, orden y lookahead**

1. **Candidatos.** Para cada espacio maximal y orientación permitida que cabe: la caja va a la esquina del espacio y, si está elevada, debe pasar el soporte.
2. **Orden.** Los candidatos se ordenan con la estrategia elegida (tabla). Sin lookahead se toma el primero: la decisión es inmediata.
3. **Lookahead (opcional).** Con un buffer de próximas cajas: se prueban los K mejores candidatos completando el buffer con la misma regla; gana el que carga más volumen.
4. **Colocar o rechazar.** Se ubica la caja y se actualizan los espacios. Si no hay candidatos se rechaza, el estado no cambia y las siguientes se siguen intentando.

| Estrategia | Orden de los candidatos | Cuándo conviene |
|---|---|---|
| `MenorAlturaMejorAjuste` | más bajo · mejor ajuste · más al fondo | defecto; pallets abiertos, capas estables |
| `FondoAlturaMejorAjuste` | más al fondo · más alto · mejor ajuste | contenedores que se llenan desde el fondo |
| `MejorAjuste` | menor sobrante · más bajo · más al fondo | cajas muy heterogéneas, aprovechar huecos |

| Parámetro | Valor por defecto |
|---|---|
| `CandidatosEvaluados` (K) | 5 |
| `SoporteCompleto` | false (soporte parcial) |
| Buffer de próximas cajas | ninguno; en pruebas 3–4 |

Caso 205³, 12 cajas en orden aleatorio: 10–11 de 12 sin lookahead según la estrategia; 11 de 12 con lookahead 3 (el offline llega a 11 de 12).

> El online no tiene ciclo de búsqueda: por cada caja hace un cálculo acotado y devuelve la posición. Lo que sí hay que definir es cuándo se da por terminado el contenedor; ver criterios de parada.

### Versión 4 · Online · Varios contenedores — `BinPackingOnline`

**La caja busca espacio en todos los contenedores abiertos**

1. **Candidatos.** Igual que la versión 3, pero sobre la lista de espacios de **todos** los contenedores abiertos. Cada candidato sabe a qué contenedor pertenece.
2. **Orden.** Primero el contenedor: el más lleno donde cabe (`MejorAjuste`) o el más antiguo (`PrimerAjuste`); dentro de él, la posición como en la versión 3.
3. **Sin candidatos: abrir.** Se abre un contenedor y su espacio completo entra a la lista. Con límite de abiertos, antes se cierra el más lleno: sus espacios salen de la lista.
4. **Lookahead y colocación.** El rollout también abre contenedores; gana el candidato con mejor −N + Σu²/N (menos contenedores, más llenos). Solo se rechaza lo que no cabe en uno vacío.

| Parámetro | Defecto | Efecto |
|---|---|---|
| `Estrategia` | `MejorAjuste` | completa contenedores y los cierra antes |
| `MaxContenedoresAbiertos` | 0 = sin límite | espacio físico (p. ej. 2 pallets en el muelle) |
| `Colocacion` | versión 3 | estrategia de posición, K, tipo de soporte |
| `Cerrar(k)` | manual | el pallet salió: no recibe más cajas |

**Resultado de referencia.** 10 instancias aleatorias de 20 a 80 cajas: 24 contenedores (mejor ajuste con lookahead 3, y también primer ajuste) frente a una cota por volumen de 16. El offline, con 2 s por instancia, queda a 1 contenedor de la cota en promedio.

> Diferencia clave con la versión 3: aquí no se rechaza (se abre contenedor). Por eso el concepto de parada cambia: no es cuándo se llena el contenedor, sino cuándo se cierra uno para despacharlo.

---

## 04 · Criterios de parada, flujo de trabajo y validación

### Offline: parada por tiempo u óptimo. Online: fin por eventos

| Versión | Qué detiene el cómputo | Qué termina el episodio / cierra el contenedor | Costo por decisión |
|---|---|---|---|
| 1 · Offline SKP | `maxTiempoSeg` · todas las cajas colocadas · cancelación | Una sola corrida: la solución es el plan completo del contenedor. | 14.000 iteraciones/s en x64 con 12 cajas |
| 2 · Offline BPP | `maxTiempoSeg` · N = cota inferior ⌈ΣV / Vc⌉ · cancelación | Una sola corrida: plan de todos los contenedores. | igual; crece con el número de cajas |
| 3 · Online SKP | Sin ciclo: por caja, un cálculo acotado (espacios × 6 orientaciones, más K × buffer con lookahead). | Se acaba la cola, o el operador cierra el contenedor según la política de abajo. Un rechazo no cierra. | menos de 1 ms por caja en x64 (20 instancias: 0,67 s en total) |
| 4 · Online BPP | Igual que la 3; nunca rechaza una caja que cabe en un contenedor vacío. | Se acaba la cola. Un contenedor se cierra al exceder `MaxContenedoresAbiertos` (el más lleno) o con `Cerrar(k)` al despachar. | igual que la 3, sobre la lista de todos los abiertos |

En las versiones offline el criterio de parada es del **algoritmo**: cuándo dejar de buscar. En las online no hay búsqueda que detener: la pregunta pasa a ser cuándo dar por **cerrado el contenedor**.

### Online: la pregunta no es cuándo parar, sino cuándo cerrar

**Propuesta: política de cierre del contenedor (a calibrar)**
- **Sin espacio útil:** ningún espacio libre (`EspaciosLibres`) admite la caja más pequeña esperada, d_min. Criterio físico: el contenedor está lleno para lo que puede llegar.
- **Rechazos consecutivos:** R cajas seguidas rechazadas (propuesta R = 2) cierran el contenedor y abren el siguiente.
- **Umbral de utilización:** utilización ≥ U (propuesta 85 %) y cola restante pequeña: se despacha sin esperar.
- **Orden del operador:** cierre manual desde HoloLens (pallet completo, cambio de destino).

**Qué está implementado**
- Rechazo por caja, consulta `Cabe()` sin modificar el estado y `EspaciosLibres()` para evaluar d_min desde la app.
- Cierre por límite de abiertos y `Cerrar(k)` manual en la versión 4.
- Los criterios R y U son reglas de la app sobre esas consultas: no tocan el motor.

Las cuatro reglas se combinan con «o»: la primera que se cumpla cierra el contenedor. Los parámetros d_min, R y U se fijan en piso con las primeras corridas reales.

### Dos modos de uso en HoloLens: planificar antes, o guiar la carga en vivo

**Modo plan · offline (versiones 1 y 2)** — la lista de cajas se conoce antes de empezar

1. Registrar las cajas (lista o escaneo) y el contenedor en mm.
2. Resolver en segundo plano (3–5 s) sin congelar la vista.
3. Guía paso a paso en el orden de empaque: holograma de la caja en su posición.
4. El operario confirma cada caja; las no colocadas se listan para otro contenedor.
5. Reporte: utilización, piezas por contenedor (bin packing), tiempo.

**Modo guía · online (versiones 3 y 4)** — las cajas llegan por la banda o el camión, una a una

1. Llega la caja: se leen sus dimensiones (escaneo o etiqueta) y, si existe, el buffer de las próximas.
2. `Colocar(caja, próximas)`: decisión en milisegundos; en bin packing dice además en qué contenedor.
3. Holograma del hueco objetivo (`CentroLocalEnUnity`) y de los espacios libres.
4. El operario confirma; una caja rechazada va a espera o al siguiente contenedor.
5. Política de cierre (d_min, R, U u orden manual): despacho y `Reiniciar` o `Cerrar(k)`.

Ambos modos corren en el dispositivo, sin servidor: el motor es C# puro dentro de la app.

> El modo plan reemplaza exactamente lo que hacía la DLL, con la ventaja de poder cancelar y de no bloquear el hilo principal (`CubicajeSolverBehaviour` corre el solver en otro hilo). El modo guía es nuevo. Punto abierto: si el operario pone la caja en un sitio distinto al sugerido, hoy hay que reiniciar; falta una API para fijar una caja colocada a mano.

### Validación: paridad con el C++ original y soluciones verificadas aparte

| Prueba | Resultado | Lectura |
|---|---|---|
| Caso 205³ de la DLL (12 cajas: 6 de 40×80×165 y 6 de 80×80×120) | C++ (MSVC) y C#: 11/12 cajas, 84,13 % a 3 s y a 15 s | paridad; el C# itera ≈ 25× más rápido |
| Determinismo | misma semilla → misma solución | reproducible para depurar |
| Offline SKP aleatorio (8 instancias, 0,5 s) | 5 a 30 cajas, todas las soluciones válidas | si todo cabe, termina en 1 iteración |
| Online SKP aleatorio (20 instancias) | utilización media 52 % sin lookahead, 54 % con 4 | el lookahead ayuda poco |
| Bin packing offline, óptimos conocidos | 8, 10, 20 cubos de 50 y 27 de 33 en 100³ → 1, 2, 3 y 1 | alcanza la cota en los cuatro |
| Bin packing offline aleatorio (6 instancias, 2 s) | 10 contenedores en total, cota inferior 8 | a 1 de la cota en 2 de 6 casos |
| Bin packing online (10 instancias) | 24 contenedores, cota 16; límite de abiertos respetado | el precio de no ver el futuro |

- **Comprobador independiente.** Fuerza bruta aparte del motor: contención, no solapamiento, soporte parcial de toda caja elevada, orientaciones permitidas, uso único de cada pieza y contenedores ≥ cota inferior.
- **Compilación.** C# 7.3 (Unity 2019+), sin paquetes. `Runtime/` compila también con `UNITY_5_3_OR_NEWER` contra un stub de UnityEngine. Pruebas: `Pruebas/compilar_y_probar.cmd` o el `.csproj` con dotnet.

> El dato clave es la primera fila: el C++ compilado en esta máquina da exactamente el mismo 11 de 12 y 84,13 %. El 12 de 12 que mencionaba el README original no lo alcanza ninguno de los dos motores.

---

## 05 · Limitaciones, siguientes pasos y API

### Qué falta por decidir, medir y construir

**Limitaciones conocidas**
- **Sin pesos ni carga máxima.** La estabilidad es geométrica. Cajas frágiles o pesadas arriba no se penalizan.
- **Cota inferior débil en bin packing.** Solo por volumen: con cajas grandes el solver agota el tiempo aunque ya tenga el óptimo.
- **Online sin re-sincronización.** Si el operario pone una caja fuera de la posición sugerida, hoy hay que reiniciar el contenedor.
- **Rendimiento en ARM64 sin medir.** Las cifras son de PC x64. El beam copia soluciones completas; en el HoloLens habrá menos iteraciones por segundo.

**Siguientes pasos propuestos**
1. Fusionar los pull requests (rama `csharp-port`) e integrar `Runtime/` en el proyecto de Unity en lugar del P/Invoke.
2. Medir en el HoloLens 2: iteraciones por segundo y tiempo por caja; fijar `maxTiempoSeg` y K para el dispositivo.
3. Calibrar en piso la política de cierre online: d_min, R rechazos consecutivos y umbral U.
4. Agregar al online una operación «fijar caja colocada a mano» para re-sincronizar sin reiniciar.
5. Reforzar la cota inferior del bin packing (cotas de Martello y Vigo) para terminar antes.
6. Si la operación lo exige: masa y carga máxima por tipo de caja, y orden de descarga por destino (multidrop).

### Anexo · API para Unity

Seis archivos en `Runtime/`, sin dependencias: se copian a `Assets/` y se usan.

| Versión | Llamada | Devuelve | Archivo |
|---|---|---|---|
| 1 · Offline SKP | `CubicajeSolver.Resolver(cx, cy, cz, piezas, maxTiempoSeg, ParametrosSolver, token)` | `ResultadoCubicaje`: `Colocadas` en orden de empaque, `NoColocadas`, `Utilizacion`, `Iteraciones` | `CubicajeSolver.cs` |
| 2 · Offline BPP | `BinPackingSolver.Resolver(cx, cy, cz, piezas, maxTiempoSeg, ParametrosSolver, token)` | `ResultadoBinPacking`: un `ResultadoCubicaje` por contenedor, `CotaInferior`, `NoColocadas` | `CubicajeBinPacking.cs` |
| 3 · Online SKP | `new CubicajeOnline(cx, cy, cz, ParametrosOnline)`; `Colocar(pieza[, proximas])`; `Cabe(pieza)`; `EspaciosLibres()`; `Reiniciar()` | `PiezaColocada?` (null = no cabe), `Colocadas`, `Utilizacion`, `PiezasRechazadas` | `CubicajeOnline.cs` |
| 4 · Online BPP | `new BinPackingOnline(cx, cy, cz, ParametrosBinPackingOnline)`; `Colocar(pieza[, proximas])`; `Cerrar(k)`; `EspaciosLibres(k)` | `PiezaColocada?` con `Contenedor`; `NumContenedores`, `NumAbiertos`, `ColocadasDe(k)`, `UtilizacionDe(k)` | `CubicajeBinPacking.cs` |
| Unity | `CubicajeEjes.PiezaDesdeUnity`, `CentroLocalEnUnity`, `TamanoEnUnity`; `CubicajeSolverBehaviour.Resolver(piezas)`, `AlTerminar`, `Cancelar()` | conversión Y vertical de Unity ↔ Z vertical del motor; solver en un hilo aparte con aviso en el hilo principal | `CubicajeUnity.cs` |
| Común | `PiezaEntrada(id, dimX, dimY, dimZ[, mantenerVertical])`; `PiezaColocada`; `ParametrosSolver`; núcleo geométrico | tipos de entrada y salida; espacios maximales, bloques, estabilidad | `CubicajeTipos.cs`, `CubicajeNucleo.cs`, `CubicajeGeometria.cs` |

Todas las llamadas son reentrantes (sin estado global, a diferencia de la DLL) y admiten un token de cancelación que devuelve la mejor solución hasta ese momento. Documentación completa en `CubicajeCSharp/README.md`.
