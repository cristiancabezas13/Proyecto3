// pgtbltest: pruebas del Proyecto 3 (Tablas de páginas en acción).
// Sistemas Operativos 2026-2, Universidad EAFIT.
//
// Uso dentro de xv6:
//   $ pgtbltest            ejecuta todas las pruebas
//   $ pgtbltest <nombre>   ejecuta solo la prueba indicada
//   $ pgtbltest -l         lista las pruebas disponibles
//
// Cada prueba corre en un proceso hijo. Si el kernel mata a ese proceso
// o la prueba encuentra un error, se reporta FALLO y se sigue con la
// siguiente. Al final se imprime un resumen.
//
// La prueba vmprint imprime la tabla de páginas en la consola. Un
// programa de usuario no puede leer esa salida, así que pgtbltest solo
// verifica que la llamada funcione; el formato lo revisa el script
// ejecutar_pruebas.py (o el estudiante, comparando con la guía).
//
// Algunas pruebas provocan accesos inválidos a propósito. Por eso es
// normal ver mensajes del kernel como
//   usertrap(): unexpected scause 0xf pid=...
// siempre que la prueba correspondiente termine en OK.

#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "user/user.h"

#define SBRK_FALLO ((char *)-1)

static char *nombre_actual = "";

static void
fallo(char *motivo)
{
  printf("prueba %s: FALLO: %s\n", nombre_actual, motivo);
  exit(1);
}

// Ejecuta f() en un hijo y devuelve su estado de salida.
// Un hijo que el kernel mata termina con estado -1.
static int
en_hijo(void (*f)(void))
{
  int pid = fork();
  if(pid < 0)
    fallo("fork() falló");
  if(pid == 0){
    f();
    exit(0);
  }
  int estado;
  wait(&estado);
  return estado;
}

// Cuenta las páginas libres: un hijo pide memoria de una página a la
// vez (sbrk normal, no perezoso) hasta que sbrk() falla.
static int
contar_paginas_libres(void)
{
  int fds[2];
  if(pipe(fds) < 0)
    fallo("pipe() falló");
  int pid = fork();
  if(pid < 0)
    fallo("fork() falló");
  if(pid == 0){
    close(fds[0]);
    while(1){
      char *a = sbrk(PGSIZE);
      if(a == SBRK_FALLO)
        break;
      a[PGSIZE - 1] = 1;
      if(write(fds[1], "x", 1) != 1)
        break;
    }
    exit(0);
  }
  close(fds[1]);
  int n = 0;
  char c;
  while(read(fds[0], &c, 1) == 1)
    n++;
  close(fds[0]);
  wait(0);
  return n;
}

// ---------------------------------------------------------------------
// Parte A: página compartida y ugetpid()
// ---------------------------------------------------------------------

static void
ugetpid_basico(void)
{
  if(ugetpid() != getpid())
    fallo("ugetpid() no coincide con getpid()");
}

static void
ugetpid_hijos(void)
{
  for(int i = 0; i < 8; i++){
    int pid = fork();
    if(pid < 0)
      fallo("fork() falló");
    if(pid == 0)
      exit(ugetpid() == getpid() ? 0 : 1);
    int estado;
    wait(&estado);
    if(estado != 0)
      fallo("en un proceso hijo, ugetpid() no coincide con getpid()");
  }
}

static void
ugetpid_exec(void)
{
  int pid = fork();
  if(pid < 0)
    fallo("fork() falló");
  if(pid == 0){
    char *argv[] = {"pgtbltest", "--revisar-ugetpid", 0};
    exec("pgtbltest", argv);
    exit(2);
  }
  int estado;
  wait(&estado);
  if(estado == 2)
    fallo("exec(\"pgtbltest\") falló");
  if(estado != 0)
    fallo("después de exec(), ugetpid() no coincide con getpid()");
}

static void
escribir_usyscall(void)
{
  ((struct usyscall *)USYSCALL)->pid = 12345;
  // si el kernel no mata al proceso, la página no es de solo lectura
  exit(2);
}

static void
usyscall_solo_lectura(void)
{
  int pid = getpid();
  if(en_hijo(escribir_usyscall) != -1)
    fallo("escribir en USYSCALL no mató al proceso (la página no es de solo lectura)");
  if(ugetpid() != pid)
    fallo("ugetpid() cambió después del intento de escritura");
}

