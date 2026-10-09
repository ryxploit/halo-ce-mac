# AGENTS.md --- Port de Halo CE a macOS

## Objetivo

Trabaja sobre este repositorio local y conviértelo, de forma
incremental, en una aplicación nativa para macOS que pueda ejecutarse en
Apple Silicon y distribuirse como `Halo CE.app` dentro de un instalador
`.dmg`.

La aplicación debe priorizar controles de escritorio: - Teclado y
ratón. - Mandos compatibles cuando la infraestructura existente lo
permita. - Ventana redimensionable y pantalla completa. - Configuración
gráfica y de controles persistente si encaja con la arquitectura
existente.

No diseñes una interfaz de controles táctiles para macOS. Conserva la
plataforma Android y cualquier otra plataforma existente: macOS debe
añadirse como un objetivo independiente.

## Reglas obligatorias de trabajo

1.  **Inspecciona antes de modificar.** Lee `README`, licencias,
    instrucciones de contribución, `AGENTS.md` existentes, scripts de
    build, CMake, dependencias, código de plataforma y CI.
2.  **Confirma qué repositorio es este.** No asumas que el checkout
    corresponde a una variante concreta. Identifica el upstream, el
    commit base, las ramas y las plataformas implementadas.
3.  **Protege el trabajo local.** Ejecuta `git status --short --branch`
    y revisa los cambios existentes. No sobrescribas, descartes ni
    reformatees cambios ajenos a esta tarea.
4.  **No hagas cambios destructivos.** No uses `git reset --hard`,
    `git clean -fd`, ni borres archivos o datos del usuario.
5.  **No inventes APIs, scripts ni rutas.** Verifica cada comando y
    objetivo de compilación en el repositorio antes de utilizarlo.
6.  **No declares éxito sin evidencia.** Ejecuta las comprobaciones
    posibles y distingue entre pruebas ejecutadas, no ejecutadas y
    bloqueadas.
7.  Trabaja en cambios pequeños y verificables. Al finalizar cada etapa,
    resume archivos modificados, motivo y resultados.
8.  Respeta todas las licencias y avisos de copyright. No añadas ni
    distribuyas assets propietarios del juego.

## Fase 1 --- Auditoría y viabilidad

Antes de cambiar código, genera `docs/macos-port-audit.md` con:

-   Arquitectura general y flujo de arranque.
-   Lenguajes, compiladores, build system y dependencias.
-   Puntos de entrada Android y capas específicas de plataforma.
-   Código compartido que pueda reutilizarse en macOS.
-   Dependencias exclusivas de Android.
-   Modelo de ejecución del motor, ABI, tamaño de punteros, supuestos de
    memoria y reservas de direcciones.
-   Renderizador actual y dependencias gráficas.
-   Audio, entrada, archivos, red y ciclo de vida.
-   Tests y comandos de compilación ya presentes.
-   Licencias relevantes.
-   Riesgos bloqueantes, cada uno con referencias concretas a archivos y
    símbolos.

No des por hecho que el runtime Android, el objetivo `arm64_32`, las
reservas de memoria ni OpenGL ES se trasladan directamente a macOS.
Verifica cada supuesto en el código.

**Puerta de decisión:** si la ejecución del motor depende de un runtime
incompatible con macOS, explica las alternativas técnicas antes de
implementar un port parcial engañoso. Prioriza reutilizar el motor
existente y la mínima capa de plataforma necesaria; no reescribas el
juego entero.

## Fase 2 --- Diseño del objetivo macOS

Propón una arquitectura que mantenga aislado el código específico de
plataforma:

-   Añadir un target macOS separado, sin romper Android.
-   Reutilizar C/C++ y bibliotecas multiplataforma cuando sean
    compatibles.
-   Usar SDL3 para ventana, eventos, teclado, ratón, audio y mandos si
    el repositorio ya la usa y su configuración macOS es válida.
