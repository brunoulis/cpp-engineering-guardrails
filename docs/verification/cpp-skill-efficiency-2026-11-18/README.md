# Consumo y compatibilidad de la skill C++

Se incorporó al repositorio la revisión que superó los criterios fijados para la confirmación. En cuatro pares nuevos, utilizó **119.962 tokens menos (6,8 %)** que la guía anterior y mantuvo verdes las cuatro pruebas de aceptación y sus suites de regresión. Sus dos parches de iteradores conservaron un uso válido que fallaba con las dos entregas de la guía anterior. [Skill adoptada](../../../SKILL.md) · [Criterios de confirmación](confirmation.md) · [Datos por sesión](confirmation-attempts.tsv)

| Medida en las ocho sesiones de confirmación | Guía anterior | Revisión adoptada |
|---|---:|---:|
| Entregas con aceptación y regresión aprobadas | 4/4 | 4/4 |
| Entregas de iteradores que admiten el cursor copiable | 0/2 | 2/2 |
| Tokens de entrada y salida | 1.757.551 | 1.637.589 |
| Entrada sin caché | 225.000 | 215.788 |
| Tokens de salida | 19.335 | 21.865 |
| Comandos de shell registrados | 65 | 49 |
| Tiempo acumulado de las sesiones | 640 s | 650 s |

| Tarea | Pares | Tokens anteriores | Tokens con la revisión |
|---|---:|---:|---:|
| Conversión de buffer constante | 1 | 310.025 | 172.559 |
| Compatibilidad binaria con un cliente MSVC | 1 | 709.377 | 556.487 |
| Iteradores en `fmt::join` | 2 | 738.149 | 908.543 |

El ahorro se concentró en buffer y ABI. En iteradores el consumo subió un 23,1 % y mejoró la compatibilidad medida. La entrada sin caché bajó un 4,1 % en el agregado, la salida subió un 13,1 % y el tiempo acumulado aumentó diez segundos. Cada grupo recibió la skill en las cuatro sesiones; las trazas contienen su texto. El total de tokens incluye entrada en caché. Estos valores miden uso, no facturación.

## Cambio y prueba de compatibilidad

La guía concentra la lectura en el símbolo, su consumidor y la prueba afectada. Pide guardar la compilación detallada en un archivo y terminar cuando el parche final supera los controles relevantes, salvo que haya un cambio de entrada, una obligación del proyecto o un riesgo pendiente. Para plantillas, pide conservar los requisitos mínimos que cumplían los tipos admitidos por la versión anterior.

El nuevo consumidor invoca `fmt::join` con dos iteradores lvalue que admiten copia y declaran eliminado el movimiento. La revisión padre compila el consumidor y permite formatear dos veces la vista. La corrección histórica y los cuatro parches del primer piloto fallan con MSVC C2280. En una copia local de la corrección histórica, transferir el inicio mediante `std::move_if_noexcept` en la construcción de la vista y en el retorno del overload permitió pasar las dos pruebas, con iteradores movibles y con iteradores exclusivamente copiables. [Consumidor y reproducción](../../../tests/agent-pilot/followup/README.md) · [Comprobación sobre los parches anteriores](archived-copy-only.tsv)

## Primera candidata

La primera ronda tuvo seis pares programados. Las seis entregas de la guía anterior y las cinco entregas completas de la candidata pasaron el evaluador original. En cinco pares completos, el consumo bajó de 1.964.634 a 1.642.888 tokens. La sexta sesión candidata agotó los 300 segundos y carece de telemetría final. Por ello no se puede calcular el ahorro de esa candidata para la ronda completa. Se conservó su parche parcial y se descartó esa versión. La confirmación utilizó ocho sesiones nuevas con el mismo modelo y límite. [Protocolo inicial](protocol.md) · [Las doce sesiones](attempts.tsv)

## Alcance y reproducción

El estudio cubre tres regresiones públicas de una biblioteca, MSVC en Windows y `gpt-6.1-sol` con esfuerzo `high`. Se seleccionaron los casos deliberadamente. La nueva prueba de iteradores procede de revisar el primer piloto y sirvió para diseñar la guía. Demuestra una mejora en ese contrato conocido. Cuatro pares dejan incertidumbre sobre el ahorro en otros trabajos.

El archivo incluye las guías, el evaluador, los prompts, los 20 parches o entregas parciales y los resultados de ambas rondas. Para reconstruir una revisión, se parte de `previous-guidance` y se sustituye su `SKILL.md` por el de la candidata correspondiente. El archivo contiene las órdenes para recalificar un parche con un clon local de fmt. [Paquete reproducible](replay.tar.gz)

Al extraer el paquete se verificaron los 20 hashes de parche. Se recompiló una entrega de la revisión adoptada desde el archivo: pasaron la aceptación, la regresión y el consumidor adicional. Durante la campaña, la suite raíz del árbol de trabajo registró 26 pruebas superadas y una omitida (`lint-dependency-cache`). `skills-ref validate` aceptó la skill. El validador auxiliar `quick_validate.py` rechaza el campo de metadatos preexistente `compatibility` en ambas revisiones; la cabecera se conservó íntegra respecto a la guía congelada del piloto.