static void
crecer_hasta_el_limite(void)
{
  // sbrklazy() no asigna memoria, así que se puede crecer hasta el límite
  while(sbrklazy(1 << 30) != SBRK_FALLO)
    ;
  while(sbrklazy(PGSIZE) != SBRK_FALLO)
    ;
  char *fin = sbrklazy(0);
  if((uint64)fin > USYSCALL)
    exit(3);
  if(ugetpid() != getpid())
    exit(4);
  exit(0);
}

static void
heap_no_invade(void)
{
  int estado = en_hijo(crecer_hasta_el_limite);
  if(estado == 3)
    fallo("el heap creció por encima de USYSCALL");
  if(estado == 4)
    fallo("al crecer el heap hasta el límite, ugetpid() dejó de funcionar");
  if(estado != 0)
    fallo("el proceso murió al crecer el heap hasta el límite");
}

static void
sin_fugas_usyscall(void)
{
  int antes = contar_paginas_libres();
  for(int i = 0; i < 40; i++){
    int pid = fork();
    if(pid < 0)
      fallo("fork() falló");
    if(pid == 0){
      char *argv[] = {"pgtbltest", "--revisar-ugetpid", 0};
      exec("pgtbltest", argv);
      exit(2);
    }
    wait(0);
  }
  int despues = contar_paginas_libres();
  if(despues < antes){
    printf("(antes %d páginas libres, después %d)\n", antes, despues);
    fallo("se pierden páginas al crear y destruir procesos");
  }
}

// ---------------------------------------------------------------------
// Parte B: vmprint()
// ---------------------------------------------------------------------

static void
vmprint_llamada(void)
{
  // Una región perezosa de 8 páginas en la que solo se toca la página 3.
  char *a = sbrklazy(8 * PGSIZE);
  if(a == SBRK_FALLO)
    fallo("sbrklazy() falló");
  uint64 mask;
  pgaccess(a, 8, &mask);   // no debe asignar ninguna página
  a[3 * PGSIZE] = 1;
  printf("vmprint: region_perezosa=%p tocada=%p\n", a, a + 3 * PGSIZE);
  printf("vmprint: inicio\n");
  if(vmprint() != 0)
    fallo("vmprint() no devolvió 0");
  printf("vmprint: fin\n");
}

// ---------------------------------------------------------------------
// Parte C: pgaccess()
// ---------------------------------------------------------------------

static void
pgaccess_basico(void)
{
  int npag = 32;
  char *a = sbrk(npag * PGSIZE);
  if(a == SBRK_FALLO)
    fallo("sbrk() falló");
  uint64 mask;
  if(pgaccess(a, npag, &mask) < 0)
    fallo("pgaccess() devolvió -1 con argumentos válidos");

  a[1 * PGSIZE] = 1;
  a[2 * PGSIZE] = 2;
  a[30 * PGSIZE] = 3;
  volatile char c = a[5 * PGSIZE];
  (void)c;
  if(pgaccess(a, npag, &mask) < 0)
    fallo("pgaccess() devolvió -1 con argumentos válidos");
  uint64 esperado = (1L << 1) | (1L << 2) | (1L << 5) | (1L << 30);
  if(mask != esperado){
    printf("(esperado %p, obtenido %p)\n", (void *)esperado, (void *)mask);
    fallo("pgaccess() no reportó exactamente las páginas leídas y escritas");
  }

  if(pgaccess(a, npag, &mask) < 0 || mask != 0)
    fallo("pgaccess() no limpió PTE_A: una segunda consulta reporta accesos viejos");

  a[7 * PGSIZE] = 1;
  if(pgaccess(a, npag, &mask) < 0 || mask != (1L << 7))
    fallo("pgaccess() no detectó un acceso nuevo después de limpiar");
  sbrk(-npag * PGSIZE);
}

static void
pgaccess_64(void)
{
  char *a = sbrk(64 * PGSIZE);
  if(a == SBRK_FALLO)
    fallo("sbrk() falló");
  uint64 mask;
  pgaccess(a, 64, &mask);
  a[0] = 1;
  a[63 * PGSIZE] = 1;
  if(pgaccess(a, 64, &mask) < 0)
    fallo("pgaccess() con 64 páginas devolvió -1");
  if(mask != ((1UL << 63) | 1))
    fallo("pgaccess() con 64 páginas no reportó la primera y la última");
  sbrk(-64 * PGSIZE);
}