-   Añadir una capa nativa de macOS únicamente donde sea necesaria.
-   Usar el backend gráfico que realmente soporte el motor. Investiga
    las diferencias entre OpenGL ES y OpenGL de escritorio; no asumas
    compatibilidad automática.
-   Mantener configuración, partidas guardadas y archivos del usuario
    fuera del bundle de la aplicación.
-   Evitar rutas absolutas de desarrollo y dependencias de archivos
    presentes solo en la máquina del desarrollador.

Documenta la decisión de arquitectura y sus compromisos en
`docs/macos-port-audit.md` antes de realizar refactors grandes.

## Fase 3 --- Implementación incremental

Implementa por hitos. No intentes resolverlo todo en un solo cambio.

### Hito A: configuración de build

-   Añade un target macOS arm64 independiente, usando el sistema de
    build existente cuando sea razonable.
-   Detecta herramientas y dependencias con errores claros.
-   Evita descargar o instalar herramientas globales automáticamente.
-   No cambies versiones de dependencias sin justificarlo.
-   Asegura que la configuración Android existente siga intacta.

### Hito B: ejecutable mínimo

-   Compila la capa macOS.
-   Abre una ventana nativa o SDL.
-   Inicializa y cierra correctamente los subsistemas.
-   Gestiona errores de inicialización, cierre de ventana y limpieza de
    recursos.
-   Añade una prueba o smoke test reproducible.

Si el motor no puede arrancar por incompatibilidad de ABI o memoria,
documenta el bloqueo exacto y presenta alternativas; no finjas que una
ventana vacía significa que el juego ya funciona.

### Hito C: motor y gráficos

-   Conecta el motor real al ciclo de ejecución macOS.
-   Resuelve las incompatibilidades verificadas del runtime y la
    memoria.
-   Implementa o adapta el backend gráfico necesario.
-   Verifica creación y destrucción de contexto, resize, pantalla
    completa y cierre limpio.
-   Comprueba errores de renderizado y recursos gráficos.

No realices una migración amplia a Metal ni una reescritura del
renderizador sin evaluar primero el coste y las APIs realmente
utilizadas.

### Hito D: entrada de escritorio

Implementa y documenta un mapa de controles coherente con el juego:

-   WASD o las teclas predeterminadas del motor para movimiento.
-   Ratón para mirar/apuntar.
-   Clic izquierdo para disparar y clic derecho para la acción
    secundaria cuando corresponda al esquema del juego.
-   Tecla para saltar, recargar, interactuar y cambiar armas, según las
    acciones disponibles en el motor.
-   Escape para pausar o abrir el menú cuando sea compatible.
-   Soporte para reasignación de teclas si la arquitectura lo permite.
-   Captura/liberación del cursor, incluyendo al perder el foco.
-   Mandos mediante SDL si ya están soportados y se pueden validar.

No inventes acciones inexistentes. Adapta el mapeo a las acciones reales
del motor y permite salir del modo de captura del ratón de forma segura.

### Hito E: recursos y almacenamiento

-   Proporciona un selector para que el usuario localice los datos del
    juego que posee.
-   No incluyas ni descargues imágenes de disco, mapas, música, vídeos
    ni otros recursos propietarios.
-   Valida las rutas y comunica claramente archivos faltantes o
    incompatibles.
-   Guarda configuración y datos de usuario en ubicaciones estándar de
    macOS.
-   No escribas dentro de `Contents/` del bundle en tiempo de ejecución.

### Hito F: aplicación `.app`

Genera un bundle válido con:

-   `Info.plist` y metadatos coherentes.
-   Identificador de bundle configurable y no presentado como oficial.
-   Icono propio que no infrinja derechos de terceros.
-   Ejecutable y bibliotecas necesarias en ubicaciones correctas.
-   Recursos y frameworks con rutas relativas y carga correcta.
-   Arquitectura arm64 inicialmente; considera universal2 solo si hay
    una necesidad real y todas las dependencias pueden compilarse para
    ambas arquitecturas.
