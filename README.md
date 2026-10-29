# Proyecto de Sistemas Operativos
## Proyecto 3: Tablas de páginas en acción (xv6)

### Integrantes

- Cristian Cabezas
- Johan Peña

### Curso

Sistemas Operativos

### Semestre

2026-2

# Descripción

Implementación sobre xv6-riscv, commit `06aad25c735fd3159bdfae5680be4eab7b1668b2`.
La Parte A permite obtener el PID sin una llamada al sistema, mediante una página de información de solo lectura. La Parte B permite inspeccionar recursivamente la tabla Sv39 del proceso. La Parte C consulta y limpia los bits de acceso de hasta 64 páginas, sin materializar las páginas perezosas inspeccionadas.

# Archivos modificados

| Archivo | Cambio y finalidad |
|---|---|
| `kernel/memlayout.h` | Define USYSCALL y struct usyscall, con protección para ensamblador. |
| `kernel/proc.h` | Guarda el puntero a la página compartida propiedad del proceso. |
| `kernel/proc.c` | Reserva, inicializa, mapea, desmapea y libera USYSCALL; protege el límite del heap. |
| `kernel/riscv.h` | Define PTE_A, bit 6. |
| `kernel/vm.c` | Implementa la impresión recursiva de las entradas válidas. |
| `kernel/sysproc.c` | Implementa los handlers de vmprint y pgaccess; ajusta el límite de sbrk perezoso. |
| `kernel/syscall.h` | Asigna los números 23 y 24 a vmprint y pgaccess. |
| `kernel/syscall.c` | Registra ambos handlers. |
| `kernel/defs.h` | Declara la función interna vmprint(pagetable_t). |
| `user/ulib.c` | Implementa ugetpid como lectura directa de USYSCALL. |
| `user/user.h` | Declara las tres interfaces públicas. |
| `user/usys.pl` | Genera los stubs de las dos llamadas nuevas; ugetpid no necesita ecall. |
| `Makefile` | Incluye pgtbltest en UPROGS. |

Se incluyen además `user/pgtbltest.c`, `user/usertests.c` y `ejecutar_pruebas.py`, proporcionados por el docente y conservados sin cambios. La sustitución de usertests respecto a xv6 original corresponde exclusivamente a la versión del curso.

# Integración con xv6

1. Clonar el repositorio oficial y seleccionar la base:

   ```bash
   git clone https://github.com/mit-pdos/xv6-riscv.git xv6-proyecto3
   cd xv6-proyecto3
   git switch -c proyecto3 06aad25
   ```

2. Copiar el contenido de esta entrega sobre esa carpeta, conservando las rutas.
3. Verificar que UPROGS contiene `$U/_pgtbltest`; el Makefile incluido ya lo incorpora.
4. Compilar e iniciar con `make qemu`. Dentro de xv6 ejecutar `pgtbltest` y `usertests -q`. Salir con Ctrl-a seguido de x.
5. Para generar la evidencia automáticamente, desde Linux ejecutar `python3 ejecutar_pruebas.py` en la raíz de xv6. Requiere Python 3, make, Perl, GCC para RISC-V, herramientas binutils para RISC-V y qemu-system-riscv64.

# Respuestas a las preguntas

## 1. Interpretación de vmprint()

Sv39 usa tres índices de 9 bits y un desplazamiento de 12 bits. Cada tabla contiene 512 entradas. Las entradas de los niveles superiores cubren rangos de 1 GiB y 2 MiB; las hojas de último nivel describen páginas de 4 KiB. El recorrido acumula los índices para reconstruir la dirección virtual y usa PTE2PA para obtener la dirección física.

En la ejecución registrada de pgtbltest se observaron las siguientes hojas. Los permisos se obtienen de las PTE; la identificación de guarda y pila coincide con la construcción de la imagen en exec.c. Esta captura incluye una región de heap reservada por la prueba, además de las páginas de la imagen recién cargada.

| Dirección virtual | Contenido | Bits de permisos observados |
|---|---|---|
| 0x0000 y 0x1000 | Texto y contenido de solo lectura de la imagen ejecutable | V, R, X, U |
| 0x2000 | Datos/BSS de la imagen | V, R, W, U |
| 0x3000 | Guarda situada debajo de la pila | V, R, W; sin U |
| 0x4000 | Pila de usuario | V, R, W, U |
| 0x8000 | Cuarta página de la región perezosa iniciada en 0x5000, única tocada | V, R, W, U |
| 0x3fffffd000 | USYSCALL: PID del proceso | V, R, U |
| 0x3fffffe000 | Trapframe: registros guardados para entrar/salir del kernel | V, R, W; sin U |
| 0x3ffffff000 | Trampolín: código de transición entre usuario y kernel | V, R, X; sin U |

Los bits A y D pueden aparecer además de estos permisos y describen accesos/modificaciones. Las páginas perezosas no tocadas en 0x5000, 0x6000, 0x7000 y 0x9000..0xc000 no aparecen como hojas. La salida completa está en pruebas/pgtbltest.txt. Las direcciones físicas dependen de la ejecución y no deben memorizarse.

## 2. Página de guarda

La guarda tiene una PTE válida, pero carece de PTE_U. Por eso un acceso en modo usuario produce una excepción aunque exista memoria física. La pila crece hacia direcciones menores: al entrar en la guarda, el acceso falla y el kernel termina el proceso. La guarda detecta los accesos que la alcanzan; no garantiza detectar un salto arbitrario que la sobrepase por completo.