static void
pgaccess_argumentos(void)
{
  char *a = sbrk(4 * PGSIZE);
  if(a == SBRK_FALLO)
    fallo("sbrk() falló");
  uint64 mask;
  if(pgaccess(a + 1, 1, &mask) != -1)
    fallo("pgaccess() aceptó una dirección no alineada a página");
  if(pgaccess(a, 0, &mask) != -1)
    fallo("pgaccess() aceptó npages = 0");
  if(pgaccess(a, 65, &mask) != -1)
    fallo("pgaccess() aceptó npages = 65");
  if(pgaccess(a, -1, &mask) != -1)
    fallo("pgaccess() aceptó npages negativo");
  if(pgaccess(a, 5, &mask) != -1)
    fallo("pgaccess() aceptó un rango que se sale del proceso");
  if(pgaccess(a, 4, (uint64 *)(sbrk(0) + 10 * PGSIZE)) != -1)
    fallo("pgaccess() aceptó un puntero de resultado fuera del proceso");
  if(pgaccess(a, 4, &mask) != 0)
    fallo("pgaccess() rechazó argumentos válidos");
  sbrk(-4 * PGSIZE);
}

static void
pgaccess_perezoso(void)
{
  char *a = sbrklazy(16 * PGSIZE);
  if(a == SBRK_FALLO)
    fallo("sbrklazy() falló");
  uint64 mask = 1;
  if(pgaccess(a, 16, &mask) != 0)
    fallo("pgaccess() sobre memoria perezosa devolvió -1");
  if(mask != 0)
    fallo("pgaccess() reportó accesos en páginas perezosas nunca tocadas");
  a[9 * PGSIZE] = 1;
  if(pgaccess(a, 16, &mask) != 0 || mask != (1L << 9))
    fallo("pgaccess() no detectó el acceso a una página perezosa recién asignada");
  sbrklazy(-16 * PGSIZE);
}

struct prueba {
  void (*f)(void);
  char *nombre;
} pruebas[] = {
  {ugetpid_basico, "ugetpid_basico"},
  {ugetpid_hijos, "ugetpid_hijos"},
  {ugetpid_exec, "ugetpid_exec"},
  {usyscall_solo_lectura, "usyscall_solo_lectura"},
  {heap_no_invade, "heap_no_invade"},
  {sin_fugas_usyscall, "sin_fugas_usyscall"},
  {vmprint_llamada, "vmprint"},
  {pgaccess_basico, "pgaccess_basico"},
  {pgaccess_64, "pgaccess_64"},
  {pgaccess_argumentos, "pgaccess_argumentos"},
  {pgaccess_perezoso, "pgaccess_perezoso"},
  {0, 0},
};

static int
correr(struct prueba *p)
{
  nombre_actual = p->nombre;
  int pid = fork();
  if(pid < 0){
    printf("prueba %s: FALLO: fork() falló\n", p->nombre);
    return 0;
  }
  if(pid == 0){
    p->f();
    exit(0);
  }
  int estado;
  wait(&estado);
  if(estado == 0){
    printf("prueba %s: OK\n", p->nombre);
    return 1;
  }
  if(estado == -1)
    printf("prueba %s: FALLO: el kernel mató al proceso de la prueba\n", p->nombre);
  return 0;
}

int
main(int argc, char *argv[])
{
  char *solo = 0;
  if(argc > 1){
    if(strcmp(argv[1], "--revisar-ugetpid") == 0)
      exit(ugetpid() == getpid() ? 0 : 1);
    if(strcmp(argv[1], "-l") == 0){
      for(struct prueba *p = pruebas; p->f; p++)
        printf("%s\n", p->nombre);
      exit(0);
    }
    solo = argv[1];
  }

  printf("pgtbltest: inicio\n");
  int total = 0, ok = 0;
  for(struct prueba *p = pruebas; p->f; p++){
    if(solo && strcmp(solo, p->nombre) != 0)
      continue;
    total++;
    ok += correr(p);
  }
  if(total == 0){
    printf("pgtbltest: no existe la prueba %s (use pgtbltest -l)\n", solo);
    exit(1);
  }
  if(ok == total){
    printf("pgtbltest: TODAS LAS PRUEBAS PASARON (%d de %d)\n", ok, total);
    exit(0);
  }
  printf("pgtbltest: FALLARON %d de %d PRUEBAS\n", total - ok, total);
  exit(1);
}