-   Logs y errores comprensibles para diagnóstico.

### Hito G: distribución `.dmg`

Añade un script reproducible para crear el DMG desde una `.app` ya
compilada. Debe:

-   Crear una imagen de disco con nombre y volumen consistentes.
-   Incluir `Halo CE.app` y un acceso directo a `/Applications`.
-   Fallar con un mensaje claro si la aplicación no existe o está
    incompleta.
-   No requerir privilegios de administrador.
-   No afirmar que el paquete está firmado o notarizado si no se ha
    hecho.
-   Mantener la firma y notarización como pasos separados y
    documentados, con credenciales suministradas de forma segura por el
    propietario.

No guardes certificados, contraseñas, tokens ni credenciales en el
repositorio.

## Fase 4 --- Calidad y pruebas

Añade o adapta las comprobaciones apropiadas para macOS:

-   Configuración y compilación limpia.
-   Pruebas unitarias existentes compatibles.
-   Smoke test de inicio y cierre.
-   Verificación del bundle y de sus arquitecturas.
-   Verificación de dependencias dinámicas y rutas de carga.
-   Prueba de eventos de teclado, ratón y pérdida de foco.
-   Prueba de ventana, resize y pantalla completa.
-   Prueba de lectura de recursos y errores por archivos ausentes.
-   Generación y montaje/verificación del DMG cuando el entorno lo
    permita.

No marques una prueba como aprobada si no se ejecutó. Si no hay acceso a
un dispositivo, GUI o herramienta requerida, anótalo como limitación.

## Fase 5 --- Documentación para desarrolladores

Actualiza o crea:

-   `docs/macos-port-audit.md`
-   `docs/macos-build.md`
-   `docs/macos-controls.md`
-   `docs/macos-release.md`

Documenta herramientas y versiones verificadas, comandos reales,
requisitos, problemas conocidos, ubicación de artefactos y cómo limpiar
únicamente los outputs generados. No documentes comandos hipotéticos
como si funcionaran.

## Restricciones de alcance

-   No reescribas el motor en Swift, Rust o un framework de juegos
    nuevo.
-   No elimines Android ni cambies su comportamiento para facilitar
    macOS.
-   No introduzcas controles táctiles en la interfaz de escritorio.
-   No migres todo el proyecto a otro sistema de build sin demostrar que
    es necesario.
-   No añadas telemetría, analítica ni servicios de red nuevos.
-   No afirmes compatibilidad con versiones de macOS que no hayas
    verificado.
-   No distribuyas recursos de Halo ni presentes el proyecto como
    producto oficial de Microsoft.
-   No hagas commits ni publiques releases sin autorización explícita.

## Entregables y definición de terminado

La tarea se considera completada por etapas, no por crear archivos
vacíos. Informa claramente del estado de cada punto:

1.  Auditoría técnica con referencias al código.
2.  Decisión documentada sobre la viabilidad del runtime y del
    renderizador.
3.  Target macOS arm64 compilable.
4.  Motor funcionando en macOS, o bloqueo técnico reproducible y
    documentado.
5.  Teclado y ratón funcionales.
6.  Bundle `Halo CE.app`.
7.  Script reproducible de empaquetado `.dmg`.
8.  Tests ejecutados y resultados.
9.  Guía de compilación y distribución.

Al terminar, presenta un resumen con: - Qué funciona realmente. - Qué
archivos se modificaron. - Comandos ejecutados y resultados. - Ruta
exacta de la `.app` y del `.dmg`, si existen. - Bloqueos restantes y
siguiente paso recomendado.

**Empieza ahora por la Fase 1.** Inspecciona el checkout y entrega la
auditoría antes de decidir la implementación. Después continúa con el
primer hito viable sin esperar aprobación para tareas rutinarias, pero
detente ante decisiones arquitectónicas irreversibles o bloqueos que
requieran elegir entre alternativas sustancialmente distintas.