## 3. Otras llamadas que se pueden acelerar

uptime podría exponer un contador de ticks en memoria de solo lectura, actualizado por el kernel. Habría que definir su consistencia y actualización; nuestra página actual solo guarda un PID y no implementa esa extensión. Las consultas de información estable o de contadores publicados son candidatas cuando el kernel mantiene una copia válida y la lectura resulta consistente.

write, fork, exec, kill y sbrk requieren acciones del kernel, validaciones, asignaciones o cambios de estado; leer una página no realiza esas operaciones. Acelerar una consulta requiere analizar su semántica, no solo copiar un valor: en datos compartidos que cambian también deben resolverse sincronización y lecturas parciales.

## 4. Riesgo de USYSCALL con PTE_W

El proceso podría falsificar el PID leído por ugetpid y alterar los datos compartidos. El kernel no debe confiar en información modificable por usuario. En esta implementación cambiar ese campo no cambiaría automáticamente p->pid ni otorgaría privilegios, pero rompería la integridad de la interfaz. Se mapea con R y U, sin W ni X, y se inicializa toda la página a cero para no exponer contenido residual del asignador.

## 5. pgaccess() y páginas perezosas

sbrklazy aumenta el tamaño virtual sin reservar inmediatamente una página física. Si no existe una PTE válida para una página inspeccionada, no hay un bit A que indique un acceso y se reporta cero. walk se invoca con alloc=0: la consulta no crea tablas ni llama a vmfault para las páginas observadas. Asignarlas consumiría memoria, cambiaría el estado observado y eliminaría el beneficio de la asignación perezosa.

# Resultados de las pruebas

Pruebas ejecutadas por el estudiante en Ubuntu/WSL y QEMU el 29 de octubre de 2026, sobre una copia limpia del commit requerido, reconstruida con los archivos de entrega:

| Comprobación | Resultado registrado |
|---|---|
| pgtbltest | 11 de 11; PASÓ; 95 segundos incluyendo compilación |
| Formato de vmprint | OK |
| usertests -q | ALL TESTS PASSED; PASÓ; 149 segundos |

Las salidas completas originales están en pruebas/. Los fallos de acceso provocados deliberadamente por las pruebas no son panics del kernel. La suite completa no reportó pérdida de páginas libres.

La comprobación inicial limpia se hizo sobre una exportación del commit base. Se regenerará la evidencia en una reconstrucción asociada al commit de estas fuentes y se incorporarán los logs en un commit posterior, sin editar manualmente sus encabezados. El commit indicado en los logs identifica la versión probada; el commit posterior añade la evidencia.

# Decisiones de diseño

- La página USYSCALL pertenece al proceso. proc_freepagetable solo elimina el mapeo; freeproc libera la página física. Esto permite que exec sustituya la tabla sin destruir la información que debe conservar el proceso.
- allocproc inicializa una página propia con el PID asignado. El hijo creado mediante fork obtiene su PID, sin copiar el del padre en USYSCALL.
- Los fallos de reserva o mapeo deshacen las asignaciones previas. Los límites de crecimiento normal y perezoso usan USYSCALL.
- vmprint recorre únicamente entradas válidas y solo desciende por entradas que no tienen R, W ni X. No reserva memoria.
- pgaccess valida alineación, cantidad y rangos mediante restas para evitar desbordamientos. Usa 1ULL para representar correctamente el bit 63.
- Se limpia únicamente PTE_A y se ejecuta sfence_vma antes de volver al usuario. No se eliminan permisos ni otros bits de la PTE.
- El resultado se copia con la interfaz copyout de este commit, que incluye el tamaño del proceso. La inspección es no asignante; copyout conserva el comportamiento normal de xv6 para el destino del resultado.

# Limitaciones conocidas

- El enlazador emite una advertencia LOAD RWX del kernel. Se reprodujo exactamente en una compilación del commit original sin cambios: no fue introducida por este proyecto. La compilación completa también registra una advertencia RWX de user/_forktest, cuyo fuente y regla de enlace no fueron modificados; la compilación de referencia solo construyó el kernel y no comprobó ese binario. No se han ocultado advertencias. Debe aclararse con el docente si estas advertencias de enlace preexistentes se exceptúan del requisito de compilación sin advertencias.
- La integración sobre una copia limpia fue verificada: pasaron ambas suites y el verificador de formato. Se generará la evidencia definitiva identificando el commit de las fuentes antes de publicar la entrega.
- Si copyout falla, pgaccess devuelve -1, pero los bits consultados ya fueron limpiados; la operación no ofrece reversión transaccional. El enunciado no prescribe conservarlos en ese caso.
- No se implementan Copy-on-Write, superpáginas ni reemplazo de páginas, fuera del alcance solicitado.
- Los integrantes deben revisar estas explicaciones y practicar la sustentación antes de entregar.

# Declaración de uso de IA

Se utilizó OpenAI Codex como apoyo para analizar el enunciado y las pruebas, preparar una primera implementación, revisar las salidas reales y redactar este borrador de documentación. Las pruebas se ejecutaron en Ubuntu/WSL por el estudiante. Los integrantes deben revisar, comprender y poder justificar el código y la documentación, y asumen plena responsabilidad académica sobre el contenido entregado.
