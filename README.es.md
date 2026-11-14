# C++ Engineering Guardrails v3

> English: [README.md](README.md)

**Skill instalable para Codex, Claude Code y Hermes**. Orienta a agentes para **generar, auditar, refactorizar y migrar C++** con seguridad, simplicidad, pruebas reales y decisiones justificadas. Integra aprendizajes de `rust-engineering-guardrails` y `flutter-engineering-guardrails`, adaptados a las particularidades de C++.

**Requisito cumplido:** las herramientas propias (`cpp-rails-audit`, `cpp-rails-check` y sus pruebas) están desarrolladas **exclusivamente en C++17**. No se distribuyen herramientas Rust/Python, Cargo, `pip` ni scripts de auditoría en otro lenguaje. CMake/CTest son infraestructura de compilación/pruebas del ecosistema C++ y Clang/GCC/MSVC, sanitizers y `abidiff` son herramientas externas opcionales.

## Qué contiene

- `SKILL.md`: entrada ligera, 14 guardrails R0–R13, enrutamiento progresivo, evidencias y límites del agente.
- `modes/`: `AUDIT`, `IMPLEMENT`, `REFACTOR`, `DEEP-REFACTOR` (cambios progresivos, verificables, reversibles).
- `references/`: memoria/lifetime/UB, dominio y tipos, estado/recursos, cancelación/backpressure, concurrencia, templates/concepts, seguridad, compatibilidad ABI/FFI, determinismo, tests, arquitectura adaptable, datos/layout, optimización y revisores.
- `profiles/`: general, low-latency, library-sdk, legacy-code, engine, service, native-ui, ffi y prototype; la selección es contextual, no obliga a optimizar todo el código.
- `policies/`: severidades P0–P3, gates según el cambio y excepciones con caducidad.
- `tools/cpp-rails/`: biblioteca y ejecutables C++17 con escaneo heurístico, baseline, triage, include-topology, informes JSON/Markdown, ejecución de CMake/CTest, `clang-tidy`, sanitizers, matrices de compiladores, comprobación de instanciaciones y benchmarks CSV/TSV.
- `tests/`: fixtures C++20 positivos/negativos y CTest, sin runners Rust/Python/Bash.
- `templates/` y `examples/`: prompts, perfil revisable, planes de refactor, ADR, reporte, protocolo de revisión y contrato FFI.

## Compilar las herramientas (C++17)

Requisitos: compilador C++17 y CMake 3.16+. Las pruebas de conceptos usan un compilador con C++20. Comandos desde la carpeta descomprimida:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Los binarios aparecen en `build/tools/cpp-rails/` (en generadores multi-config, el subdirectorio puede ser `Release/`). Los binarios no se incluyen precompilados para no asociarlos a un sistema operativo/arquitectura.

## Instalar la skill en los agentes

Conserva **toda la carpeta** y sitúala en el directorio de skills compatible con tu versión:

- Codex: `PROYECTO/.agents/skills/cpp-engineering-guardrails/`
- Claude Code: `PROYECTO/.claude/skills/cpp-engineering-guardrails/`
- Hermes Agent: `~/.hermes/skills/cpp-engineering-guardrails/` o directorio externo de skills configurado.

Para trabajar entre equipos, utiliza una sola copia versionada y enlaces simbólicos donde el agente los soporte. Compila el CLI en cada máquina (GCC/Clang/MSVC según plataforma). Los modos funcionan como instrucciones aunque el CLI no esté instalado.

## Uso en un proyecto C++

