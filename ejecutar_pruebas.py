#!/usr/bin/env python3
# ejecutar_pruebas.py: corre pgtbltest y usertests en xv6 y guarda la
# evidencia en la carpeta pruebas/.
# Proyecto 3, Sistemas Operativos 2026-2, Universidad EAFIT.
#
# Uso, desde la carpeta raíz de xv6 (donde está el Makefile):
#   python3 ejecutar_pruebas.py              corre pgtbltest y usertests -q
#   python3 ejecutar_pruebas.py pgtbltest    corre solo pgtbltest
#   python3 ejecutar_pruebas.py usertests    corre solo usertests -q
#
# El script compila xv6 (make qemu), arranca QEMU, escribe el comando en
# la consola de xv6, espera a que termine y guarda toda la salida en
# pruebas/pgtbltest.txt y pruebas/usertests.txt, con un encabezado que
# indica la fecha, el commit y el resultado. Para pgtbltest, además
# revisa el formato de la salida de vmprint().

import os
import re
import select
import signal
import subprocess
import sys
import time

PRUEBAS = {
    # nombre: (comando en xv6, segundos máximos, texto que indica éxito)
    "pgtbltest": ("pgtbltest", 600, "pgtbltest: TODAS LAS PRUEBAS PASARON"),
    "usertests": ("usertests -q", 1800, "ALL TESTS PASSED"),
}


def git(*args):
    try:
        return subprocess.run(["git", *args], capture_output=True,
                              text=True).stdout.strip()
    except OSError:
        return ""


USYSCALL = 0x3fffffd000
TRAPFRAME = 0x3fffffe000
TRAMPOLINE = 0x3ffffff000
PGSIZE = 4096
LINEA = re.compile(r"^((?: \.\.){1,3})(0x[0-9a-f]{16}): pte (0x[0-9a-f]{16}) pa (0x[0-9a-f]{16})$")


def revisar_vmprint(salida):
    """Revisa la salida de vmprint() que imprime la prueba vmprint.
    Devuelve una lista de errores (vacía si todo está bien)."""
    lineas = [l.rstrip("\r") for l in salida.splitlines()]
    try:
        info = next(l for l in lineas if l.startswith("vmprint: region_perezosa="))
        i0 = lineas.index("vmprint: inicio")
        i1 = lineas.index("vmprint: fin")
    except (StopIteration, ValueError):
        return ["no se encontró la salida de la prueba vmprint"]
    m = re.search(r"region_perezosa=(0x[0-9a-f]+) tocada=(0x[0-9a-f]+)", info)
    region, tocada = int(m.group(1), 16), int(m.group(2), 16)
    cuerpo = [l for l in lineas[i0 + 1:i1] if l and not l.startswith(("usertrap", "    "))]
    errores = []
    if not cuerpo or not re.match(r"^page table 0x[0-9a-f]{16}$", cuerpo[0]):
        return ["la primera línea debe ser 'page table 0x<16 dígitos hex>'"]
    hojas = {}
    for l in cuerpo[1:]:
        m = LINEA.match(l)
        if not m:
            errores.append("línea con formato incorrecto: %r" % l)
            continue
        nivel = len(m.group(1)) // 3
        va, pte, pa = (int(m.group(k), 16) for k in (2, 3, 4))
        if not pte & 1:
            errores.append("se imprimió una PTE no válida: %r" % l)
        if (pte >> 10) << 12 != pa:
            errores.append("pa no corresponde a la PTE: %r" % l)
        if nivel == 3:
            hojas[va] = pte
        elif pte & 0xe:
            errores.append("una PTE de nivel %d no debería ser hoja: %r" % (nivel, l))
    if USYSCALL not in hojas:
        errores.append("no aparece la página USYSCALL (0x%x)" % USYSCALL)
    elif hojas[USYSCALL] & 0x1f != 0x13:
        errores.append("USYSCALL debe tener exactamente PTE_V, PTE_R y PTE_U")
    for va, nombre in ((TRAPFRAME, "TRAPFRAME"), (TRAMPOLINE, "TRAMPOLINE"), (0, "la primera página de texto")):
        if va not in hojas:
            errores.append("no aparece %s (0x%x)" % (nombre, va))
    if tocada not in hojas:
        errores.append("no aparece la página perezosa que el proceso tocó (0x%x)" % tocada)
    for k in range(8):
        va = region + k * PGSIZE
        if va != tocada and va in hojas:
            errores.append("aparece la página perezosa no tocada 0x%x (pgaccess o vmprint le asignaron memoria)" % va)
    return errores


