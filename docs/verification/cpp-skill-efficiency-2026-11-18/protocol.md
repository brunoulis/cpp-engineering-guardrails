# Comparación de dos revisiones de la skill C++

Se comparan la guía congelada del piloto y una candidata que cambia únicamente el cuerpo de `SKILL.md`. El agente recibe una skill en ambos grupos. Se mantienen el modelo `gpt-6.1-sol`, esfuerzo `high`, límite de 300 segundos, auditor, compilador, prompts y criterios del piloto original. El [paquete de reproducción](replay.tar.gz) incluye esos prompts y el evaluador.

## Muestra y asignación

Se seleccionaron tres tareas antes de lanzar las sesiones. `move-only-join` permite medir una pérdida de compatibilidad encontrada al revisar el piloto. `locale-abi` fue la tarea con mayor consumo del grupo con skill. `const-buffer` representa una corrección pequeña donde la skill anterior ya había consumido menos que el control. La selección es intencional.

Cada tarea se ejecuta dos veces con cada guía. En la primera repetición comienza la guía anterior en las tareas de iteradores y buffer, y la candidata en la de ABI. En la segunda se invierte el orden. Se permiten hasta tres tareas concurrentes. Cada sesión tiene su propio árbol de trabajo y solo se puntúan los doce intentos asignados.

## Medidas y decisión

Se comparan los tokens totales de entrada y salida sumados en los seis intentos de cada revisión. Se informa de la caché por separado. Se exige mantener la aceptación y la suite de regresión del piloto. Una sesión sin entrega o telemetría completa queda identificada y no aporta evidencia de ahorro.

Además se compila un consumidor de prueba de la API existente de iteradores. Su cursor admite copia y elimina explícitamente el constructor de movimiento. Debe conservarse la construcción con dos lvalues y el formateo repetido. Se comprobó que este consumidor funciona en el padre y que las cuatro entregas del piloto anterior y la corrección histórica fallan en compilación. Es una prueba de compatibilidad construida después del primer piloto. La nueva guía se ha diseñado con ese defecto conocido; el prompt original se mantiene idéntico para ambos grupos.

El criterio para adoptar la candidata como mejora conjunta es obtener menos tokens agregados, mantener todos los resultados originales y superar al menos un control de compatibilidad adicional frente a la guía anterior en los pares nuevos. Se conservan los resultados desfavorables. Las mediciones describen estas tareas y ejecuciones.