```bash
SKILL=/ruta/a/cpp-engineering-guardrails
BIN="$SKILL/build/tools/cpp-rails"

"$BIN/cpp-rails-audit" audit --root . --profile general
"$BIN/cpp-rails-audit" triage --root . --format json --output review.json
"$BIN/cpp-rails-audit" topology --root .
"$BIN/cpp-rails-audit" baseline --root . --output .cpp-rails/baseline.txt
"$BIN/cpp-rails-audit" audit --root . --baseline .cpp-rails/baseline.txt --changed
"$BIN/cpp-rails-audit" templates --check --root . --source tests/concrete_instances.cpp --compiler clang++
"$BIN/cpp-rails-audit" perf compare --root . --results benchmarks.csv

# Solo si ya existe un CMake build configurado:
"$BIN/cpp-rails-check" --root . --build-dir build
# Opt-in para configurar si falta el build:
"$BIN/cpp-rails-check" --root . --build-dir build --configure
# Pruebas adicionales: cada sanitario en su propio build
"$BIN/cpp-rails-check" --root . --build-dir build --sanitizer asan-ubsan --compiler clang++
"$BIN/cpp-rails-check" --root . --build-dir build --sanitizer tsan --compiler clang++
```

**Importante:** `audit` nunca corrige código; `baseline` escribe solo el inventario solicitado. `check` puede ejecutar compilación, tests y configuración en el proyecto objetivo. Una herramienta que falta o un gate exigido que no se ejecuta se registra como **UNVERIFIED** (código de salida `2`), nunca PASS. Un gate fallido devuelve `1`. El escáner es heurístico y no demuestra UB, memory safety ni exactitud de ABI. `--strict` exige también que no haya gates opcionales UNVERIFIED.

## Las cuatro modalidades y cuándo usarlas

| Modo | Uso | Qué exige |
|---|---|---|
| Audit | Investigar deuda, errores, calidad y seguridad | Solo lectura, P0–P3, evidencia y plan |
| Implement | Función o corrección | Contratos, menor complejidad útil y tests |
| Refactor | Mejorar código existente | Opciones + dejar como está, pruebas de caracterización, regresión y rollback |
| Deep-refactor | Cambiar arquitectura o fronteras | ADR, mapa de consumidores, migración por etapas, compatibilidad y reviewer |

Ejemplos en `examples/AGENT_PROMPTS.md`.

## Aprendizajes integrados de Rust y Flutter

| Lección transferida | Aplicación correcta en C++ |
|---|---|
| Ownership, newtypes, typestate, generational IDs | RAII, contratos de lifetime, tipos validados y handles seguros cuando conviene |
| Sans-IO, cancelación, backpressure y bounded queues | Diseño de workers, cierre seguro, colas acotadas y pruebas de saturación |
| SoA/AoS, allocations y determinismo | Perfilado de hot paths, precisión numérica y tests de replay/paridad |
| State owner, lifecycle, stale async results | Fuente única de verdad, cancelación de callbacks y destrucción limpia |
| Arquitectura adaptable y evitar capas innecesarias | Análisis previo a separar clases, módulos, targets, PImpl o plugins |
| Refactor inteligente/deep refactor | Corrección mínima, alternativa estructural, leave-as-is, checkpoints, reversibilidad |
| Reviewer escéptico + CI y fixtures | Pruebas positivas/negativas, no false green, findings con evidencia |
| Contexto progresivo/memoria jerárquica | Referencias por demanda y notas verificadas por proyecto/scope |

No se trasplantan conceptos específicos de Flutter (widgets, Provider) a librerías C++ que no tienen UI, ni se presupone que C++ ofrece las garantías del borrow checker de Rust.

## Limitaciones explícitas

El CLI no reimplementa Clang AST, no hace análisis semántico completo, no automatiza investigación online de bugs de GCC/LLVM y no promete compatibilidad con todos los builds no-CMake. `std::regex` y el escáner léxico localizan **indicios** para que un agente los revise; riesgos de lifetime, races y template semantics necesitan compiladores, tests y juicio técnico. La verificación de benchmarks mediante mediana/MAD es una heurística para priorizar, no inferencia estadística definitiva.

El paquete mantiene el código fuente y tests, no un ejecutable universal. Licencia MIT (`LICENSE`).
