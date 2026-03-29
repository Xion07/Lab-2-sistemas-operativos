```markdown
# Laboratorio de Sistemas Operativos — Práctica No. 2  
## API de Procesos: *wish* (Wisconsin Shell)

> **Facultad de Ingeniería · Ingeniería de Sistemas · Universidad de Antioquia** > Fecha de entrega: 12 de abril de 2026

---

## a) Integrantes

| Nombre completo | Correo institucional | N.º de documento |
|-----------------|----------------------|------------------|
| David Penagos | david.penagos1407@gmail.com / julian.penagos@udea.edu.co | |
| Andrea | | |

---

## b) Documentación de funciones

### `void print_error(void)`
Escribe el único mensaje de error permitido (`"An error has occurred\n"`) sobre
`stderr` usando `write()` directamente, tal como lo exige la sección 2.7 del
enunciado.

---

### `char *trim(char *s)`
Elimina espacios en blanco (`' '`, `'\t'`, `'\n'`, `'\r'`) al inicio y al final
de la cadena `s`.  
Retorna un puntero al primer carácter no-blanco dentro del mismo buffer (no aloca
memoria nueva). Es vital para limpiar los comandos antes de parsearlos.

---

### `void free_path(void)`
Libera con `free()` cada cadena almacenada dinámicamente en el arreglo global `search_path` y
reinicia el contador `path_count` a 0.  
Se llama antes de actualizar el search path con el built-in `route` y justo antes de un `exit` limpio.

---

### `void init_path(void)`
Inicializa el search path con el único directorio `/bin`, según lo especificado
en la sección 2.3.  
Es la primera función invocada en el `main()`.

---

### `char *find_executable(const char *cmd)`
Itera sobre cada directorio en `search_path` y construye la ruta completa
`<dir>/<cmd>`. Usa `access(ruta, X_OK)` para verificar si el archivo existe y
es ejecutable.  
Retorna un `strdup()` de la primera ruta válida encontrada, o `NULL` si no
existe en ningún directorio. El llamador es responsable de liberar la memoria.

---

### `pid_t run_command(char *segment)`
Procesa un único segmento de comando (ya separado de los operadores `&`).

Pasos internos:
1. **Validación de redirección**: cuenta ocurrencias de `>`. Más de una es error.
   Si hay exactamente una, extrae y valida el nombre del archivo destino
   (exactamente un token después de `>`).
2. **Parsing de argumentos**: usa `strsep()` para dividir por espacios/tabs y
   llena el arreglo `args[]`.
3. **Casos Borde**: Verifica que el comando no esté vacío (ej. redirección sin comando previo `> archivo.txt`).
4. **Built-ins**:
   - `exit`: invoca limpieza de memoria y `exit(0)` si no tiene argumentos; error en caso contrario.
   - `cd` / `chd`: invoca `chdir(args[1])`; error si argc ≠ 2 o si falla (soporta ambos nombres por la inconsistencia del PDF).
   - `path` / `route`: reemplaza todo el search path con los argumentos dados.
5. **Externos**: llama a `find_executable()` y hace `fork()`. El hijo aplica
   redirección con `dup2()` a STDOUT (1) y STDERR (2) si corresponde, y ejecuta con `execv()`. 

Retorna el PID del proceso hijo si se hizo fork, o `-1` si era built-in o hubo un error.

---

### `void process_line(char *line)`
Procesa una línea completa de entrada:
1. Divide la línea por el operador `&` usando `strsep()`, obteniendo los
   segmentos de comandos paralelos.
2. Para cada segmento hace una copia y llama a `run_command()`, ya que `strsep` modifica la cadena original.
3. Acumula los PIDs retornados en un arreglo local.
4. Llama a `waitpid()` mediante un ciclo `for`, esperando la finalización de
   todos los procesos hijos lanzados en paralelo (sección 2.6).

---

### `int main(int argc, char *argv[])`
Punto de entrada del shell.

- `argc == 1` → modo interactivo: imprime el prompt `wish> ` y lee de `stdin`.
- `argc == 2` → modo batch: abre el archivo dado como argumento y lee de él
  sin imprimir prompt.
- `argc > 2` → error + `exit(1)`.

Lee líneas con `getline()` en un bucle infinito. Al encontrar EOF libera memoria e invoca `exit(0)`.

---

## c) Problemas encontrados y soluciones

### 1. Inconsistencia de nombres en el documento guía
**Problema**: El PDF menciona al inicio que los comandos son `cd` y `path`, pero más abajo explica su funcionamiento llamándolos `chd` y `route`.  
**Solución**: En `run_command` utilizamos condicionales dobles (`strcmp` con un `||`) para que el shell acepte ambas nomenclaturas sin arrojar error.

---

### 2. Redirección de salida de errores (El "Giro" del PDF)
**Problema**: El enunciado (sección 2.5) exige que la redirección `>` también envíe los errores (`stderr`) al archivo, no solo la salida estándar (`stdout`).  
**Solución**: En el bloque del proceso hijo (`pid == 0`), después de abrir el archivo, usamos `dup2()` dos veces: una para `STDOUT_FILENO` y otra para `STDERR_FILENO`.

---

### 3. Falsos positivos en redirección vacía
**Problema**: Si el usuario digitaba solo `> archivo.txt` (sin comando a la izquierda), el parseo lo tomaba como válido pero fallaba al buscar el comando nulo.  
**Solución**: Agregamos una validación posterior al parseo de argumentos. Si `argc == 0` pero se detectó un archivo de redirección, imprimimos el error reglamentario y abortamos la ejecución de esa línea.

---

## d) Pruebas realizadas

### Prueba 1 — Comando externo simple
```bash
$ echo "ls" | ./wish
```
**Resultado esperado**: lista los archivos del directorio actual, ya que `/bin` está cargado por defecto. ✅

---

### Prueba 2 — Built-in `chd` (o `cd`)
```bash
$ printf "chd /tmp\npwd\n" | ./wish
```
**Resultado esperado**: imprime `/tmp`, confirmando el cambio de directorio exitoso. ✅

---

### Prueba 3 — Rutas (`route`) y manejo de errores
```bash
$ printf "route\nls\n" | ./wish
```
**Resultado esperado**: El primer comando vacía el path. El `ls` falla e imprime `An error has occurred`. ✅

---

### Prueba 4 — Redirección estricta `>`
```bash
$printf "ls -la /tmp > salida.txt\n" | ./wish$ cat salida.txt
```
**Resultado esperado**: stdout y stderr redirigidos al archivo, la pantalla queda limpia durante la ejecución. ✅

---

### Prueba 5 — Comandos paralelos `&` con `waitpid`
```bash
$ printf "sleep 2 & sleep 2 & sleep 2\n" | ./wish
```
**Resultado esperado**: Los tres comandos inician al mismo tiempo. El shell recupera el control exactamente en 2 segundos, demostrando concurrencia real. ✅

---

### Prueba 6 — Errores de sintaxis en redirección
```bash
$ printf "ls > f1 f2\n" | ./wish
```
**Resultado esperado**: Imprime `An error has occurred` debido a que hay más de un token después del operador de redirección. ✅

---

### Prueba 7 — Error: más de un argumento al shell
```bash
$ ./wish arg1 arg2
```
**Resultado esperado**: Imprime error y el programa finaliza inmediatamente con `exit(1)`. ✅

---

### Prueba 8 — Modo batch
```bash
$ ./wish comandos.txt
```
**Resultado esperado**: Ejecuta todos los comandos del archivo de texto en silencio (sin mostrar `wish> `) y termina. ✅

---

## e) Video de sustentación

> **Enlace**: > Duración: 10 minutos  
> Contenido: Demostración en vivo del shell en Fedora, explicación de la arquitectura del código fuente (especialmente `fork` y `waitpid`), recorrido por el manejo de redirección y pruebas de concurrencia.

---

## f) Manifiesto de transparencia — Uso de IA generativa

El diseño general de la arquitectura, la implementación de la lógica de procesos (`fork`/`exec`/`waitpid`), el manejo de punteros y la estructuración de las pruebas fueron realizados **manualmente** por los integrantes del grupo, basándonos en el libro guía de Remzi.

Se hizo un uso de IA Generativa para:

| Herramienta | Uso específico y puntual |
|-------------|--------------------------|
| **Asistente IA** | Consulta rápida sobre la sintaxis exacta de la función POSIX `strsep()` frente a `strtok()` para dividir cadenas, y verificación de los flags apropiados para compilar limpio con `gcc -Wall -Werror`. |

---

## Compilación y uso

```bash
# Compilar estrictamente sin advertencias
make

# O manualmente:
gcc -Wall -Werror -O2 -o wish wish.c

# Modo interactivo
./wish

# Modo batch
./wish comandos.txt
```
```