def correr_en_xv6(comando, limite):
    """Arranca xv6, ejecuta un comando y devuelve (salida, motivo_fin)."""
    qemu = subprocess.Popen(["make", "qemu"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            start_new_session=True)
    salida = b""
    enviado = False
    fin_comando = 0
    inicio = time.time()
    motivo = "tiempo agotado"
    try:
        while time.time() - inicio < limite:
            listo, _, _ = select.select([qemu.stdout], [], [], 0.5)
            if listo:
                bloque = os.read(qemu.stdout.fileno(), 4096)
                if not bloque:
                    motivo = "QEMU terminó inesperadamente"
                    break
                salida += bloque
                sys.stdout.buffer.write(bloque)
                sys.stdout.flush()
            if b"panic" in salida[fin_comando:]:
                time.sleep(1)
                motivo = "el kernel entró en pánico"
                break
            if salida.endswith(b"$ "):
                if not enviado:
                    qemu.stdin.write((comando + "\n").encode())
                    qemu.stdin.flush()
                    enviado = True
                    fin_comando = len(salida)
                elif len(salida) > fin_comando + len(comando) + 2:
                    motivo = "terminó"
                    break
    finally:
        try:
            os.killpg(qemu.pid, signal.SIGKILL)
        except OSError:
            pass
    return salida.decode("utf-8", errors="replace"), motivo


def main():
    if not os.path.exists("Makefile") or not os.path.isdir("kernel"):
        sys.exit("Ejecute este script desde la carpeta raíz de xv6.")
    nombres = sys.argv[1:] or list(PRUEBAS)
    for n in nombres:
        if n not in PRUEBAS:
            sys.exit("Prueba desconocida: %s (use pgtbltest o usertests)" % n)

    os.makedirs("pruebas", exist_ok=True)
    todo_ok = True
    for n in nombres:
        comando, limite, exito = PRUEBAS[n]
        print("\n=== Ejecutando %s (puede tardar varios minutos) ===\n" % comando)
        inicio = time.time()
        salida, motivo = correr_en_xv6(comando, limite)
        duracion = time.time() - inicio
        paso = exito in salida and motivo == "terminó"
        extra = ""
        if n == "pgtbltest" and "vmprint: inicio" in salida:
            errores = revisar_vmprint(salida)
            if errores:
                paso = False
                extra = "\nRevisión del formato de vmprint(): FALLÓ\n" + "".join("  - %s\n" % e for e in errores)
            else:
                extra = "\nRevisión del formato de vmprint(): OK\n"
            print(extra)
        todo_ok = todo_ok and paso

        encabezado = [
            "Proyecto 3 - Tablas de páginas en acción - evidencia de pruebas",
            "Prueba:     %s" % comando,
            "Fecha:      %s" % time.strftime("%Y-%m-%d %H:%M:%S"),
            "Commit:     %s" % (git("rev-parse", "--short", "HEAD") or "(sin git)"),
            "Máquina:    %s" % (os.uname().nodename),
            "Duración:   %.0f s" % duracion,
            "Fin:        %s" % motivo,
            "Resultado:  %s" % ("PASÓ" if paso else "FALLÓ"),
            "-" * 60,
            "",
        ]
        archivo = os.path.join("pruebas", "%s.txt" % n)
        with open(archivo, "w", encoding="utf-8") as f:
            f.write("\n".join(encabezado) + salida + extra)
        print("\n=== %s: %s. Salida guardada en %s ===" %
              (comando, "PASÓ" if paso else "FALLÓ", archivo))

    sys.exit(0 if todo_ok else 1)


if __name__ == "__main__":
    main()
