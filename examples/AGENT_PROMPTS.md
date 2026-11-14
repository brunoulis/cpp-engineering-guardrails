# Ready-to-use prompts (Spanish)

## Auditoría completa, sin modificar archivos

> Usa la skill `cpp-engineering-guardrails` en modo AUDIT. Antes de actuar, detecta versión de C++, toolchain, build system, tests, APIs públicas y rutas críticas. Prioriza P0–P3 con evidencias y confianza. No modifiques el código. Separa el resultado real de compiladores/sanitizers de sospechas heurísticas y crea un plan de corrección.

## Implementación de una función

> Activa IMPLEMENT de `cpp-engineering-guardrails`. Comprende las interfaces y sus consumidores, define ownership y error handling, implementa el cambio mínimo y añade pruebas. Ejecuta los gates relevantes, realiza una revisión independiente y enumera cualquier verificación no ejecutada.

## Refactor seguro

> Activa REFACTOR de `cpp-engineering-guardrails`. No dividas clases por su número de líneas ni impongas patrones. Analiza responsabilidades/dependencias, captura comportamiento con tests, compara opciones y ejecuta cambios incrementales sin romper API/ABI. Verifica cada paso.

## Refactor de arquitectura

> Activa DEEP REFACTOR de `cpp-engineering-guardrails`. Entrega antes un mapa de dependencias, contratos públicos, riesgos de concurrencia/memoria, plan de migración reversible y estrategia de pruebas. No cambies interfaces públicas sin autorización y pruebas de consumidores.

## Núcleo C++ con Rust y Flutter (low-latency)

> Usa `cpp-engineering-guardrails` con perfil `low-latency` para investigar el motor C++ de telemetría. Prioriza contratos C ABI, lifetime de buffers, seguridad de callbacks, cierres ordenados, allocations y latencia p95/p99. No afirmes mejoras sin comparar una baseline representativa de compilación release y múltiples mediciones. No introduzcas SIMD ni un sistema lock-free sin demostrar beneficio y corrección.
